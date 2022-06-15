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
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include <memory>

#include "antlr2ast.hpp"
#include "visitors/compile.hpp"


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

        auto ES     = std::make_unique<llvm::orc::ExecutionSession>(std::move(*EPC));
        auto JTMB   = llvm::orc::JITTargetMachineBuilder(ES->getExecutorProcessControl().getTargetTriple());
        auto DL     = JTMB.getDefaultDataLayoutForTarget();

        if (!DL)
            return DL.takeError();

        return std::make_unique<RefereeJIT>(std::move(ES), std::move(JTMB), std::move(*DL));
    }

    const llvm::DataLayout& getDataLayout() const { return DL; }
    llvm::orc::JITDylib&    getMainJITDylib() { return MainJD; }
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

class LogicTest : public ::testing::Test {

protected:
    state_t     state[27];
    bool        T   = true;
    bool        F   = false;

protected:
    void SetUp() override
    {
        for(int i = 0 ; i < 27; i++)
        {
            state[i].time   = i * 1000;

            for(int j = 0; j < 26; j++)
            {
                state[i].lttr[j]    = i == j ? &T : &F;
            }
        }
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

TEST_F(LogicTest, Pass)
{
    std::string                 filename    = "../test/logic/pass.ref";
    std::ifstream               stream(filename, std::ios_base::in);

    ASSERT_TRUE(stream.is_open());

    antlr4::ANTLRInputStream    input(stream);
    referee::refereeLexer       lexer(&input);
    antlr4::CommonTokenStream   tokens(&lexer);
    referee::refereeParser      parser(&tokens);
    Antlr2AST                   antlr2ast;

    auto    TheContext  = std::make_unique<llvm::LLVMContext>();
    auto    TheModule   = std::make_unique<llvm::Module>(filename, *TheContext);
    auto    TheBuilder  = std::make_unique<llvm::IRBuilder<>>(*TheContext);   
    auto    TheFPM      = std::make_unique<llvm::legacy::FunctionPassManager>(TheModule.get());
    auto    TheJIT      = RefereeJIT::Create();

    TheModule->setDataLayout(TheJIT.get()->getDataLayout());

    LLVMInitializeNativeTarget();
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    try {
        auto*   tree    = parser.program();
        auto*   module  = std::any_cast<Module*>(antlr2ast.visitProgram(tree));

        Compile::make(TheContext.get(), TheModule.get(), module);

        TheFPM->add(llvm::createInstructionCombiningPass());
        TheFPM->add(llvm::createReassociatePass());
        TheFPM->add(llvm::createGVNPass());
        TheFPM->add(llvm::createCFGSimplificationPass());

        TheFPM->doInitialization();

        auto& functions = TheModule->getFunctionList();

        for(auto iter = functions.begin(); iter != functions.end(); iter++)
            TheFPM->run(*iter);
/*
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
*/
        TheModule->dump();

        TheJIT.get()->addModule(llvm::orc::ThreadSafeModule(std::move(TheModule), std::move(TheContext)));
        //.get()->addModule(std::move(TheModule));
        //for(auto iter = functions.begin(); iter != functions.end(); iter++)
        //{
            //TheJIT.get()->findSymbol("__anon_expr");
        //}
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