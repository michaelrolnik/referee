/*
 *  MIT License
 *  
 *  Copyright (c) 2022-2026 Michael Rolnik
 *  
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *  
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *  
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

#include "referee.hpp"

#include <fmt/format.h>

#include <atomic>
#include <iostream>

#include "antlr4-runtime/antlr4-runtime.h"
#include "refereeParser.h"
#include "refereeLexer.h"
#include "refereeBaseVisitor.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/TypeFinder.h"
#include "llvm/ADT/StringRef.h"
#include <llvm/Support/TargetSelect.h>
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/IRTransformLayer.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorSymbolDef.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#if __has_include("llvm/ExecutionEngine/Orc/AbsoluteSymbols.h")
#  include "llvm/ExecutionEngine/Orc/AbsoluteSymbols.h"
#endif
#include "rdb/database.hpp"
#include "rdb/ingest.hpp"

namespace {
bool isMinMaxIntrinsic(llvm::Intrinsic::ID id)
{
    using namespace llvm;
    switch (id)
    {
        case Intrinsic::smax:
        case Intrinsic::smin:
        case Intrinsic::umax:
        case Intrinsic::umin:
            return true;
        default:
            return false;
    }
}

void lowerMinMaxIntrinsics(llvm::Module& M)
{
    using namespace llvm;
    for (auto& F : M)
    {
        SmallVector<IntrinsicInst*, 8>  worklist;
        for (auto& I : instructions(F))
        {
            if (auto* II = dyn_cast<IntrinsicInst>(&I))
            {
                if (isMinMaxIntrinsic(II->getIntrinsicID()))
                    worklist.push_back(II);
            }
        }
        for (auto* II : worklist)
        {
            IRBuilder<>     B(II);
            Value*          lhs  = II->getArgOperand(0);
            Value*          rhs  = II->getArgOperand(1);
            CmpInst::Predicate pred;
            switch (II->getIntrinsicID())
            {
                case Intrinsic::smax:   pred = ICmpInst::ICMP_SGT; break;
                case Intrinsic::smin:   pred = ICmpInst::ICMP_SLT; break;
                case Intrinsic::umax:   pred = ICmpInst::ICMP_UGT; break;
                case Intrinsic::umin:   pred = ICmpInst::ICMP_ULT; break;
                default:                continue;
            }
            Value*      cmp  = B.CreateICmp(pred, lhs, rhs);
            Value*      sel  = B.CreateSelect(cmp, lhs, rhs);
            II->replaceAllUsesWith(sel);
            II->eraseFromParent();
        }
    }

    SmallVector<Function*, 4>   deadDecls;
    for (auto& F : M)
    {
        if (F.isDeclaration() && F.use_empty() && isMinMaxIntrinsic(F.getIntrinsicID()))
            deadDecls.push_back(&F);
    }
    for (auto* F : deadDecls)
        F->eraseFromParent();
}
} // namespace

#include <memory>
#include <iomanip>

#include "antlr2ast.hpp"
#include "visitors/compile.hpp"
#include "utils.hpp"

#include <sstream>

namespace {

static void debugCallback(int64_t value)
{
    std::cerr << value << "\n";
}

// Run the standard LLVM new-PM per-module O2 pipeline on `M`. This subsumes
// the InstCombine/Reassociate/GVN/SimplifyCFG/LoopDataPrefetch sequence the
// legacy FunctionPassManager used to run, and additionally schedules the
// loop-level transforms (rotate, LICM, indvar-simplify, unroll, distribute,
// vectorize, …) with the analyses they actually require.
static void optimizeModuleO2(llvm::Module& M)
{
    llvm::PassBuilder               PB;
    llvm::LoopAnalysisManager       LAM;
    llvm::FunctionAnalysisManager   FAM;
    llvm::CGSCCAnalysisManager      CGAM;
    llvm::ModuleAnalysisManager     MAM;

    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    auto    MPM = PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
    MPM.run(M, MAM);
}

} // anonymous namespace

// ── Compiled lifetime helpers ──────────────────────────────────────────────
// Defined out-of-line because the header forward-declares llvm::LLVMContext,
// llvm::Module, and Antlr2AST — destroying their unique_ptrs needs the full
// type, which is only available here.
Referee::Compiled::Compiled()                                       = default;
Referee::Compiled::Compiled(Compiled&&) noexcept                    = default;
Referee::Compiled& Referee::Compiled::operator=(Compiled&&) noexcept = default;
Referee::Compiled::~Compiled()                                      = default;

Referee::Compiled   Referee::compile(std::istream& is, std::string name,
                                     llvm::DataLayout const* dataLayout,
                                     std::vector<std::string> const& includePaths)
{
    Compiled    out;

    out.ctx     = std::make_unique<llvm::LLVMContext>();
    out.mod     = std::make_unique<llvm::Module>(name, *out.ctx);

    // Forward declaration of the host-side `debug(int64_t)` callback that
    // generated requirement IR may reference. The actual symbol is supplied
    // by the JIT (see Referee::execute) or by the host program (tests).
    {
        llvm::IRBuilder<>   b(*out.ctx);
        auto*   fty = llvm::FunctionType::get(b.getVoidTy(),
                                              {b.getInt64Ty()}, false);
        (void) llvm::Function::Create(fty, llvm::Function::ExternalLinkage,
                                      "debug", *out.mod);
    }

    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    // Pin the module to the requested data layout. Aggregate sizes/alignments
    // queried during Compile::make depend on this, so it must be set before
    // we lower the AST.
    {
        llvm::Triple    triple{llvm::sys::getDefaultTargetTriple()};
        if (dataLayout)
        {
            out.mod->setDataLayout(*dataLayout);
            out.mod->setTargetTriple(triple.getTriple());
        }
        else
        {
            llvm::orc::JITTargetMachineBuilder  JTMB{triple};
            if (auto DL = JTMB.getDefaultDataLayoutForTarget())
            {
                out.mod->setDataLayout(*DL);
                out.mod->setTargetTriple(triple.getTriple());
            }
        }
    }

    // Parse and AST-build. The Antlr2AST owns the AST nodes pointed at by
    // out.ast; we move it into Compiled so the AST stays valid for callers
    // (notably Referee::execute, which still consults the AST after the LLVM
    // module has been transferred to the JIT).
    antlr4::ANTLRInputStream    input(is);
    referee::refereeLexer       lexer(&input);
    antlr4::CommonTokenStream   tokens(&lexer);
    referee::refereeParser      parser(&tokens);

    // Antlr2AST resolves its Module via Factory<Module>::create(name), which
    // memoises by `name`. If compile() is called twice with the same name
    // (e.g. tests that compile the same .ref multiple times), the second call
    // re-adds the same data/conf decls to the cached Module and throws.
    // Tagging the Antlr2AST name with a process-unique counter keeps every
    // compile self-contained.
    static std::atomic<unsigned>    s_uniq{0};
    auto    uniqName    = name + "#compile:" + std::to_string(s_uniq.fetch_add(1));

    out.astOwner    = std::make_unique<Antlr2AST>(uniqName, name, includePaths);
    auto*   tree    = parser.program();
    out.ast         = std::any_cast<::Module*>(out.astOwner->visitProgram(tree));

    Compile::make(out.ctx.get(), out.mod.get(), out.ast);

    optimizeModuleO2(*out.mod);

    // Lower min/max intrinsics after optimization so that any new ones
    // introduced by the optimizer (e.g. via SCEV/IndVarSimplify) are also
    // expanded into icmp+select for the ORC JIT.
    lowerMinMaxIntrinsics(*out.mod);

    return out;
}

// ── Schema lifetime helpers (out-of-line for the same forward-decl reasons
//    as Compiled). ────────────────────────────────────────────────────────────
Referee::Schema::Schema()                                       = default;
Referee::Schema::Schema(Schema&&) noexcept                      = default;
Referee::Schema& Referee::Schema::operator=(Schema&&) noexcept  = default;
Referee::Schema::~Schema()                                      = default;

Referee::Schema     Referee::parseSchema(std::istream& is, std::string name,
                                         std::vector<std::string> const& includePaths)
{
    Schema  out;
    antlr4::ANTLRInputStream    input(is);
    referee::refereeLexer       lexer(&input);
    antlr4::CommonTokenStream   tokens(&lexer);
    referee::refereeParser      parser(&tokens);

    // Antlr2AST goes through Factory<Module>::create(name), which caches by
    // the exact name string. If parseSchema is called for the same path that
    // Referee::compile was (or will be) invoked on, the cached Module would
    // get its data/conf decls added a second time and throw. Tag the schema
    // parse with a per-call counter so it gets its own Module.
    static std::atomic<unsigned>    s_uniq{0};
    auto    uniqName    = name + "#schema:" + std::to_string(s_uniq.fetch_add(1));

    out.astOwner    = std::make_unique<Antlr2AST>(uniqName, name, includePaths);
    auto*   tree    = parser.program();
    out.ast         = std::any_cast<::Module*>(out.astOwner->visitProgram(tree));
    return out;
}

bool    Referee::printIR(std::istream& is, std::string name, std::ostream& os,
                         std::vector<std::string> const& includePaths)
{
    try
    {
        auto    built   = compile(is, name, nullptr, includePaths);
        auto    raw     = llvm::raw_os_ostream(os);
        built.mod->print(raw, nullptr);
        return true;
    }
    catch(Exception& e)
    {
        std::cerr << "exception: " << e.what() << std::endl;
        return false;
    }
    catch(std::exception& e)
    {
        std::cerr << "exception: " << e.what() << std::endl;
        return false;
    }
}

namespace {
// JIT setup shared by execute() / executeRdb(): create LLJIT, compile the
// .ref pinned to the JIT's data layout, expose process symbols, register
// the host `debug(int64)` callback, add the IR module, and collect the
// requirement function names sorted by source position.
struct JitWithSpecs
{
    std::unique_ptr<llvm::orc::LLJIT>       jit;
    std::vector<std::string>                funcNames;
    std::unique_ptr<Antlr2AST>              astOwner;
    ::Module*                               astModule = nullptr;
};

JitWithSpecs    buildJitFromRef(std::istream& refStream, std::string const& refName,
                                std::vector<std::string> const& includePaths)
{
    JitWithSpecs    out;

    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    auto JITOrErr = llvm::orc::LLJITBuilder().create();
    if (!JITOrErr)
        throw std::runtime_error("Failed to create LLJIT");
    out.jit = std::move(*JITOrErr);

    {
        auto GenOrErr = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
            out.jit->getDataLayout().getGlobalPrefix());
        if (!GenOrErr)
            throw std::runtime_error("Failed to create dynamic library search generator");
        out.jit->getMainJITDylib().addGenerator(std::move(*GenOrErr));

        llvm::orc::MangleAndInterner    Mangle(out.jit->getExecutionSession(),
                                               out.jit->getDataLayout());
        llvm::orc::SymbolMap            symMap;
        symMap[Mangle("debug")] = {
            llvm::orc::ExecutorAddr::fromPtr(&debugCallback),
            llvm::JITSymbolFlags::Exported | llvm::JITSymbolFlags::Callable,
        };
        if (auto Err = out.jit->getMainJITDylib().define(
                llvm::orc::absoluteSymbols(std::move(symMap))))
            throw std::runtime_error("Failed to define debug symbol");
    }

    auto    built = Referee::compile(refStream, refName, &out.jit->getDataLayout(), includePaths);
    out.astOwner  = std::move(built.astOwner);
    out.astModule = built.ast;

    // Requirement functions are named "[file:]row:col .. row:col". Report them
    // grouped by file and then in source order, so a program assembled from
    // imports still reads top-to-bottom per file.
    struct FuncEntry { std::string file; int row, col; std::string name; };
    std::vector<FuncEntry>  entries;
    for (auto& F : *built.mod) {
        if (F.isDeclaration()) continue;
        auto name = F.getName().str();
        if (name == "__prepare__") continue;

        auto        head = name.substr(0, name.find(" .. "));   // "[file:]row:col"
        std::string file;
        int         row = 0, col = 0;

        auto        colonB = head.rfind(':');
        if (colonB != std::string::npos)
        {
            auto    colonA = head.rfind(':', colonB - 1);
            auto    rowTxt = colonA == std::string::npos
                           ? head.substr(0, colonB)
                           : head.substr(colonA + 1, colonB - colonA - 1);
            if (colonA != std::string::npos)
                file = head.substr(0, colonA);

            std::sscanf(rowTxt.c_str(), "%d", &row);
            std::sscanf(head.c_str() + colonB + 1, "%d", &col);
        }
        entries.push_back({std::move(file), row, col, std::move(name)});
    }
    std::sort(entries.begin(), entries.end(), [](FuncEntry const& a, FuncEntry const& b) {
        if (a.file != b.file) return a.file < b.file;
        return a.row != b.row ? a.row < b.row : a.col < b.col;
    });
    for (auto& e : entries)
        out.funcNames.push_back(std::move(e.name));

    if (auto Err = out.jit->addIRModule(
            llvm::orc::ThreadSafeModule(std::move(built.mod), std::move(built.ctx))))
        throw std::runtime_error("Failed to add IR module to JIT");

    return out;
}

bool    runAllSpecs(llvm::orc::LLJIT& jit,
                    std::vector<std::string> const& funcNames,
                    void* frst, void* last, void* conf,
                    std::ostream& os)
{
    using SpecFn = bool(*)(void*, void*, void*);
    bool allPass = true;
    for (auto const& name : funcNames)
    {
        if (name == "debug") continue;

        auto symOrErr = jit.lookup(name);
        if (!symOrErr) {
            llvm::consumeError(symOrErr.takeError());
            os << std::left << std::setw(40) << name << " ERROR\n";
            continue;
        }

        auto fn     = symOrErr->toPtr<SpecFn>();
        bool result = fn(frst, last, conf);
        allPass    &= result;

        os << std::left << std::setw(40) << name
           << (result ? " PASS" : " FAIL") << "\n";
    }
    return allPass;
}

// Structurally compare two AST types. Used to verify a .rdb's embedded
// schema is layout-compatible with the .ref the user is executing against.
bool    typesEqual(Type* a, Type* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    if (typeid(*a) != typeid(*b)) return false;

    if (auto* ea = dynamic_cast<TypeEnum*>(a))
    {
        auto* eb = dynamic_cast<TypeEnum*>(b);
        return ea->items == eb->items;
    }
    if (auto* sa = dynamic_cast<TypeStruct*>(a))
    {
        auto* sb = dynamic_cast<TypeStruct*>(b);
        if (sa->members.size() != sb->members.size()) return false;
        for (size_t i = 0; i < sa->members.size(); i++)
        {
            if (sa->members[i].name != sb->members[i].name) return false;
            if (!typesEqual(sa->members[i].data, sb->members[i].data)) return false;
        }
        return true;
    }
    if (auto* aa = dynamic_cast<TypeArray*>(a))
    {
        auto* ab = dynamic_cast<TypeArray*>(b);
        return aa->count == ab->count && typesEqual(aa->type, ab->type);
    }
    return true;        // primitives — class identity already matched
}

} // namespace

namespace {

// JIT the .ref, schema-check the Reader's embedded schema against the AST,
// and dispatch every requirement against the Reader's already-fixed-up
// state buffer. The single backend used by both `Referee::execute` (after
// ingesting CSV/YAML into an in-memory `.rdb`) and `Referee::executeRdb`.
bool    runAgainstRdb(std::istream&            refStream,
                      std::string const&       refName,
                      referee::db::Reader&     rdb,
                      std::ostream&            os,
                      std::vector<std::string> const& includePaths)
{
    auto    js          = buildJitFromRef(refStream, refName, includePaths);
    auto*   astModule   = js.astModule;

    auto    checkList   = [](std::vector<std::string> const& wanted,
                             std::vector<referee::db::PropDecl> const& got,
                             auto&& typeOf, char const* kind)
    {
        if (wanted.size() != got.size())
            throw std::runtime_error(fmt::format(
                "rdb: {} count mismatch — .ref declares {}, .rdb has {}",
                kind, wanted.size(), got.size()));
        for (size_t i = 0; i < wanted.size(); i++)
        {
            if (wanted[i] != got[i].name)
                throw std::runtime_error(fmt::format(
                    "rdb: {} #{} name mismatch — .ref '{}', .rdb '{}'",
                    kind, i, wanted[i], got[i].name));
            if (!typesEqual(typeOf(wanted[i]), got[i].type))
                throw std::runtime_error(fmt::format(
                    "rdb: {} '{}' type mismatch between .ref and .rdb",
                    kind, wanted[i]));
        }
    };
    std::vector<std::string> csvPropNames;
    std::map<std::string, std::size_t> csvPropIndices;
    for (auto const& n : astModule->getPropNames())
    {
        if (!astModule->isExprData(n))
        {
            csvPropIndices[n] = csvPropNames.size();
            csvPropNames.push_back(n);
        }
    }
    checkList(csvPropNames, rdb.props(),
              [&](std::string const& n) { return astModule->getProp(n); }, "data");
    checkList(astModule->getConfNames(), rdb.confs(),
              [&](std::string const& n) { return astModule->getConf(n); }, "conf");

    std::size_t numStates = rdb.numStates();
    std::size_t totalProps = astModule->getPropNames().size();
    std::size_t stateStride = sizeof(int64_t) + totalProps * sizeof(void*);
    std::vector<std::uint8_t> runStates(numStates * stateStride, 0);

    // Backing storage for computed (`data x = expr`) props: one byte per state
    // per prop, which __prepare__ fills before any requirement runs.
    //
    // Each computed prop gets its own buffer rather than sharing storage
    // between props whose live ranges look disjoint. Sharing is not safe here:
    // a temporal prop reads its dependencies at *other* states, so a prop's
    // values must survive for the whole trace, not just until the next
    // declaration that mentions it. At one byte per state per prop the memory
    // saved would not have been worth the risk in any case.
    std::map<std::string, std::vector<std::uint8_t>> computedBuffers;
    for (auto const& name : astModule->getPropNames())
    {
        if (astModule->isExprData(name))
            computedBuffers[name].assign(numStates, 0);
    }

    for (std::size_t si = 0; si < numStates; si++)
    {
        uint8_t* statePtr = runStates.data() + si * stateStride;
        int64_t t = rdb.time(si);
        std::memcpy(statePtr, &t, sizeof(t));

        for (std::size_t pi = 0; pi < totalProps; pi++)
        {
            auto const& name = astModule->getPropNames()[pi];
            void* valPtr = astModule->isExprData(name)
                         ? static_cast<void*>(computedBuffers[name].data() + si)
                         : const_cast<void*>(rdb.propBlob(si, csvPropIndices[name]));
            std::memcpy(statePtr + sizeof(int64_t) + pi * sizeof(void*), &valPtr, sizeof(valPtr));
        }
    }

    auto prepSymOrErr = js.jit->lookup("__prepare__");
    if (!prepSymOrErr)
        throw std::runtime_error("JIT: failed to locate __prepare__ function");

    using PrepFn = void(*)(void*, void*, void*);
    auto prepFn = (*prepSymOrErr).toPtr<PrepFn>();
    prepFn(runStates.data(), runStates.data() + (numStates - 1) * stateStride, rdb.confPtr());

    return runAllSpecs(*js.jit, js.funcNames,
                       runStates.data(), runStates.data() + (numStates - 1) * stateStride, rdb.confPtr(),
                       os);
}

} // namespace

bool    Referee::execute(std::istream& refStream, std::string refName,
                         std::istream& csvStream, std::string csvName,
                         std::istream* confStream,
                         std::string   confName,
                         std::ostream& os,
                         std::vector<std::string> const& includePaths)
{
    // Funnel CSV/YAML execution through the same RDB pipeline that the
    // `.rdb` path uses: ingest the inputs into an in-memory `.rdb`, then
    // execute the requirements against the resulting Reader.
    //
    // The `.ref` stream is consumed twice — once for schema-only parsing
    // inside `referee::db::ingest`, and once for full lowering inside
    // `runAgainstRdb`. We snapshot it up-front so callers can pass any
    // `std::istream` (including non-seekable ones).
    std::string refSrc{std::istreambuf_iterator<char>(refStream),
                       std::istreambuf_iterator<char>()};

    std::stringstream   rdbBuf(std::ios::in | std::ios::out | std::ios::binary);
    {
        std::istringstream  refForIngest(refSrc);
        referee::db::ingest(refForIngest, refName,
                            csvStream,    csvName,
                            confStream,   confName,
                            rdbBuf,       includePaths);
    }

    auto                rdbStr = std::move(rdbBuf).str();
    std::vector<uint8_t> bytes(rdbStr.begin(), rdbStr.end());
    referee::db::Reader rdb(std::move(bytes), csvName);

    std::istringstream  refForExec(refSrc);
    return runAgainstRdb(refForExec, refName, rdb, os, includePaths);
}

bool    Referee::executeRdb(std::istream& refStream, std::string refName,
                            std::string const& rdbPath,
                            std::ostream& os,
                            std::vector<std::string> const& includePaths)
{
    referee::db::Reader     rdb(rdbPath);
    return runAgainstRdb(refStream, refName, rdb, os, includePaths);
}
