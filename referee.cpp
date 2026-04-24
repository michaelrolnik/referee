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

#include "referee.hpp"

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
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/TargetParser/Host.h"

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

#include "antlr2ast.hpp"
#include "strings.hpp"
#include "visitors/compile.hpp"

void    Referee::compile(std::istream& is, std::string name, std::ostream& os)
{
    antlr4::ANTLRInputStream    input(is);
    referee::refereeLexer       lexer(&input);
    antlr4::CommonTokenStream   tokens(&lexer);
    referee::refereeParser      parser(&tokens);
    Antlr2AST                   antlr2ast(name);

    auto    TheContext  = std::make_unique<llvm::LLVMContext>();
    auto    TheModule   = std::make_unique<llvm::Module>(name, *TheContext);
    auto    TheBuilder  = std::make_unique<llvm::IRBuilder<>>(*TheContext);   
    auto    TheFPM      = std::make_unique<llvm::legacy::FunctionPassManager>(TheModule.get());

    auto    funcType    = llvm::FunctionType::get(TheBuilder->getVoidTy(), {TheBuilder->getInt64Ty()}, false);
    __attribute__((unused))
    auto    func        = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, "debug", *TheModule);

    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    //  pin the module to the host data layout
    {
        llvm::Triple                        triple{llvm::sys::getDefaultTargetTriple()};
        llvm::orc::JITTargetMachineBuilder  JTMB{triple};
        if (auto DL = JTMB.getDefaultDataLayoutForTarget())
        {
            TheModule->setDataLayout(*DL);
            TheModule->setTargetTriple(triple.getTriple());
        }
    }

    try {
        auto*   tree    = parser.program();
        auto*   module  = std::any_cast<Module*>(antlr2ast.visitProgram(tree));

        Compile::make(TheContext.get(), TheModule.get(), module);

        TheFPM->add(llvm::createInstructionCombiningPass());
        TheFPM->add(llvm::createReassociatePass());
        TheFPM->add(llvm::createGVNPass());
        TheFPM->add(llvm::createCFGSimplificationPass());
        //  TheFPM->add(llvm::createLoopStrengthReducePass());
        //  TheFPM->add(llvm::createLoopLoadEliminationPass());
        TheFPM->add(llvm::createLoopDataPrefetchPass());
        //  TheFPM->add(llvm::createLoopSimplifyCFGPass());
        //  TheFPM->add(llvm::createLoopGuardWideningPass());
        //  TheFPM->add(llvm::createLoopDistributePass());
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

        lowerMinMaxIntrinsics(*TheModule);

        auto    xyz = llvm::raw_os_ostream(os);
        TheModule->print(xyz, nullptr);
    }
    catch(Exception& e)
    {
        std::cerr << "exception: " << e.what() << std::endl;
    }
    catch(std::exception& e)
    {
        std::cerr << "exception: " << e.what() << std::endl;
    }    
}
