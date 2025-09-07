/*
 *  MIT License
 *  
 *  Copyright (c) 2022 Michael Rolnik
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
#include "../rdb/database.hpp"

#include "antlr4-runtime.h"
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
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include <memory>

#include "antlr2ast.hpp"
#include "strings.hpp"
#include "visitors/compile.hpp"


//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
extern "C"
void    debug(int64_t value)
{
    printf("debug: %ld\n", value);
}
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP

class RefereeJIT 
{
private:
    std::unique_ptr<llvm::orc::ExecutionSession>    ES;
    llvm::DataLayout                                DL;
    llvm::orc::MangleAndInterner                    Mangle;
    llvm::orc::RTDyldObjectLinkingLayer             ObjectLayer;
    llvm::orc::IRCompileLayer                       CompileLayer;
    llvm::orc::JITDylib&                            MainJD;

public:
    RefereeJIT(
            std::unique_ptr<llvm::orc::ExecutionSession>    ES,
            llvm::orc::JITTargetMachineBuilder              JTMB, 
            llvm::DataLayout                                DL)
        : ES(std::move(ES))
        , DL(std::move(DL))
        , Mangle(*this->ES, this->DL)
        , ObjectLayer(*this->ES,[]() { return std::make_unique<llvm::SectionMemoryManager>(); })
        , CompileLayer(*this->ES, ObjectLayer, std::make_unique<llvm::orc::ConcurrentIRCompiler>(std::move(JTMB)))
        , MainJD(this->ES->createBareJITDylib("<main>")) 
    {

        llvm::ExitOnError()(MainJD.define(
            llvm::orc::absoluteSymbols(llvm::orc::SymbolMap{
                { Mangle("debug"), llvm::JITEvaluatedSymbol::fromPointer(&debug)}})));

        MainJD.addGenerator(cantFail(llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(DL.getGlobalPrefix())));
    }

    ~RefereeJIT() 
    {
        if (auto Err = ES->endSession())
        {
            ES->reportError(std::move(Err));
        }
    }

    static llvm::Expected<std::unique_ptr<RefereeJIT>> Create() 
    {
        auto EPC = llvm::orc::SelfExecutorProcessControl::Create();
        if (!EPC)
            return EPC.takeError();

        auto    ES     = std::make_unique<llvm::orc::ExecutionSession>(std::move(*EPC));
        auto    JTMB   = llvm::orc::JITTargetMachineBuilder(ES->getExecutorProcessControl().getTargetTriple());
        auto    DL     = JTMB.getDefaultDataLayoutForTarget();
        
        if (!DL)
            return DL.takeError();

        return std::make_unique<RefereeJIT>(std::move(ES), std::move(JTMB), std::move(*DL));
    }

    const llvm::DataLayout& getDataLayout() const   { return DL; }
    llvm::orc::JITDylib&    getMainJITDylib()       { return MainJD; }
    llvm::Error             addModule(llvm::orc::ThreadSafeModule TSM, llvm::orc::ResourceTrackerSP RT = nullptr)
    {
        if (!RT)
            RT = MainJD.getDefaultResourceTracker();
        return CompileLayer.add(RT, std::move(TSM));
    }

    llvm::Expected<llvm::JITEvaluatedSymbol>    lookup(llvm::StringRef Name)
    {
        return ES->lookup({&MainJD}, Mangle(Name.str()));
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

class LogicTest : public ::testing::Test {

protected:
    state_t     state[28];  //  1 + 26 + 1
    conf_t      conf;
    bool        T   = true;
    bool        F   = false;

protected:
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
/*
        auto    type    = referee::db::TypeBoolean();

        referee::db::Writer writer;
        writer.open("test.rdb");

        auto    typeID  = writer.declType(&type);

        for(char it = 'a'; it <= 'z'; it++)
        {
            char    name[2];    name[0] = it;
            auto    propID  = writer.declProp(typeID, name);
            auto    time    = (it - 'a') * 100 + 100;
            writer.pushData(propID, 0,          referee::db::DataWriter(&type).boolean(false).build());
            writer.pushData(propID, time,       referee::db::DataWriter(&type).boolean(true).build());
            writer.pushData(propID, time + 100, referee::db::DataWriter(&type).boolean(false).build());
        }
*/
    }
};

static llvm::ExitOnError ExitOnErr;

TEST_F(LogicTest, Pass)
{
    std::string                 filename    = "../test/logic/pass.ref";
    std::ifstream               stream(filename, std::ios_base::in);

    ASSERT_TRUE(stream.is_open());

    std::ostringstream          os;
    Referee::compile(stream, filename, os);
    std::string                 ir  = os.str();
    auto    ref     = llvm::StringRef(ir);
    auto    buff    = llvm::MemoryBuffer::getMemBuffer(ref);

    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    auto    TheJIT      = ExitOnErr(RefereeJIT::Create());

    auto    error       = std::make_unique<llvm::SMDiagnostic>();
    auto    TheContext  = std::make_unique<llvm::LLVMContext>();
    auto    TheModule   = llvm::parseIR(*buff, *error, *TheContext);
    auto    TheBuilder  = std::make_unique<llvm::IRBuilder<>>(*TheContext);   
    auto    TheFPM      = std::make_unique<llvm::legacy::FunctionPassManager>(TheModule.get());
#if 0
    auto    funcType    = llvm::FunctionType::get(TheBuilder->getVoidTy(), {TheBuilder->getInt64Ty()}, false);
    auto    func        = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, "debug", *TheModule);
#endif
        
    TheModule->setDataLayout(TheJIT->getDataLayout());

    try {
        TheFPM->add(llvm::createInstructionCombiningPass());
        TheFPM->add(llvm::createReassociatePass());
        TheFPM->add(llvm::createGVNPass());
        TheFPM->add(llvm::createCFGSimplificationPass());
        TheFPM->add(llvm::createLoopStrengthReducePass());
        TheFPM->add(llvm::createLoopLoadEliminationPass());
        TheFPM->add(llvm::createLoopDataPrefetchPass());
        TheFPM->add(llvm::createLoopSimplifyCFGPass());
        TheFPM->add(llvm::createLoopGuardWideningPass());
        TheFPM->add(llvm::createLoopDistributePass());
        TheFPM->add(llvm::createInstructionCombiningPass());
        TheFPM->add(llvm::createReassociatePass());
        TheFPM->add(llvm::createGVNPass());
        TheFPM->add(llvm::createCFGSimplificationPass());

        TheFPM->doInitialization();

        auto& functions = TheModule->getFunctionList();
        std::vector<std::string>    names;

        for(auto iter = functions.begin(); iter != functions.end(); iter++)
        {
            names.push_back(iter->getName().str());

            TheFPM->run(*iter);
        }

        ExitOnErr(TheJIT->addModule(llvm::orc::ThreadSafeModule(std::move(TheModule), std::move(TheContext))));

        //auto    symbol  = ExitOnErr(TheJIT->lookup("30:3 .. 30:13"));
        //auto    func    = (bool (*)(state_t*, state_t*, void*))(intptr_t)symbol.getAddress();
        //auto    result  = func(&state[0], &state[26], nullptr);
        //std::cout << "result: " << result << std::endl; 
        {
            for(auto name: names)
            {
                auto    symbol  = ExitOnErr(TheJIT->lookup(name));
                auto    func    = (bool (*)(state_t*, state_t*, void*))(intptr_t)symbol.getAddress();
                if(name == "debug")
                    continue;
                auto    result  = func(&state[0], &state[27], &conf);
                std::cout << std::setw(20) << std::left << name << " eval: " << result << std::endl; 
                ASSERT_TRUE(result);
            }
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

TEST_F(LogicTest, Fail)
{
    std::string                 filename    = "../test/logic/fail.ref";
    std::ifstream               stream(filename, std::ios_base::in);

    ASSERT_TRUE(stream.is_open());

    std::ostringstream          os;
    Referee::compile(stream, filename, os);
    std::string                 ir  = os.str();
    auto    ref     = llvm::StringRef(ir);
    auto    buff    = llvm::MemoryBuffer::getMemBuffer(ref);

    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    auto    TheJIT      = ExitOnErr(RefereeJIT::Create());

    auto    error       = std::make_unique<llvm::SMDiagnostic>();
    auto    TheContext  = std::make_unique<llvm::LLVMContext>();
    auto    TheModule   = llvm::parseIR(*buff, *error, *TheContext);
    auto    TheBuilder  = std::make_unique<llvm::IRBuilder<>>(*TheContext);   
    auto    TheFPM      = std::make_unique<llvm::legacy::FunctionPassManager>(TheModule.get());
        
    TheModule->setDataLayout(TheJIT->getDataLayout());

    try {
        TheFPM->add(llvm::createInstructionCombiningPass());
        TheFPM->add(llvm::createReassociatePass());
        TheFPM->add(llvm::createGVNPass());
        TheFPM->add(llvm::createCFGSimplificationPass());
        TheFPM->add(llvm::createLoopStrengthReducePass());
        TheFPM->add(llvm::createLoopLoadEliminationPass());
        TheFPM->add(llvm::createLoopDataPrefetchPass());
        TheFPM->add(llvm::createLoopSimplifyCFGPass());
        TheFPM->add(llvm::createLoopGuardWideningPass());
        TheFPM->add(llvm::createLoopDistributePass());
        TheFPM->add(llvm::createInstructionCombiningPass());
        TheFPM->add(llvm::createReassociatePass());
        TheFPM->add(llvm::createGVNPass());
        TheFPM->add(llvm::createCFGSimplificationPass());

        TheFPM->doInitialization();

        auto& functions = TheModule->getFunctionList();
        std::vector<std::string>    names;

        for(auto iter = functions.begin(); iter != functions.end(); iter++)
        {
            names.push_back(iter->getName().str());

            TheFPM->run(*iter);
        }

        ExitOnErr(TheJIT->addModule(llvm::orc::ThreadSafeModule(std::move(TheModule), std::move(TheContext))));

        {
            for(auto name: names)
            {
                auto    symbol  = ExitOnErr(TheJIT->lookup(name));
                auto    func    = (bool (*)(state_t*, state_t*, void*))(intptr_t)symbol.getAddress();
                if(name == "debug")
                    continue;
                auto    result  = func(&state[0], &state[27], &conf);
                std::cout << std::setw(20) << std::left << name << " eval: " << result << std::endl; 
                ASSERT_FALSE(result);
            }
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
