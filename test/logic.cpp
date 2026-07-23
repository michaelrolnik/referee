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

#include "gtest/gtest.h"

#include "antlr4-runtime/antlr4-runtime.h"
#include "refereeParser.h"
#include "refereeLexer.h"
#include "refereeBaseVisitor.h"
#include "referee.hpp"

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
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#if __has_include("llvm/ExecutionEngine/Orc/AbsoluteSymbols.h")
#  include "llvm/ExecutionEngine/Orc/AbsoluteSymbols.h"
#endif
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorSymbolDef.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include <memory>

#include "antlr2ast.hpp"
#include "strings.hpp"
#include "visitors/compile.hpp"


//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
extern "C"
void    debug(int64_t value)
{
    std::cout << value << std::endl;
}
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP

class RefereeJIT
{
private:
    std::unique_ptr<llvm::orc::LLJIT>   J;

public:
    explicit RefereeJIT(std::unique_ptr<llvm::orc::LLJIT> J_)
        : J(std::move(J_))
    {
    }

    static llvm::Expected<std::unique_ptr<RefereeJIT>> Create()
    {
        auto    JOrErr  = llvm::orc::LLJITBuilder().create();
        if (!JOrErr)
            return JOrErr.takeError();

        auto    J       = std::move(*JOrErr);

        auto    GenOrErr = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
            J->getDataLayout().getGlobalPrefix());
        if (!GenOrErr)
            return GenOrErr.takeError();
        J->getMainJITDylib().addGenerator(std::move(*GenOrErr));

        llvm::orc::MangleAndInterner    Mangle(J->getExecutionSession(), J->getDataLayout());
        llvm::orc::SymbolMap            symMap;
        symMap[Mangle("debug")]     = {
            llvm::orc::ExecutorAddr::fromPtr(&debug),
            llvm::JITSymbolFlags::Exported | llvm::JITSymbolFlags::Callable,
        };
        if (auto Err = J->getMainJITDylib().define(
                llvm::orc::absoluteSymbols(std::move(symMap))))
            return std::move(Err);

        return std::make_unique<RefereeJIT>(std::move(J));
    }

    const llvm::DataLayout& getDataLayout() const   { return J->getDataLayout(); }
    llvm::orc::JITDylib&    getMainJITDylib()       { return J->getMainJITDylib(); }
    llvm::Error             addModule(llvm::orc::ThreadSafeModule TSM)
    {
        return J->addIRModule(std::move(TSM));
    }

    llvm::Expected<llvm::orc::ExecutorAddr>     lookup(llvm::StringRef Name)
    {
        return J->lookup(Name);
    }
};

typedef struct state_t {
    uint64_t    time;
    bool*       lttr[26];
} state_t;

typedef struct conf_t {
    int64_t     i;
    bool        b;
    double      n;
    char const* s;
    uint8_t     e;
    int64_t     a[2];
} conf_t;

static llvm::ExitOnError ExitOnErr;

class LogicTest : public ::testing::Test {

protected:
    state_t     state[28];  //  1 + 26 + 1
    conf_t      conf;
    bool        T   = true;
    bool        F   = false;

protected:
    // Compile `filename` via Referee::compile, hand the resulting module to
    // the JIT, and run every non-`debug` requirement against the fixture
    // trace. `expected` is the value each requirement must return — true
    // for pass.ref, false for fail.ref.
    void    runRefFile(std::string const& filename, bool expected)
    {
        std::ifstream   stream(filename, std::ios_base::in);
        ASSERT_TRUE(stream.is_open());

        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();
        auto    TheJIT  = ExitOnErr(RefereeJIT::Create());

        try {
            // Pin the IR layout to the JIT we are about to feed it to.
            auto    built   = Referee::compile(stream, filename,
                                               &TheJIT->getDataLayout());

            // Snapshot function names before transferring ownership of the
            // module to the JIT.
            std::vector<std::string>    names;
            for(auto& F : *built.mod)
                names.push_back(F.getName().str());

            ExitOnErr(TheJIT->addModule(llvm::orc::ThreadSafeModule(
                std::move(built.mod), std::move(built.ctx))));

            for(auto const& name : names)
            {
                if(name == "debug" || name == "__prepare__")
                    continue;
                //  A `__col__<req>` companion is a four-argument column
                //  evaluator, not a requirement -- calling it with three
                //  arguments dereferences a garbage `curr`.
                if(name.rfind("__col__", 0) == 0)
                    continue;
                if(name.rfind("__ante__", 0) == 0)
                    continue;
                if(name.rfind("__sub__", 0) == 0)
                    continue;

                auto    symbol  = ExitOnErr(TheJIT->lookup(name));
                auto    func    = symbol.toPtr<bool (*)(state_t*, state_t*, void*)>();
                auto    result  = func(&state[0], &state[27], &conf);
                std::cout << std::setw(20) << std::left << name
                          << " eval: " << result << std::endl;
                ASSERT_EQ(result, expected);
            }
        }
        catch(Exception& e)
        {
            std::cerr << "exception: " << e.what() << std::endl;
            ASSERT_TRUE(false);
        }
        catch(std::exception& e)
        {
            std::cerr << "exception: " << e.what() << std::endl;
            ASSERT_TRUE(false);
        }
    }

    void SetUp() override
    {
        for(int i = 0 ; i < 26; i++)
        {
            state[i + 1].time   = i * 1000;

            for(int j = 0; j < 26; j++)
            {
                state[i + 1].lttr[j]    = i == j ? &T : &F;
            }
        }
        state[ 0].time  = state[ 1].time - 1;
        state[27].time  = state[26].time + 1;

        conf.i  = 1;
        conf.b  = true;
        conf.n  = 1.1;
        conf.s  = Strings::instance()->getString("hello");
        conf.e  = 1;
        conf.a[0]   = 1;
        conf.a[1]   = 2;
    }
};

TEST_F(LogicTest, Pass)
{
    runRefFile(std::string(REFEREE_TEST_DATA_DIR) + "/pass.ref", true);
}

TEST_F(LogicTest, Fail)
{
    runRefFile(std::string(REFEREE_TEST_DATA_DIR) + "/fail.ref", false);
}
