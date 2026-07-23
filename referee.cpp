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
#include "core/factory.hpp"
#include "core/json.hpp"

#include <fmt/format.h>

#include <atomic>
#include <filesystem>
#include <cstring>
#include <set>
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
#include <dlfcn.h>
#include <filesystem>
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
                                     std::vector<std::string> const& includePaths,
                                     Sizes const& sizes)
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

    // Without this the parse reports its complaints to stderr and hands back a
    // partial tree, so a malformed .ref compiled to a module and exited 0.
    ParseErrors                 errors;
    errors.attach(lexer, parser);

    // Antlr2AST resolves its Module via Factory<Module>::create(name), which
    // memoises by `name`. If compile() is called twice with the same name
    // (e.g. tests that compile the same .ref multiple times), the second call
    // re-adds the same data/conf decls to the cached Module and throws.
    // Tagging the Antlr2AST name with a process-unique counter keeps every
    // compile self-contained.
    static std::atomic<unsigned>    s_uniq{0};
    auto    uniqName    = name + "#compile:" + std::to_string(s_uniq.fetch_add(1));

    out.astOwner    = std::make_unique<Antlr2AST>(uniqName, name, includePaths, sizes);

    //  Every node built from here on belongs to this compilation's arena, so
    //  two specifications compiled in one process cannot share one -- and the
    //  nodes are freed when the Antlr2AST is, rather than living forever.
    Arena::Scope    arenaScope(out.astOwner->arena);
    auto*   tree    = parser.program();
    if (errors.any())
        throw std::runtime_error(errors.summary(name));
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
                                         std::vector<std::string> const& includePaths,
                                         Sizes const& sizes,
                                         bool allowUnsized)
{
    Schema  out;
    antlr4::ANTLRInputStream    input(is);
    referee::refereeLexer       lexer(&input);
    antlr4::CommonTokenStream   tokens(&lexer);
    referee::refereeParser      parser(&tokens);

    // Without this the parse reports its complaints to stderr and hands back a
    // partial tree, so a malformed .ref compiled to a module and exited 0.
    ParseErrors                 errors;
    errors.attach(lexer, parser);

    // Antlr2AST goes through Factory<Module>::create(name), which caches by
    // the exact name string. If parseSchema is called for the same path that
    // Referee::compile was (or will be) invoked on, the cached Module would
    // get its data/conf decls added a second time and throw. Tag the schema
    // parse with a per-call counter so it gets its own Module.
    static std::atomic<unsigned>    s_uniq{0};
    auto    uniqName    = name + "#schema:" + std::to_string(s_uniq.fetch_add(1));

    out.astOwner    = std::make_unique<Antlr2AST>(uniqName, name, includePaths, sizes, allowUnsized);

    //  Every node built from here on belongs to this compilation's arena, so
    //  two specifications compiled in one process cannot share one -- and the
    //  nodes are freed when the Antlr2AST is, rather than living forever.
    Arena::Scope    arenaScope(out.astOwner->arena);
    auto*   tree    = parser.program();
    if (errors.any())
        throw std::runtime_error(errors.summary(name));
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
//  The host side of `std::string`. Linked into referee and registered with
//  the JIT, so a specification using them needs no .so and no -L -- the same
//  arrangement as `debug`.
//
//  Every one returns a number or a boolean. None returns a string: with no
//  allocator and no ownership model, a function building a new string would
//  have nobody to free it.
extern "C" {

std::int64_t    __ref_str_len(char const* s)
{
    return s ? static_cast<std::int64_t>(std::strlen(s)) : 0;
}

//  Out of range yields -1 rather than reading past the end. There is no
//  bounds checking elsewhere in the language, but here the alternative is a
//  segfault in the checker rather than a wrong answer about the trace.
std::int64_t    __ref_str_at(char const* s, std::int64_t i)
{
    if (s == nullptr || i < 0 || static_cast<std::size_t>(i) >= std::strlen(s))
        return -1;

    return static_cast<unsigned char>(s[i]);
}

std::int64_t    __ref_str_cmp(char const* a, char const* b)
{
    return std::strcmp(a ? a : "", b ? b : "");
}

bool            __ref_str_starts(char const* s, char const* p)
{
    if (s == nullptr || p == nullptr) return false;
    auto n = std::strlen(p);
    return std::strlen(s) >= n && std::strncmp(s, p, n) == 0;
}

bool            __ref_str_ends(char const* s, char const* p)
{
    if (s == nullptr || p == nullptr) return false;
    auto ls = std::strlen(s), lp = std::strlen(p);
    return ls >= lp && std::strcmp(s + ls - lp, p) == 0;
}

//  -1 when absent, so a caller can test for it without a second call.
std::int64_t    __ref_str_find(char const* s, char const* p)
{
    if (s == nullptr || p == nullptr) return -1;
    auto at = std::strstr(s, p);
    return at ? static_cast<std::int64_t>(at - s) : -1;
}

} // extern "C"

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

//  Bind every `func` the specification declared to a symbol in one of the
//  objects on the -L path. The symbol carries a `referee_` prefix, so only
//  declared entry points are ever looked at -- two plugins that each happen to
//  have a private helper called `crc8` do not collide, and a func named `read`
//  cannot resolve to read(2).
static void     bindExternalFunctions(llvm::orc::LLJIT&               jit,
                                      ::Module&                       astModule,
                                      std::vector<std::string> const& libraryPaths)
{
    auto const& funcNames = astModule.getFuncNames();
    if (funcNames.empty())
        return;             //  nothing declared: the path is not even scanned

    //  Deterministic order. readdir order is not stable across filesystems,
    //  and a verdict must not depend on it.
    std::vector<std::string>    objects;
    for (auto const& entry : libraryPaths)
    {
        std::error_code ec;

        //  A file names one object; a directory contributes every .so in it.
        //  Naming the file is what a build that produces a single object
        //  wants, and saves inventing a directory to hold it.
        if (std::filesystem::is_regular_file(entry, ec))
        {
            objects.push_back(entry);
            continue;
        }

        std::vector<std::string>    here;
        for (auto const& e : std::filesystem::directory_iterator(entry, ec))
            if (e.path().extension() == ".so")
                here.push_back(e.path().string());
        std::sort(here.begin(), here.end());
        objects.insert(objects.end(), here.begin(), here.end());
    }

    if (objects.empty())
        throw std::runtime_error(fmt::format(
            "specification declares {} external function(s) but no .so was found"
            " -- pass -L with the object, or a directory containing it",
            funcNames.size()));

    std::vector<void*>  handles;
    for (auto const& obj : objects)
    {
        auto*   h = dlopen(obj.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (h == nullptr)
            throw std::runtime_error(fmt::format("cannot load '{}': {}", obj, dlerror()));
        handles.push_back(h);
    }

    //  A whole-state function reads the row through accessors generated into
    //  its header, so the offsets are compiled into the object. Those offsets
    //  are only right for the signals this specification declares, in this
    //  order, with these types -- and the row layout is an implementation
    //  choice that has already changed once. An object that defines the symbol
    //  states which layout it was built for; a mismatch is caught here rather
    //  than by reading the right offsets of the wrong row.
    {
        auto    wantsState = false;
        for (auto const& name : funcNames)
            for (auto const& decl : astModule.funcsNamed(name))
                wantsState = wantsState || decl.state;

        if (wantsState)
        {
            auto    want = astModule.stateLayoutVersion();

            for (std::size_t h = 0; h < handles.size(); h++)
            {
                dlerror();
                auto*   have = reinterpret_cast<std::uint64_t const*>(
                                    dlsym(handles[h], "referee_state_layout"));

                if (have == nullptr || dlerror() != nullptr)
                    continue;           //  does not use the state: nothing to check

                if (*have != want)
                    throw std::runtime_error(fmt::format(
                        "'{}' was built against a different set of signals\n"
                        "  expected  {}\n"
                        "  found     {}\n"
                        "  the accessors in its header read fixed offsets into a state row,"
                        " and a signal added, removed, reordered or retyped moves them."
                        " Regenerate the header and rebuild.",
                        objects[h], want, *have));
            }
        }
    }

    llvm::orc::MangleAndInterner    Mangle(jit.getExecutionSession(), jit.getDataLayout());
    llvm::orc::SymbolMap            symMap;

    for (auto const& name : funcNames)
    {
        //  `::` is not an identifier character in C, so a namespaced name
        //  mangles to `__`. Three places have to agree on this -- the code
        //  generator, the header emitter and this lookup -- so it lives in
        //  one function rather than being written out three times.
      for (auto const& decl : astModule.funcsNamed(name))
      {
        auto    symbol = astModule.symbolFor(name, decl);

        //  A duplicate is an error, not a race: two implementations may
        //  differ, and nothing in the report would show which one ran.
        void*                       found = nullptr;
        std::vector<std::string>    definedBy;
        for (std::size_t i = 0; i < handles.size(); i++)
        {
            dlerror();
            if (void* p = dlsym(handles[i], symbol.c_str()); p != nullptr && dlerror() == nullptr)
            {
                found = p;
                definedBy.push_back(objects[i]);
            }
        }

        //  Not found. Ask each object what it does implement, so a function
        //  that was never written can be told from one built against an older
        //  version of this specification.
        if (definedBy.empty())
        {
            struct  Entry { char const* name; char const* symbol; };

            for (std::size_t h = 0; h < handles.size(); h++)
            {
                auto*   table = reinterpret_cast<Entry const*>(dlsym(handles[h], "referee_manifest"));
                auto*   count = reinterpret_cast<unsigned const*>(dlsym(handles[h], "referee_manifest_count"));

                if (table == nullptr || count == nullptr)
                    continue;               //  hand-written, no manifest: fall through

                for (unsigned e = 0; e < *count; e++)
                    if (name == table[e].name && symbol != table[e].symbol)
                        throw std::runtime_error(fmt::format(
                            "external function '{}' is implemented against a different layout\n"
                            "  expected  {}\n"
                            "  found     {}   in {}\n"
                            "  a type its signature depends on has changed."
                            " Regenerate the header and rebuild.",
                            name, symbol, table[e].symbol, objects[h]));
            }
        }

        if (definedBy.empty())
            throw std::runtime_error(fmt::format(
                "external function '{}' is declared but '{}' was not found in: {}\n"
                "  the symbol carries a structural hash of the signature, so if the"
                " object was built against an earlier version of this specification"
                " -- a field added to a struct, a member inserted into an enum, a"
                " parameter type changed -- the hash has moved."
                " Regenerate the header and rebuild.",
                name, symbol, fmt::join(objects, ", ")));

        if (definedBy.size() > 1)
            throw std::runtime_error(fmt::format(
                "external function '{}' is defined more than once as '{}': {}",
                name, symbol, fmt::join(definedBy, ", ")));

        symMap[Mangle(symbol)] = {
            llvm::orc::ExecutorAddr::fromPtr(found),
            llvm::JITSymbolFlags::Exported | llvm::JITSymbolFlags::Callable,
        };
      }
    }

    if (auto Err = jit.getMainJITDylib().define(llvm::orc::absoluteSymbols(std::move(symMap))))
        throw std::runtime_error("failed to bind external functions");
}

JitWithSpecs    buildJitFromRef(std::istream& refStream, std::string const& refName,
                                std::vector<std::string> const& includePaths,
                                Referee::Sizes const& sizes,
                                std::vector<std::string> const& libraryPaths = {})
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

        auto    host = [&](char const* name, auto* fn)
        {
            symMap[Mangle(name)] = {
                llvm::orc::ExecutorAddr::fromPtr(fn),
                llvm::JITSymbolFlags::Exported | llvm::JITSymbolFlags::Callable,
            };
        };

        host("__ref_str_len",    &__ref_str_len);
        host("__ref_str_at",     &__ref_str_at);
        host("__ref_str_cmp",    &__ref_str_cmp);
        host("__ref_str_starts", &__ref_str_starts);
        host("__ref_str_ends",   &__ref_str_ends);
        host("__ref_str_find",   &__ref_str_find);
        if (auto Err = out.jit->getMainJITDylib().define(
                llvm::orc::absoluteSymbols(std::move(symMap))))
            throw std::runtime_error("Failed to define debug symbol");
    }

    auto    built = Referee::compile(refStream, refName, &out.jit->getDataLayout(), includePaths, sizes);
    out.astOwner  = std::move(built.astOwner);
    out.astModule = built.ast;

    //  Resolve before a single trace row is read: a missing or duplicated
    //  symbol should never be discoverable halfway through a corpus.
    bindExternalFunctions(*out.jit, *out.astModule, libraryPaths);

    // Requirement functions are named "[file:]row:col .. row:col". Report them
    // grouped by file and then in source order, so a program assembled from
    // imports still reads top-to-bottom per file.
    struct FuncEntry { std::string file; int row, col; std::string name; };
    std::vector<FuncEntry>  entries;
    for (auto& F : *built.mod) {
        if (F.isDeclaration()) continue;
        auto name = F.getName().str();
        if (name == "__prepare__") continue;

        //  `__col__<req>` is a companion that evaluates a requirement at a
        //  caller-chosen state -- a four-argument function reached by name
        //  from the explain path, never a requirement to run. Calling it as a
        //  three-argument requirement passes garbage for `curr` and
        //  dereferences it.
        if (name.rfind("__col__", 0) == 0) continue;
        if (name.rfind("__ante__", 0) == 0) continue;
        if (name.rfind("__sub__", 0) == 0) continue;

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
    // Stable, because a named requirement has no position to sort by: its
    // label replaced the position entirely. Named requirements therefore all
    // compare equal here and must keep the order the module emitted them in,
    // which is the order they were written.
    std::stable_sort(entries.begin(), entries.end(), [](FuncEntry const& a, FuncEntry const& b) {
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
                    std::ostream& os,
                    std::ostream* explain = nullptr,
                    std::size_t   stride = 0,
                    std::size_t   firstReal = 0,
                    std::size_t   lastReal = 0,
                    std::set<std::string> const* temporalReqs = nullptr,
                    ::Module* astModule = nullptr)
{
    using SpecFn = bool(*)(void*, void*, void*);
    using ColFn  = bool(*)(void*, void*, void*, void*);
    using SubFn  = std::int64_t(*)(void*, void*, void*, void*);

    //  An index outside its array cannot be answered, and it cannot be turned
    //  into a verdict inside the generated code either: a requirement returns
    //  one boolean and this decides what it means, so returning `false` from
    //  inside a negated requirement would read as a pass. The generated code
    //  raises a flag and answers the read from a zeroed buffer instead; the
    //  requirement runs to completion and fails here, named by the requirement
    //  rather than by a position -- identical index expressions intern to one
    //  AST node, so the node's position names an arbitrary occurrence of it.
    //
    //  The globals are absent from a module that indexes nothing, so every
    //  lookup is optional.
    auto    slot = [&](char const* n) -> void*
    {
        auto    sym = jit.lookup(n);
        if (!sym) { llvm::consumeError(sym.takeError()); return nullptr; }
        return sym->toPtr<void*>();
    };

    auto*   oobFlag = static_cast<std::uint8_t*>(slot("__oob_flag__"));
    auto*   oobIndx = static_cast<std::int64_t*>(slot("__oob_indx__"));
    auto*   oobCnt  = static_cast<std::int64_t*>(slot("__oob_cnt__"));

    auto    faulted = [&](std::string& why)
    {
        if (oobFlag == nullptr || *oobFlag == 0)
            return false;

        why = fmt::format("index {} is outside an array of {}",
                          oobIndx ? *oobIndx : 0,
                          oobCnt  ? *oobCnt  : 0);
        *oobFlag = 0;
        return true;
    };

    //  __prepare__ has already run, so a computed signal that indexed out of
    //  range is reported before any requirement is blamed for it.
    {
        std::string why;
        if (faulted(why))
            os << std::left << std::setw(40) << "<computed signal>" << " FAIL  " << why << "\n";
    }

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

        auto        fn     = symOrErr->toPtr<SpecFn>();
        bool        result = fn(frst, last, conf);
        std::string why;

        if (faulted(why))
            result = false;

        allPass    &= result;

        os << std::left << std::setw(40) << name
           << (result ? " PASS" : " FAIL")
           << (why.empty() ? "" : "  " + why) << "\n";

        //  The verdict, and the requirement's own per-state column beside it.
        //  A companion `__col__` evaluates the same node at each state, so the
        //  column is the same compiler applied to the same AST -- there is no
        //  second evaluator to drift from the verdict, which is what makes a
        //  second evaluation path safe rather than dangerous.
        //
        //  The check that pays for it: the column's value at the first state
        //  is what the requirement returns, so the two must agree. A mismatch
        //  is a bug in referee, reported as one rather than drawn as a picture.
        //  A bare `Expr` requirement has a `__col__`; a Dwyer pattern does not
        //  -- it quantifies over the whole trace and has no per-state value to
        //  draw -- so its record carries the verdict alone, as before.
        if (explain != nullptr)
        {
            std::vector<bool>   column;
            bool                haveColumn = false;

            if (stride != 0)
            {
                if (auto sym = jit.lookup("__col__" + name))
                {
                    auto    col  = sym->toPtr<ColFn>();
                    auto*   base = static_cast<std::uint8_t*>(frst);

                    for (std::size_t si = firstReal; si < lastReal; si++)
                        column.push_back(col(frst, last, base + si * stride, conf));

                    haveColumn = true;

                    if (!column.empty() && column.front() != result)
                        os << std::left << std::setw(40) << name
                           << " INTERNAL  column disagrees with verdict\n";
                }
                else
                    llvm::consumeError(sym.takeError());
            }

            //  Vacuity: a passing requirement that proved nothing. The one
            //  case computable without scope analysis is an implication whose
            //  antecedent never fires -- `G(a => b)` on a trace where `a` is
            //  never true holds no matter what `b` does. The `__ante__`
            //  companion is exactly the antecedent's column, so this is a scan
            //  of it for a single true. A failing requirement is never
            //  vacuous: it found a counterexample, so something fired.
            bool    vacuous = false;
            if (result && stride != 0)
                if (auto sym = jit.lookup("__ante__" + name))
                {
                    auto    ante = sym->toPtr<ColFn>();
                    auto*   base = static_cast<std::uint8_t*>(frst);
                    bool    everFired = false;

                    for (std::size_t si = firstReal; si < lastReal && !everFired; si++)
                        everFired = ante(frst, last, base + si * stride, conf);

                    vacuous = !everFired;
                }
                else
                    llvm::consumeError(sym.takeError());

            json::Writer    w(*explain);
            {
                auto    doc = w.object();
                w.key("kind").value("requirement");
                w.key("where").value(name);
                w.key("verdict").value(result ? "pass" : "fail");
                w.key("vacuous").value(vacuous);
                if (vacuous)
                {
                    w.key("vacuity");
                    auto    v = w.object();
                    w.key("reason").value("antecedent_never_true");
                }

                if (haveColumn)
                {
                    w.key("rows");
                    auto    rows = w.array();

                    //  r0: the requirement's own column.
                    {
                        auto    row  = w.object();
                        w.key("id").value("r0");
                        w.key("label").value(name);
                        w.key("kind").value(
                            temporalReqs && temporalReqs->count(name) ? "temporal" : "state");
                        w.key("type").value("boolean");
                        w.key("values");
                        auto    vals = w.array();
                        for (bool v : column)
                            w.value(v);
                    }

                    //  The operands of the outermost operator, so the picture
                    //  shows which side of a compound requirement gave way and
                    //  when. Their functions and labels come from the code
                    //  generator, the one authority on what it emitted.
                    if (astModule != nullptr)
                    {
                        auto const& subs = astModule->runRowsFor(name);
                        auto*       base = static_cast<std::uint8_t*>(frst);
                        int         ri   = 1;

                        for (auto const& sub : subs)
                        {
                            auto sym = jit.lookup(sub.func);
                            if (!sym) { llvm::consumeError(sym.takeError()); continue; }
                            auto fn = sym->toPtr<SubFn>();

                            auto    row = w.object();
                            w.key("id").value("r" + std::to_string(ri++));
                            w.key("label").value(sub.label);
                            w.key("kind").value(sub.temporal ? "temporal" : "state");
                            w.key("type").value(sub.type);
                            w.key("values");
                            auto    vals = w.array();

                            for (std::size_t si = firstReal; si < lastReal; si++)
                            {
                                std::int64_t bits = fn(frst, last, base + si * stride, conf);
                                if (sub.type == "boolean")   w.value(bits != 0);
                                else if (sub.type == "number")
                                {
                                    double d;
                                    std::memcpy(&d, &bits, sizeof(d));
                                    w.value(d);
                                }
                                else                         w.value(bits);
                            }
                        }
                    }
                }
            }
            w.line();
        }
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
// The extents an already-packed trace carries, so a specification that leaves
// them out can be compiled against it. The .rdb schema stores concrete
// TypeArray counts, so this is a read rather than an inference.
//  Recover the same path-keyed extent table from an already-built .rdb
//  schema that inferSizes() recovers from CSV column names, so a
//  specification resolves identically against either. Arrays nested inside
//  structs are reached by extending the path, which is why this recurses
//  rather than walking only the top-level array spine.
static void     collectSizes(std::string const& path, Type* type, Referee::Sizes& out)
{
    std::vector<unsigned>   dims;
    auto*                   t = type;

    for(;;)
    {
        auto* array = dynamic_cast<TypeArray*>(t);
        if (array == nullptr) break;
        dims.push_back(array->count);
        t = array->type;
    }

    if (!dims.empty())
        out[path] = dims;

    if (auto* strct = dynamic_cast<TypeStruct*>(t))
    {
        for (auto const& m : strct->members)
            collectSizes(path + "." + m.name, m.data, out);
    }
}

Referee::Sizes  sizesFromSchema(std::vector<referee::db::PropDecl> const& props)
{
    Referee::Sizes  out;

    for (auto const& prop : props)
        collectSizes(prop.name, prop.type, out);

    return out;
}

// Run an already-compiled specification against one trace. Split out from
// runAgainstRdb so a batch pays for the compile once: it is by far the larger
// cost (~700ms for a 233-requirement spec, against ~0.12ms per trace row), so
// looping here rather than re-entering buildJitFromRef is the whole point of
// accepting more than one trace per invocation.
//  ── run traces ───────────────────────────────────────────────────────────
//
//  The header and signal lines of docs/run-trace-format.md. Both recorded and
//  computed signals are already materialised by the time this runs --
//  __prepare__ fills the computed ones before any requirement is evaluated --
//  so this costs one pass and needs no instrumentation. That is why it is the
//  first part of the feature to exist.
//
//  Requirement lines are not emitted yet; those need per-subexpression
//  columns, which is the larger half.
namespace {

char const*     jsonTypeName(Type* type)
{
    if (type == Factory<TypeBoolean>::create())     return "boolean";
    if (type == Factory<TypeByte>::create())        return "byte";
    if (type == Factory<TypeInteger>::create())     return "integer";
    if (type == Factory<TypeNumber>::create())      return "number";
    if (type == Factory<TypeString>::create())      return "string";
    if (dynamic_cast<TypeEnum*>(type) != nullptr)   return "enum";

    return "integer";
}

//  One value out of a prop blob. Mirrors DataYamlPrinter in rdb/database.cpp,
//  which walks the same layout to a different output.
void    writeValue(json::Writer& w, Type* type, std::uint8_t const* data)
{
    if (data == nullptr)                        { w.null();                                             return; }
    if (type == Factory<TypeBoolean>::create()) { w.value(*reinterpret_cast<bool const*>(data));        return; }
    if (type == Factory<TypeByte>::create())    { w.value(std::int64_t(*data));                         return; }
    if (type == Factory<TypeInteger>::create()) { w.value(*reinterpret_cast<std::int64_t const*>(data));return; }
    if (type == Factory<TypeNumber>::create())  { w.value(*reinterpret_cast<double const*>(data));      return; }

    if (type == Factory<TypeString>::create())
    {
        auto*   str = *reinterpret_cast<char const* const*>(data);
        if (str != nullptr) w.value(str); else w.null();
        return;
    }

    if (auto* e = dynamic_cast<TypeEnum*>(type))
    {
        //  Stored 1-based, 0 meaning no member matched -- so a malformed cell
        //  stays distinguishable here rather than becoming the first member.
        auto    idx = std::size_t(*data);
        if (idx == 0 || idx > e->items.size())  w.null();
        else                                    w.value(e->items[idx - 1]);
        return;
    }

    w.null();       //  a composite is not one row; it would need one per leaf
}

//  Sparse when it pays. A flag constant across a long capture is one entry
//  against one per state; an octet changing every state is cheaper dense,
//  since sparse carries an index alongside every value. Measured, not
//  configured -- and the comparison is over the stored bytes, so it agrees
//  with what gets written by construction.
bool    changedAt(Type* type, std::uint8_t const* a, std::uint8_t const* b)
{
    if (a == nullptr || b == nullptr)   return a != b;

    return std::memcmp(a, b, type->size()) != 0;
}

} // namespace

bool    runOneTrace(JitWithSpecs&            js,
                    referee::db::Reader&     rdb,
                    std::ostream&            os,
                    std::ostream*            explain    = nullptr,
                    std::string const&       refName    = {},
                    std::string const&       tracePath  = {})
{
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

    // Backing storage for computed (`data x = expr`) props, which __prepare__
    // fills before any requirement runs. The stride is the prop's own width:
    // it was one byte here originally, which is right for a boolean and wrong
    // for everything else -- __prepare__ stores through a pointer typed by the
    // expression, so an integer wrote eight bytes into a one-byte slot and
    // trampled the seven states after it. Filling is prop-major, so every
    // state but the last ended up holding its own low byte under its
    // successors'.
    //
    // Each computed prop gets its own buffer rather than sharing storage
    // between props whose live ranges look disjoint. Sharing is not safe here:
    // a temporal prop reads its dependencies at *other* states, so a prop's
    // values must survive for the whole trace, not just until the next
    // declaration that mentions it.
    std::map<std::string, std::size_t>               computedStrides;
    std::map<std::string, std::vector<std::uint8_t>> computedBuffers;
    for (auto const& name : astModule->getPropNames())
    {
        if (astModule->isExprData(name))
        {
            auto    width           = astModule->getProp(name)->size();
            computedStrides[name]   = width;
            computedBuffers[name].assign(numStates * width, 0);
        }
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
                         ? static_cast<void*>(computedBuffers[name].data() + si * computedStrides[name])
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

    //  Every signal is materialised now, recorded and computed alike.
    if (explain != nullptr)
    {
        json::Writer    w(*explain);

        //  Sentinels bracket the real states -- ingest adds one at each end so
        //  a temporal walk always has somewhere to stop. They carry no
        //  recorded data and no timestamp anyone chose, so a run trace is over
        //  the states between them. Emitting them would put two states in
        //  every picture that were never in the capture.
        auto const      frst = std::size_t(1);
        auto const      lastX = numStates > 1 ? numStates - 1 : std::size_t(1);
        auto const      count = lastX > frst ? lastX - frst : std::size_t(0);

        auto            propAt = [&](std::size_t si, std::size_t pi) -> std::uint8_t const*
        {
            return reinterpret_cast<std::uint8_t const*>(
                       *reinterpret_cast<void* const*>(
                            runStates.data() + si * stateStride
                            + sizeof(std::int64_t) + pi * sizeof(void*)));
        };

        {
            auto    doc = w.object();
            w.key("kind").value("header");
            w.key("version").value(1);
            w.key("specification"); { auto o = w.object(); w.key("path").value(refName); }
            w.key("trace");
            {
                auto    o = w.object();
                w.key("path").value(tracePath);
                w.key("states").value(std::int64_t(count));
            }
            w.key("states");
            {
                auto    o = w.object();
                w.key("time");
                {
                    auto    arr = w.array();
                    for (std::size_t si = frst; si < lastX; si++)
                        w.value(std::int64_t(rdb.time(si)));
                }
            }
        }
        w.line();

        auto const& names = astModule->getPropNames();

        for (std::size_t pi = 0; pi < names.size(); pi++)
        {
            auto const& name = names[pi];
            auto*       type = astModule->getProp(name);

            //  Composites would need one row per leaf, which the format
            //  supports and this does not do yet. Skipping is better than
            //  emitting a row of nulls that looks like missing data.
            if (dynamic_cast<TypePrimitive*>(type) == nullptr
             && dynamic_cast<TypeEnum*>(type) == nullptr)
                continue;

            //  Where the value changes, decided over the stored bytes so the
            //  encoding choice and the output agree by construction.
            std::vector<std::size_t>    changes;
            for (std::size_t si = frst; si < lastX; si++)
                if (si == frst || changedAt(type, propAt(si, pi), propAt(si - 1, pi)))
                    changes.push_back(si - frst);

            auto    sparse = count > 8 && changes.size() * 3 < count;

            {
            auto    doc = w.object();
            w.key("kind").value("signal");
            w.key("id").value("p" + std::to_string(pi));
            w.key("name").value(name);
            w.key("type").value(jsonTypeName(type));
            if (astModule->isExprData(name))
                w.key("computed").value(true);
            w.key("encoding").value(sparse ? "sparse" : "dense");

            if (sparse)
            {
                w.key("at");
                {
                    auto    arr = w.array();
                    for (auto si : changes)
                        w.value(std::int64_t(si));
                }
            }

            w.key("values");
            {
                auto    arr = w.array();
                if (sparse)
                    for (auto si : changes)
                        writeValue(w, type, propAt(si + frst, pi));
                else
                    for (std::size_t si = frst; si < lastX; si++)
                        writeValue(w, type, propAt(si, pi));
            }

            }

            //  One record per line: what makes the file streamable, and
            //  readable a line at a time.
            w.line();
        }
    }

    //  Which requirement columns are a claim about the future rather than a
    //  fact about the instant. `G(p)` false at a state is a judgement about
    //  the suffix from there; `p == 1` false is a fact about that state. Drawn
    //  the same they read the same, and one misleads -- so the viewer is told
    //  which it is, from the AST that already knows.
    std::set<std::string>   temporalReqs;
    {
        auto&   exprs = js.astModule->getExprs();
        for (std::size_t ei = 0; ei < exprs.size(); ei++)
            if (exprs[ei]->is_temporal())
            {
                auto const& named = js.astModule->getExprName(ei);
                temporalReqs.insert(named.empty() ? exprs[ei]->where().text() : named);
            }
    }

    return runAllSpecs(*js.jit, js.funcNames,
                       runStates.data(), runStates.data() + (numStates - 1) * stateStride, rdb.confPtr(),
                       os, explain,
                       stateStride, std::size_t(1),
                       numStates > 1 ? numStates - 1 : std::size_t(1),
                       &temporalReqs, js.astModule);
}

// Compile, then run against a single trace. The original single-trace entry
// point, kept as the shape most callers want.
bool    runAgainstRdb(std::istream&            refStream,
                      std::string const&       refName,
                      referee::db::Reader&     rdb,
                      std::ostream&            os,
                      std::vector<std::string> const& includePaths)
{
    //  Any array the specification left unsized takes its extent from the
    //  trace, which is why the trace is opened before this is called.
    auto    js  = buildJitFromRef(refStream, refName, includePaths,
                                  sizesFromSchema(rdb.props()));
    return runOneTrace(js, rdb, os);
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

namespace {

// Open a trace by extension: `.rdb` is already the execute-ready layout, and
// everything else is packed into an in-memory one first. Same dispatch the CLI
// did inline, moved here so every caller agrees on it.
std::unique_ptr<referee::db::Reader>    openTrace(std::string const&  refSrc,
                                                 std::string const&  refName,
                                                 std::string const&  tracePath,
                                                 std::string const&  confPath,
                                                 std::vector<std::string> const& includePaths)
{
    auto    isRdb = tracePath.size() >= 4
                 && tracePath.compare(tracePath.size() - 4, 4, ".rdb") == 0;

    if (isRdb)
        return std::make_unique<referee::db::Reader>(tracePath);

    std::ifstream   dataStream(tracePath, std::ios_base::in);
    if (!dataStream)
        throw std::runtime_error(fmt::format("cannot open trace '{}'", tracePath));

    std::ifstream   confStream;
    if (!confPath.empty())
    {
        confStream.open(confPath, std::ios_base::in);
        if (!confStream)
            throw std::runtime_error(fmt::format("cannot open conf '{}'", confPath));
    }

    std::stringstream   rdbBuf(std::ios::in | std::ios::out | std::ios::binary);
    {
        std::istringstream  refForIngest(refSrc);
        referee::db::ingest(refForIngest, refName,
                            dataStream,   tracePath,
                            confPath.empty() ? nullptr : &confStream, confPath,
                            rdbBuf,       includePaths);
    }

    auto                    str = std::move(rdbBuf).str();
    std::vector<uint8_t>    bytes(str.begin(), str.end());
    return std::make_unique<referee::db::Reader>(std::move(bytes), tracePath);
}

} // namespace

std::vector<Referee::Trace>     Referee::readSuite(std::string const& manifestPath)
{
    std::ifstream   in(manifestPath);
    if (!in)
        throw std::runtime_error(fmt::format("cannot open suite '{}'", manifestPath));

    auto            dir = std::filesystem::path(manifestPath).parent_path();
    std::vector<Trace>  traces;
    std::string     line;
    unsigned        lineNo = 0;

    auto    trim = [](std::string s) {
        auto b = s.find_first_not_of(" \t");
        auto e = s.find_last_not_of(" \t");
        return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
    };

    while (std::getline(in, line))
    {
        lineNo++;
        auto    hash = line.find('#');
        if (hash != std::string::npos)
            line = line.substr(0, hash);
        line = trim(line);
        if (line.empty())
            continue;

        std::istringstream  fields(line);
        std::string         path, verb;
        if (!(fields >> path >> verb))
            throw std::runtime_error(fmt::format(
                "{}:{}: expected '<trace> passes|fails [requirements…]'",
                manifestPath, lineNo));

        Trace   trace;
        //  Relative to the manifest, so a suite is movable as a unit.
        trace.path = std::filesystem::path(path).is_absolute()
                   ? path
                   : (dir.empty() ? std::filesystem::path(path) : dir / path).string();

        if      (verb == "passes")  trace.expectFailure = false;
        else if (verb == "fails")   trace.expectFailure = true;
        else
            throw std::runtime_error(fmt::format(
                "{}:{}: expected 'passes' or 'fails', got '{}'",
                manifestPath, lineNo, verb));

        std::string rest;
        std::getline(fields, rest);
        for (auto& c : rest)
            if (c == ',') c = ' ';
        std::istringstream  names(rest);
        std::string         name;
        while (names >> name)
            trace.violates.push_back(name);

        if (!trace.violates.empty() && !trace.expectFailure)
            throw std::runtime_error(fmt::format(
                "{}:{}: '{}' passes, so it cannot also be required to violate anything",
                manifestPath, lineNo, path));

        traces.push_back(std::move(trace));
    }

    if (traces.empty())
        throw std::runtime_error(fmt::format("suite '{}' lists no traces", manifestPath));

    return traces;
}

//  ── C header generation ──────────────────────────────────────────────────
//
//  Emitted from the same type objects the code generator and the loader use,
//  so the header cannot drift from the layout. C has no way to check that a
//  linked function matches the signature a specification declared -- a
//  mismatch is undefined behaviour, not a diagnostic -- so generating the
//  header is the only mechanism that makes the two agree.
namespace {

std::string     cTypeName(Type* type, ::Module& mod);


//  The C spelling of a named type. Everything referee emits is prefixed, both
//  to keep out of the user's namespace and because the header lands in
//  someone else's translation unit.
std::string     cNameOf(Type* type, ::Module& mod)
{
    for (auto const& name : mod.getTypeNames())
        if (mod.getType(name) == type)
            return "referee_" + name;

    return {};
}

//  The descriptor a REF array crosses the boundary as. Named after its
//  element type, so one typedef serves every array of that element.
std::string     cSliceName(Type* elem, ::Module& mod);

std::string     cTypeName(Type* type, ::Module& mod)
{
    if (auto* array = dynamic_cast<TypeArray*>(type))
        return cSliceName(array->type, mod);

    if (type == Factory<TypeBoolean>::create()) return "bool";
    if (type == Factory<TypeByte>::create())    return "uint8_t";
    if (type == Factory<TypeInteger>::create()) return "int64_t";
    if (type == Factory<TypeNumber>::create())  return "double";
    if (type == Factory<TypeString>::create())  return "const char *";

    if (auto named = cNameOf(type, mod); !named.empty())
        return named;

    return "/* unsupported */ void";
}

//  Element spelling for a slice typedef's name: `byte` -> referee_slice_byte.
std::string     cSliceName(Type* elem, ::Module& mod)
{
    std::string tag;

    if      (elem == Factory<TypeBoolean>::create()) tag = "bool";
    else if (elem == Factory<TypeByte>::create())    tag = "byte";
    else if (elem == Factory<TypeInteger>::create()) tag = "integer";
    else if (elem == Factory<TypeNumber>::create())  tag = "number";
    else if (elem == Factory<TypeString>::create())  tag = "string";
    else if (auto named = cNameOf(elem, mod); !named.empty())
                                                    tag = named.substr(8);
    else                                            tag = "unsupported";

    return "referee_slice_" + tag;
}

//  A struct member keeps an array's element type: the extent is in the
//  declarator, so no descriptor is involved.
std::string     cMemberType(Type* type, ::Module& mod)
{
    for (auto* t = type; ; )
    {
        auto* array = dynamic_cast<TypeArray*>(t);
        if (array == nullptr) return cTypeName(t, mod);
        t = array->type;
    }
}

//  One parameter, spelled as it crosses the boundary: structs by `const`
//  pointer, everything else by value.
std::string     cParam(Type* type, ::Module& mod, std::size_t i)
{
    auto    name = "arg" + std::to_string(i);

    if (dynamic_cast<TypeStruct*>(type))
        return "const " + cTypeName(type, mod) + " *" + name;

    return cTypeName(type, mod) + " " + name;
}

//  Array extents belong after the declarator in C, so a member is emitted as
//  name + suffix rather than as one string.
std::string     cArraySuffix(Type* type)
{
    std::string out;
    for (auto* t = type; ; )
    {
        auto* array = dynamic_cast<TypeArray*>(t);
        if (array == nullptr) break;
        out += "[" + std::to_string(array->count) + "]";
        t = array->type;
    }
    return out;
}

} // namespace

void    Referee::emitHeader(std::string const& refPath,
                            std::ostream& os,
                            std::vector<std::string> const& includePaths,
                            std::string const& tracePath)
{
    //  The specification is enough. A header describes types and signatures,
    //  and an array parameter carries its extent in the descriptor built at
    //  the call -- so nothing here needs a trace to be correct.
    //
    //  A trace still refines one case: a named type whose member array is
    //  unsized has a genuinely trace-dependent C layout, and passing one makes
    //  that extent concrete rather than leaving it as a marker.
    Sizes   sizes;
    if (!tracePath.empty())
    {
        std::ifstream       src(refPath, std::ios_base::in);
        std::ostringstream  buf;
        buf << src.rdbuf();

        auto    rdb    = openTrace(buf.str(), refPath, tracePath, "", includePaths);
        sizes = sizesFromSchema(rdb->props());
    }

    std::ifstream   is(refPath, std::ios_base::in);
    if (!is)
        throw std::runtime_error("cannot open " + refPath);

    auto    schema  = parseSchema(is, refPath, includePaths, sizes, /*allowUnsized*/ true);
    auto&   mod     = *schema.ast;

    os << "/*\n"
       << " *  Generated by `referee header` from " << refPath << ".\n"
       << " *  Do not edit: regenerate instead. Every entry point and type is\n"
       << " *  prefixed `referee_`, so a `func` named `read` binds to\n"
       << " *  `referee_read` and cannot reach read(2).\n"
       << " */\n"
       << "#ifndef REFEREE_GENERATED_H\n"
       << "#define REFEREE_GENERATED_H\n\n"
       << "#include <stdint.h>\n"
       << "#include <stdbool.h>\n"
       << "#include <stddef.h>\n\n"
       << "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n";

    //  Named types, in declaration order so a struct's members are already
    //  defined by the time it is emitted.
    for (auto const& typeName : mod.getTypeNames())
    {
        auto*   type = mod.getType(typeName);

        if (auto* e = dynamic_cast<TypeEnum*>(type))
        {
            //  One byte, and 1-based: TypeEnum::index() returns the
            //  declaration position plus one, and the loader leaves 0 when a
            //  cell matched no member. A C enum written the obvious way would
            //  start at 0, making every member off by one and colliding the
            //  first member with the "matched nothing" sentinel.
            //
            //  A typedef plus an anonymous enum keeps the one-byte storage and
            //  gives the constants a name in every dialect, without macros --
            //  which would ignore scope and leak into whatever is included
            //  after this header.
            os << "typedef uint8_t referee_" << typeName << ";\n"
               << "enum {\n"
               << "    referee_" << typeName << "_unknown = 0,\n";
            for (std::size_t i = 0; i < e->items.size(); i++)
                os << "    referee_" << typeName << "_" << e->items[i]
                   << " = " << (i + 1) << ",\n";
            os << "};\n\n";
            continue;
        }

        if (auto* st = dynamic_cast<TypeStruct*>(type))
        {
            os << "typedef struct referee_" << typeName << " {\n";
            //  Inside a struct an array is inline storage with a static
            //  extent, so it stays a C array -- a descriptor is only needed
            //  where the extent is not already in the type.
            std::size_t width = 0;
            for (auto const& m : st->members)
                width = std::max(width, cMemberType(m.data, mod).size());

            for (auto const& m : st->members)
                os << "    " << cMemberType(m.data, mod)
                   << std::string(width - cMemberType(m.data, mod).size() + 1, ' ')
                   << m.name << cArraySuffix(m.data) << ";\n";
            os << "} referee_" << typeName << ";\n\n";
            continue;
        }
    }

    //  One descriptor typedef per element type that a signature actually
    //  mentions. `count` is the array's extent -- its capacity, not the
    //  meaningful length -- so a caller with a shorter payload passes its own
    //  length alongside, and the callee can bounds-check against `count`.
    auto    usesState = false;
    for (auto const& funcName : mod.getFuncNames())
        for (auto const& decl : mod.funcsNamed(funcName))
            usesState = usesState || decl.state;

    std::set<std::string>   slices;
    auto                    noteSlice = [&](Type* t)
    {
        if (auto* array = dynamic_cast<TypeArray*>(t))
            slices.insert(cSliceName(array->type, mod) + "|" + cMemberType(array, mod));
    };

    for (auto const& funcName : mod.getFuncNames())
    {
        auto const& decl = mod.getFunc(funcName);
        for (auto* a : decl.args) noteSlice(a);
        noteSlice(decl.ret);
    }

    //  A whole-state accessor returns a signal's own type, so every array
    //  signal needs its descriptor typedef too -- not only the ones a
    //  signature happens to mention.
    if (usesState)
        for (auto const& name : mod.getPropNames())
            noteSlice(mod.getProp(name));

    if (!slices.empty())
    {
        os << "/*\n"
           << " *  Arrays cross the boundary as a descriptor. `count` is the number of\n"
           << " *  elements there are -- the length, not a capacity -- so a callee reads\n"
           << " *  exactly `count` of them and needs no separate length argument. An\n"
           << " *  array written with an extent passes that extent; one written `T[]`\n"
           << " *  passes however many the record held.\n"
           << " */\n";

        for (auto const& entry : slices)
        {
            auto    bar  = entry.find('|');
            auto    name = entry.substr(0, bar);
            auto    elem = entry.substr(bar + 1);

            auto    ptr   = "const " + elem + "*";
            auto    width = std::max(std::string("size_t").size(), ptr.size()) + 2;

            os << "typedef struct " << name << " {\n"
               << "    " << "size_t" << std::string(width - 6, ' ') << "count;\n"
               << "    " << ptr << std::string(width - ptr.size(), ' ') << "data;\n"
               << "} " << name << ";\n\n";
        }
    }

    //  `(__state__)`: the state crosses as an opaque handle read through the
    //  accessors below, never as a struct the callee lays out for itself.
    //
    //  A row is `{ int64_t time; void *prop[N]; }` today. That is an
    //  implementation choice and it has already changed once -- arrays that
    //  carry their own length rewrote what a prop points at -- so a header
    //  that published it would have shipped a contract the loader was free to
    //  break. Accessors move that risk to a place the compiler can see: the
    //  offsets live here, are regenerated with the header, and are checked
    //  against the loader by a layout version at JIT setup.
    if (usesState)
    {
        auto const& propNames = mod.getPropNames();

        os << "/*\n"
           << " *  The state at the point of evaluation -- which moves: inside a\n"
           << " *  temporal operator it is the state the walk has reached, not the one\n"
           << " *  the requirement was evaluated at.\n"
           << " *\n"
           << " *  Opaque on purpose. Read it with the accessors below; do not cast it,\n"
           << " *  index past it, or hold it after the call returns. Neighbouring states\n"
           << " *  are adjacent in memory and nothing stops a walk off the end of this\n"
           << " *  one, which is the reason to prefer a value signature where one will\n"
           << " *  do.\n"
           << " */\n"
           << "typedef struct referee_state referee_state_t;\n\n"
           << "/*  Verified at JIT setup: an object built against a different set of\n"
           << " *  signals, or a different row layout, fails to load rather than\n"
           << " *  reading the wrong offsets.  */\n"
           << "extern const uint64_t referee_state_layout;\n"
           << "#define REFEREE_STATE_LAYOUT " << mod.stateLayoutVersion() << "ull\n\n"
           << "static inline int64_t referee_state___time__(const referee_state_t *s)\n"
           << "{\n"
           << "    return *(const int64_t *)s;\n"
           << "}\n\n";

        for (std::size_t pi = 0; pi < propNames.size(); pi++)
        {
            auto const& name = propNames[pi];
            auto*       type = mod.getProp(name);
            auto        cName = name;
            for (auto& ch : cName) if (ch == ':' || ch == '.') ch = '_';

            //  Every prop is one pointer in the row; what it points at is the
            //  prop's own storage. A primitive is loaded, a descriptor is
            //  loaded, and anything compound stays a pointer -- the same three
            //  cases the code generator makes.
            auto        slot = "((void *const *)((const char *)s + sizeof(int64_t)))["
                             + std::to_string(pi) + "]";

            if (dynamic_cast<TypeArray*>(type) != nullptr
             || dynamic_cast<TypeStruct*>(type) != nullptr)
            {
                auto    ret = cTypeName(type, mod);
                os << "static inline " << ret << " referee_state_" << cName
                   << "(const referee_state_t *s)\n{\n"
                   << "    return *(const " << ret << " *)" << slot << ";\n}\n\n";
                continue;
            }

            auto    ret = cTypeName(type, mod);
            os << "static inline " << ret << " referee_state_" << cName
               << "(const referee_state_t *s)\n{\n"
               << "    return *(const " << ret << " *)" << slot << ";\n}\n\n";
        }
    }

    //  An accessor and a function alias live in one C namespace, and a
    //  specification can put them in the same place: signal `len` generates
    //  `referee_state_len`, and `func state_len` aliases to the same spelling.
    //  The alias is a macro, so it would quietly replace the accessor.
    //
    //  No prefix avoids this -- whatever it is, a function can be named to
    //  produce it -- so it is detected rather than designed around, which is
    //  the same answer two overloads with identical argument types get.
    if (usesState)
    {
        std::set<std::string>   accessors{"referee_state___time__", "referee_state_layout"};

        for (auto const& name : mod.getPropNames())
        {
            auto    cName = name;
            for (auto& ch : cName) if (ch == ':' || ch == '.') ch = '_';
            accessors.insert("referee_state_" + cName);
        }

        for (auto const& funcName : mod.getFuncNames())
        {
            auto    plain = "referee_" + funcName;
            for (auto at = plain.find("::"); at != std::string::npos; at = plain.find("::", at))
                plain.replace(at, 2, "__");

            if (accessors.count(plain))
                throw std::runtime_error(
                    "'" + funcName + "' generates the C name '" + plain + "', which is already"
                    " the whole-state accessor for a signal of that name. One of the two has to"
                    " be renamed: the alias is a macro and would replace the accessor.");
        }
    }

    //  Prototypes, one parameter per line. A signature names types only, so
    //  the parameters are numbered; the point of the header is that the shape
    //  is right, and a reader can rename them when implementing.
    if (!mod.getFuncNames().empty())
        os << "/*\n"
           << " *  A plugin describes itself. `--stub` defines the table below;\n"
           << " *  referee reads it to tell a function that was never implemented\n"
           << " *  from one implemented against an older version of this\n"
           << " *  specification -- which are opposite problems with opposite\n"
           << " *  fixes, and otherwise both surface as \"not found\".\n"
           << " *\n"
           << " *  It is optional: a hand-written implementation without it still\n"
           << " *  works, and simply gets the vaguer diagnostic.\n"
           << " */\n"
           << "typedef struct referee_entry {\n"
           << "    const char* name;       /*  as written in the .ref  */\n"
           << "    const char* symbol;     /*  with its structural hash  */\n"
           << "} referee_entry;\n\n"
           << "extern const referee_entry referee_manifest[];\n"
           << "extern const unsigned      referee_manifest_count;\n\n"
           << "/*\n"
           << " *  Implement these.\n"
           << " *\n"
           << " *  The symbol carries a structural hash of the signature, so a change to\n"
           << " *  any type it reaches -- a field added to a struct, a member inserted\n"
           << " *  into an enum -- changes the symbol and a stale object fails to\n"
           << " *  resolve instead of being called with the wrong layout. Two overloads\n"
           << " *  of one name likewise get different symbols, which is what makes\n"
           << " *  overloading work without C++ linkage.\n"
           << " *\n"
           << " *  Each alias below lets an implementation be written against the plain\n"
           << " *  name and still define the hashed symbol, so the hash need never be\n"
           << " *  typed. Regenerate this header when the specification changes and the\n"
           << " *  aliases follow; nothing in your source has to.\n"
           << " */\n";

    for (auto const& funcName : mod.getFuncNames())
    {
        auto const& overloads = mod.funcsNamed(funcName);

        //  An alias only makes sense for a name with a single signature: one
        //  macro cannot stand for two symbols. An overloaded name is written
        //  out in full, which `--stub` does for you.
        if (overloads.size() != 1)
            continue;

        auto    plain = "referee_" + funcName;
        for (auto at = plain.find("::"); at != std::string::npos; at = plain.find("::", at))
            plain.replace(at, 2, "__");

        os << "#define " << plain << " " << mod.symbolFor(funcName, overloads.front()) << "\n";
    }

    os << "\n";

    for (auto const& funcName : mod.getFuncNames())
      for (auto const& decl : mod.funcsNamed(funcName))
    {
        auto        head = cTypeName(decl.ret, mod) + " " + mod.symbolFor(funcName, decl) + "(";

        os << head;

        if (decl.state)
        {
            os << "const referee_state_t *state);\n\n";
            continue;
        }

        if (decl.args.empty())
        {
            os << "void);\n\n";
            continue;
        }

        for (std::size_t i = 0; i < decl.args.size(); i++)
        {
            if (i) os << ",\n" << std::string(head.size(), ' ');
            os << cParam(decl.args[i], mod, i);
        }
        os << ");\n\n";
    }

    os << "\n#ifdef __cplusplus\n}\n#endif\n"
       << "#endif  /* REFEREE_GENERATED_H */\n";
}

void    Referee::emitStub(std::string const& refPath,
                          std::string const& headerName,
                          std::ostream& os,
                          std::vector<std::string> const& includePaths,
                          std::string const& tracePath)
{
    Sizes   sizes;
    if (!tracePath.empty())
    {
        std::ifstream       src(refPath, std::ios_base::in);
        std::ostringstream  buf;
        buf << src.rdbuf();

        auto    rdb = openTrace(buf.str(), refPath, tracePath, "", includePaths);
        sizes = sizesFromSchema(rdb->props());
    }

    std::ifstream   is(refPath, std::ios_base::in);
    if (!is)
        throw std::runtime_error("cannot open " + refPath);

    auto    schema  = parseSchema(is, refPath, includePaths, sizes, /*allowUnsized*/ true);
    auto&   mod     = *schema.ast;

    os << "/*\n"
       << " *  Skeleton generated by `referee header --stub` from " << refPath << ".\n"
       << " *\n"
       << " *  Fill in the bodies. The signatures come from the same table the\n"
       << " *  header and the symbol lookup use, so copying is safe and\n"
       << " *  transcribing is not -- C cannot diagnose a mismatch.\n"
       << " *\n"
       << " *  Unlike the header, this file is yours once written: regenerating\n"
       << " *  it would discard your work, so it is emitted to be edited.\n"
       << " */\n"
       << "#include \"" << (headerName.empty() ? "spec.h" : headerName) << "\"\n\n";

    if (mod.getFuncNames().empty())
    {
        os << "/*  The specification declares no external functions.  */\n";
        return;
    }

    //  The self-description referee reads back. Emitted here rather than in
    //  the header because it has to be *defined* once, in the object.
    os << "const referee_entry referee_manifest[] = {\n";
    for (auto const& funcName : mod.getFuncNames())
        for (auto const& decl : mod.funcsNamed(funcName))
            os << "    { \"" << funcName << "\", \"" << mod.symbolFor(funcName, decl) << "\" },\n";
    os << "};\n"
       << "const unsigned referee_manifest_count = sizeof(referee_manifest) / sizeof(referee_manifest[0]);\n\n";

    for (auto const& funcName : mod.getFuncNames())
      for (auto const& decl : mod.funcsNamed(funcName))
    {
        auto        head = cTypeName(decl.ret, mod) + " " + mod.symbolFor(funcName, decl) + "(";

        os << head;

        if (decl.args.empty())
            os << "void";

        for (std::size_t i = 0; i < decl.args.size(); i++)
        {
            if (i) os << ",\n" << std::string(head.size(), ' ');
            os << cParam(decl.args[i], mod, i);
        }

        os << ")\n{\n";

        //  Name every parameter in a comment: an unused-parameter warning
        //  under -Wextra is otherwise the first thing a reader hits.
        for (std::size_t i = 0; i < decl.args.size(); i++)
            os << "    (void) arg" << i << ";\n";

        os << "\n    /*  TODO  */\n";

        if (decl.ret == Factory<TypeBoolean>::create())      os << "    return false;\n";
        else if (decl.ret == Factory<TypeNumber>::create())  os << "    return 0.0;\n";
        else if (decl.ret == Factory<TypeString>::create())  os << "    return \"\";\n";
        else if (dynamic_cast<TypeStruct*>(decl.ret) == nullptr) os << "    return 0;\n";

        os << "}\n\n";
    }
}

bool    Referee::executeAll(std::istream& refStream, std::string refName,
                            std::vector<Trace> const& traces,
                            std::string const& confPath,
                            std::ostream& os,
                            Detail        detail,
                            std::vector<std::string> const& includePaths,
                            std::vector<std::string> const& libraryPaths,
                            std::string const&              explainPath)
{
    //  One file per run. With several traces the last one wins, which is the
    //  honest simple behaviour -- a corpus wants a file each, and naming them
    //  is a decision for whoever asks for it.
    std::ofstream                   explainFile;
    if (!explainPath.empty())
    {
        explainFile.open(explainPath, std::ios_base::out | std::ios_base::trunc);
        if (!explainFile)
            throw std::runtime_error("cannot write " + explainPath);
    }
    if (traces.empty())
        throw std::runtime_error("no traces given");

    //  The .ref stream is consumed once per trace by the ingest path, so
    //  snapshot it up front and hand out copies.
    std::string refSrc{std::istreambuf_iterator<char>(refStream),
                       std::istreambuf_iterator<char>()};

    //  The first trace is opened before compiling, because an array the
    //  specification declares `T[]` takes its extent from the trace. Every
    //  later trace is checked against the schema that fixed, so a corpus whose
    //  traces disagree on an extent is reported rather than silently
    //  misread.
    auto    first = openTrace(refSrc, refName, traces.front().path,
                              confPath, includePaths);

    //  Compile once. This is the reason the loop is here rather than in the
    //  caller: it is the dominant cost and it does not depend on the trace.
    JitWithSpecs    js;
    {
        std::istringstream  refForJit(refSrc);
        js = buildJitFromRef(refForJit, refName, includePaths,
                             sizesFromSchema(first->props()), libraryPaths);
    }

        auto    atLeast = [&](Detail want) {
        return static_cast<int>(detail) >= static_cast<int>(want);
    };

    struct Outcome
    {
        std::string                 path;
        bool                        expectFailure;
        bool                        allPass;
        std::string                 detail;
        std::vector<std::string>    missing;    //  named, expected to fail, did not
        bool    ok() const
        {
            return allPass != expectFailure && missing.empty();
        }
    };

    std::vector<Outcome>    outcomes;
    bool                    everyTraceOk = true;

    for (std::size_t ti = 0; ti < traces.size(); ti++)
    {
        auto const& trace = traces[ti];

        //  The first was opened above to fix any unsized extents.
        auto    owned = ti == 0 ? nullptr
                                : openTrace(refSrc, refName, trace.path,
                                            confPath, includePaths);
        auto&   rdb   = ti == 0 ? *first : *owned;

        std::ostringstream  perTrace;
        bool                allPass = runOneTrace(js, rdb, perTrace,
                                                  explainFile.is_open() ? &explainFile : nullptr,
                                                  refName, trace.path);
        auto                report  = perTrace.str();

        //  When the trace names the requirements it is meant to violate, each
        //  has to actually be among the violations. Otherwise the corpus
        //  quietly stops testing what it was written for the moment the trace
        //  starts failing for some other reason.
        std::vector<std::string>    missing;
        for (auto const& want : trace.violates)
        {
            bool    found = false;
            std::istringstream  lines(report);
            std::string         line;
            while (std::getline(lines, line))
            {
                if (line.find("FAIL") == std::string::npos) continue;
                if (line.compare(0, want.size(), want) == 0) { found = true; break; }
            }
            if (!found)
                missing.push_back(want);
        }

        outcomes.push_back({trace.path, trace.expectFailure, allPass,
                            std::move(report), std::move(missing)});
        everyTraceOk = everyTraceOk && outcomes.back().ok();
    }

    //  One trace, no expectation declared, full detail asked for: print what
    //  a single-trace run has always printed. There is no volume problem to
    //  solve and the per-requirement lines are the whole of the useful output.
    bool    lone = traces.size() == 1 && !traces.front().expectFailure;
    if (lone && detail == Detail::Requirements)
    {
        os << outcomes.front().detail;
        return everyTraceOk;
    }

    std::size_t width = 0;
    for (auto const& o : outcomes)
        width = std::max(width, o.path.size());

    unsigned    good = 0, bad = 0, surprises = 0;
    for (auto const& o : outcomes)
    {
        char const* observed = o.allPass ? "PASS" : "FAIL";
        char const* verdict  = o.ok()               ? "ok"
                             : !o.missing.empty()   ? "WRONG REQUIREMENT"
                             : o.expectFailure      ? "UNEXPECTED PASS"
                                                    : "FAILED";

        //  At Summary only misbehaviour is worth a line; the closing tally
        //  covers the rest.
        if (atLeast(Detail::Traces) || !o.ok())
            os << fmt::format("{:<{}}  expected {:<7}  {:<4}  {}\n",
                              o.path, width,
                              o.expectFailure ? "failure" : "success",
                              observed, verdict);

        //  The full table when asked for. Otherwise, only what a reader can
        //  act on: the violated requirements of a trace that was supposed to
        //  pass. An unexpected pass has nothing to point at -- every
        //  requirement held -- so listing them all would be noise.
        bool    wantAll     = atLeast(Detail::Requirements);
        bool    wantFailed  = !o.ok() && !o.allPass;

        for (auto const& m : o.missing)
            os << "    expected to violate " << m << ", but it held\n";

        if (wantAll || wantFailed)
        {
            std::istringstream  lines(o.detail);
            std::string         line;
            while (std::getline(lines, line))
            {
                if (wantAll || line.find("FAIL") != std::string::npos)
                    os << "    " << line << "\n";
            }
        }

        if (o.ok())                     good++;
        else if (!o.missing.empty())    bad++;
        else if (o.expectFailure)       surprises++;
        else                            bad++;
    }

    os << fmt::format("\n{} trace{}: {} ok", traces.size(),
                      traces.size() == 1 ? "" : "s", good);
    if (bad)        os << fmt::format(", {} failed", bad);
    if (surprises)  os << fmt::format(", {} unexpected pass{}",
                                      surprises, surprises == 1 ? "" : "es");
    os << "\n";

    return everyTraceOk;
}

bool    Referee::executeRdb(std::istream& refStream, std::string refName,
                            std::string const& rdbPath,
                            std::ostream& os,
                            std::vector<std::string> const& includePaths)
{
    referee::db::Reader     rdb(rdbPath);
    return runAgainstRdb(refStream, refName, rdb, os, includePaths);
}
