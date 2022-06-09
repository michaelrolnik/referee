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

#include "compile.hpp"
#include "rewrite.hpp"
#include "printer.hpp"
#include "typecalc.hpp"
#include "../factory.hpp"

#include <functional>
#include <vector>

struct CompileTypeImpl
    : Visitor<  TypeInteger
             ,  TypeNumber
             ,  TypeString
             ,  TypeBoolean
             ,  TypeStruct
             ,  TypeEnum
             ,  TypeArray>
{
    CompileTypeImpl(
            llvm::LLVMContext*  context, 
            llvm::Module*       module);

    llvm::Type*     make(Type* type, std::string name);
    llvm::Value*    make(Expr* expr);

    void    visit(TypeInteger*          type) override;
    void    visit(TypeNumber*           type) override;
    void    visit(TypeString*           type) override;
    void    visit(TypeBoolean*          type) override;
    void    visit(TypeStruct*           type) override;
    void    visit(TypeEnum*             type) override;
    void    visit(TypeArray*            type) override;

    llvm::Type*         m_type  = nullptr;
    std::string         m_name;

private:
    llvm::LLVMContext*  m_context;
    llvm::Module*       m_module;
    llvm::Function*     m_func;
    std::unique_ptr<llvm::IRBuilder<>>
                        m_builder;
};

struct CompileExprImpl
    : Visitor< ExprAdd
             , ExprAnd
             , ExprAt
             , ExprChoice
             , ExprConstBoolean
             , ExprConstInteger
             , ExprConstNumber
             , ExprConstString
             , ExprContext
             , ExprData
             , ExprDiv
             , ExprEq
             , ExprEqu
             , ExprF
             , ExprG
             , ExprGe
             , ExprGt
             , ExprH
             , ExprImp
             , ExprIndx
             , ExprInt
             , ExprLe
             , ExprLt
             , ExprMmbr
             , ExprMod
             , ExprMul
             , ExprNe
             , ExprNeg
             , ExprNot
             , ExprO
             , ExprOr
             , ExprParen
             , ExprRs
             , ExprRw
             , ExprSs
             , ExprSub
             , ExprSw
             , ExprTs
             , ExprTw
             , ExprUs
             , ExprUw
             , ExprXor
             , ExprXs
             , ExprXw
             , ExprYs
             , ExprYw>
{
    CompileExprImpl(
            llvm::LLVMContext*  context, 
            llvm::Module*       module,
            llvm::IRBuilder<>*  builder,
            llvm::Function*     function,
            Module*             refmod);

    void    visit(ExprAdd*          expr) override;
    void    visit(ExprAnd*          expr) override;
    void    visit(ExprAt*           expr) override;
    void    visit(ExprChoice*       expr) override;
    void    visit(ExprConstBoolean* expr) override;
    void    visit(ExprConstInteger* expr) override;
    void    visit(ExprConstNumber*  expr) override;
    void    visit(ExprConstString*  expr) override;
    void    visit(ExprContext*      expr) override;
    void    visit(ExprData*         expr) override;
    void    visit(ExprDiv*          expr) override;
    void    visit(ExprEq*           expr) override;
    void    visit(ExprEqu*          expr) override;
    void    visit(ExprF*            expr) override;
    void    visit(ExprG*            expr) override;
    void    visit(ExprGe*           expr) override;
    void    visit(ExprGt*           expr) override;
    void    visit(ExprH*            expr) override;
    void    visit(ExprImp*          expr) override;
    void    visit(ExprIndx*         expr) override;
    void    visit(ExprInt*          expr) override;
    void    visit(ExprLe*           expr) override;
    void    visit(ExprLt*           expr) override;
    void    visit(ExprMmbr*         expr) override;
    void    visit(ExprMod*          expr) override;
    void    visit(ExprMul*          expr) override;
    void    visit(ExprNe*           expr) override;
    void    visit(ExprNeg*          expr) override;
    void    visit(ExprNot*          expr) override;
    void    visit(ExprO*            expr) override;
    void    visit(ExprOr*           expr) override;
    void    visit(ExprParen*        expr) override;
    void    visit(ExprRs*           expr) override;
    void    visit(ExprRw*           expr) override;
    void    visit(ExprSs*           expr) override;
    void    visit(ExprSub*          expr) override;
    void    visit(ExprSw*           expr) override;
    void    visit(ExprTs*           expr) override;
    void    visit(ExprTw*           expr) override;
    void    visit(ExprUs*           expr) override;
    void    visit(ExprUw*           expr) override;
    void    visit(ExprXor*          expr) override;
    void    visit(ExprXs*           expr) override;
    void    visit(ExprXw*           expr) override;
    void    visit(ExprYs*           expr) override;
    void    visit(ExprYw*           expr) override;

    void    compare(
                llvm::CmpInst::Predicate    ipred, 
                llvm::CmpInst::Predicate    fpred, ExprBinary* expr);
    void    arithmetic(
                ExprBinary* expr,
                std::function<llvm::Value*(llvm::Value*, llvm::Value*)> ifunc,
                std::function<llvm::Value*(llvm::Value*, llvm::Value*)> ffunc);


    llvm::Value*    make(Expr* expr);

private:
    llvm::LLVMContext*  m_context;
    llvm::Module*       m_module;
    llvm::Function*     m_function;
    llvm::IRBuilder<>*  m_builder;

    llvm::Value*        m_value;
    std::vector<llvm::Value*>
                        m_curr;
    std::vector<std::pair<std::string, llvm::Value*>>
                        m_name2value;

    Module*             m_refmod;
    llvm::Value*        m_0;
    llvm::Value*        m_1;
    llvm::Value*        m_T;
    llvm::Value*        m_F;
    llvm::Value*        m_frst;
    llvm::Value*        m_last;
    llvm::Value*        m_conf;
    llvm::Type*         m_propType;
    llvm::Type*         m_confType;
};


CompileTypeImpl::CompileTypeImpl(
            llvm::LLVMContext*  context, 
            llvm::Module*       module)
    : m_context(context)
    , m_module(module)
    , m_builder(std::make_unique<llvm::IRBuilder<>>(*m_context))
{
}

void    CompileTypeImpl::visit(TypeInteger*          type)
{
    m_type  = m_builder->getInt64Ty();
}

void    CompileTypeImpl::visit(TypeNumber*           type)
{
    m_type  = m_builder->getDoubleTy();
}

void    CompileTypeImpl::visit(TypeString*           type)
{
    m_type  = m_builder->getInt8PtrTy();
}

void    CompileTypeImpl::visit(TypeBoolean*          type)
{
    m_type  = m_builder->getInt1Ty();
}

void    CompileTypeImpl::visit(TypeStruct*           type)
{
    std::vector<llvm::Type*>    elements;
    
    for(auto member: type->members)
    {
        elements.push_back(Compile::make(m_context, m_module, member.data, m_name + "::" + member.name));
    }
    m_type  = llvm::StructType::create(*m_context, elements, m_name);
}

void    CompileTypeImpl::visit(TypeEnum*             type)
{
    m_type  = m_builder->getInt8Ty();
}

void    CompileTypeImpl::visit(TypeArray*            type)
{
    std::vector<llvm::Type*>    elements;

    elements.push_back(m_builder->getInt16Ty());
    auto    base    = Compile::make(m_context, m_module, type->type, m_name + "[]");
    auto    size    = type->size;

    if(size == 0)
    {
        elements.push_back(llvm::PointerType::get(base, 0));

        m_type  = llvm::StructType::create(*m_context, elements, m_name);
    }
    else
    {
        m_type  = llvm::ArrayType::get(base, size);
    }
}

llvm::Type* CompileTypeImpl::make(Type*  type, std::string name)
{
    m_name = name;
    
    type->accept(*this);

    return  this->m_type;
}

CompileExprImpl::CompileExprImpl(
            llvm::LLVMContext*  context, 
            llvm::Module*       module,
            llvm::IRBuilder<>*  builder,
            llvm::Function*     function,
            Module*             refmod)
    : m_context(context)
    , m_module(module)
    , m_function(function)
    , m_builder(builder)
    , m_refmod(refmod)
{
    m_0     = llvm::ConstantInt::getSigned(m_builder->getInt64Ty(), 0);
    m_1     = llvm::ConstantInt::getSigned(m_builder->getInt64Ty(), 1);
    m_T     = llvm::ConstantInt::getTrue(*m_context);
    m_F     = llvm::ConstantInt::getFalse(*m_context);

    auto    iter    = function->arg_begin();
    
    m_frst  = iter++;
    m_last  = iter++;
    m_conf  = iter;
    m_propType  = cast<llvm::PointerType>(m_frst->getType())->getPointerElementType();
    m_confType  = cast<llvm::PointerType>(m_conf->getType())->getPointerElementType();

    m_curr.push_back(m_frst);
}
 
void    CompileExprImpl::visit(ExprAdd*          expr)
{
    arithmetic(
        expr, 
        [this](llvm::Value* lhs, llvm::Value* rhs) {return m_builder->CreateAdd(lhs, rhs);},
        [this](llvm::Value* lhs, llvm::Value* rhs) {return m_builder->CreateFAdd(lhs, rhs);});
}

void    CompileExprImpl::visit(ExprAnd*          expr)
{
    auto    lhs = make(expr->lhs);
    auto    rhs = make(expr->rhs);
    m_value = m_builder->CreateLogicalAnd(lhs, rhs);
}

void    CompileExprImpl::visit(ExprAt*           expr)
{
    m_name2value.push_back(std::make_pair(expr->name, m_curr.back()));
    m_value = make(expr->arg);
    m_name2value.pop_back();
}

void    CompileExprImpl::visit(ExprChoice*       expr)
{
}

void    CompileExprImpl::visit(ExprConstBoolean* expr)
{
    m_value = llvm::ConstantInt::getBool(*m_context, expr->value);
}

void    CompileExprImpl::visit(ExprConstInteger* expr)
{
    m_value = llvm::ConstantInt::getSigned(m_builder->getInt64Ty(), expr->value);
}

void    CompileExprImpl::visit(ExprConstNumber*  expr)
{
    m_value = llvm::ConstantFP::get(m_builder->getDoubleTy(), expr->value);
}

void    CompileExprImpl::visit(ExprConstString*  expr)
{
}

void    CompileExprImpl::visit(ExprContext*      expr)
{
    if(expr->name == "__curr__")
    {
        m_value = m_curr.back();
    }
    else
    {
        for(auto it = m_name2value.rbegin(); it != m_name2value.rend(); it++)
        {
            if(expr->name == it->first)
            {
                m_value = it->second;
                return;
            }
        }
    
        throw std::runtime_error(__PRETTY_FUNCTION__);
    }
}

void    CompileExprImpl::visit(ExprData*         expr)
{
    auto    ctxtPtr     = make(expr->ctxt);
    auto    ctxtPtrType = cast<llvm::PointerType>(ctxtPtr->getType());
    auto    ctxtType    = ctxtPtrType->getPointerElementType();

    if(expr->name == "__time__")
    {
        auto    propPtr         = m_builder->CreateStructGEP(ctxtType, ctxtPtr, 0);     //  skip __time__
        auto    propPtrType     = cast<llvm::PointerType>(propPtr->getType());
        auto    propType        = propPtrType->getPointerElementType();

        m_value = m_builder->CreateLoad(propType, propPtr, "__time__");
    }
    else
    {
        auto    propPtrPtr      = m_builder->CreateStructGEP(ctxtType, ctxtPtr, dynamic_cast<TypeContext*>(expr->ctxt->type())->index(expr->name) + 1); //  +1 to skip __time__
        auto    propPtrPtrType  = cast<llvm::PointerType>(propPtrPtr->getType());
        auto    propPtrType     = cast<llvm::PointerType>(propPtrPtrType->getPointerElementType());

        auto    propType        = propPtrType->getPointerElementType();

        m_value = m_builder->CreateLoad(propPtrType, propPtrPtr, "ptr_" + expr->name);

        if(dynamic_cast<TypePrimitive*>(expr->type()))
        {
            m_value = m_builder->CreateLoad(propPtrType->getPointerElementType(), m_value, "val_" + expr->name);
        }
    }
}

void    CompileExprImpl::visit(ExprDiv*          expr)
{
    arithmetic(
        expr, 
        [this](llvm::Value* lhs, llvm::Value* rhs) {return m_builder->CreateSDiv(lhs, rhs);},
        [this](llvm::Value* lhs, llvm::Value* rhs) {return m_builder->CreateFDiv(lhs, rhs);});
}

void    CompileExprImpl::compare(
            llvm::CmpInst::Predicate    ipred, 
            llvm::CmpInst::Predicate    fpred, ExprBinary* expr)
{
    auto    lhs     = make(expr->lhs);
    auto    rhs     = make(expr->rhs);
    auto    lhsT    = expr->lhs->type();
    auto    rhsT    = expr->rhs->type();

    if(     lhsT == Factory<TypeNumber>::create() 
        && rhsT == Factory<TypeInteger>::create())
    {
        rhs = m_builder->CreateSIToFP(rhs, lhs->getType());
        m_value = m_builder->CreateFCmp(fpred, lhs, rhs);
    }
    else if(lhsT == Factory<TypeInteger>::create() 
        &&  rhsT == Factory<TypeNumber>::create())
    {
        lhs = m_builder->CreateSIToFP(lhs, rhs->getType());
        m_value = m_builder->CreateFCmp(fpred, lhs, rhs);
    }
    else if(lhsT == Factory<TypeInteger>::create() 
        &&  rhsT == Factory<TypeInteger>::create())
    {
        m_value = m_builder->CreateICmp(ipred, lhs, rhs);
    }
    else if(lhsT == Factory<TypeNumber>::create() 
        &&  rhsT == Factory<TypeNumber>::create())
    {
        m_value = m_builder->CreateFCmp(fpred, lhs, rhs);
    }
    else if(lhsT == Factory<TypeString>::create()
        &&  rhsT == Factory<TypeString>::create())
    {
        m_value = m_builder->CreateICmp(ipred, lhs, rhs);   //  TODO: check
    }
    else if(lhsT == Factory<TypeBoolean>::create()
        &&  rhsT == Factory<TypeBoolean>::create())
    {
        m_value = m_builder->CreateICmp(ipred, lhs, rhs);   //  TODO: check
    }
    else
    {
        throw std::runtime_error(__PRETTY_FUNCTION__);
    }
}

void    CompileExprImpl::arithmetic(
                ExprBinary* expr,
                std::function<llvm::Value*(llvm::Value*, llvm::Value*)> ifunc,
                std::function<llvm::Value*(llvm::Value*, llvm::Value*)> ffunc)
{
    auto    lhs     = make(expr->lhs);
    auto    rhs     = make(expr->rhs);
    auto    lhsT    = expr->lhs->type();
    auto    rhsT    = expr->rhs->type();

    if(     lhsT == Factory<TypeNumber>::create() 
        && rhsT == Factory<TypeInteger>::create())
    {
        rhs = m_builder->CreateSIToFP(rhs, lhs->getType());
        m_value = ffunc(lhs, rhs);
    }
    else if(lhsT == Factory<TypeInteger>::create() 
        &&  rhsT == Factory<TypeNumber>::create())
    {
        lhs = m_builder->CreateSIToFP(lhs, rhs->getType());
        m_value = ffunc(lhs, rhs);
    }
    else if(lhsT == Factory<TypeInteger>::create() 
        &&  rhsT == Factory<TypeInteger>::create())
    {
        m_value = ifunc(lhs, rhs);
    }
    else if(lhsT == Factory<TypeNumber>::create() 
        &&  rhsT == Factory<TypeNumber>::create())
    {
        m_value = ffunc(lhs, rhs);
    }
    else
    {
        throw std::runtime_error(__PRETTY_FUNCTION__);
    }    
}

void    CompileExprImpl::visit(ExprEq*           expr)
{
    compare(llvm::CmpInst::Predicate::ICMP_EQ, llvm::CmpInst::Predicate::FCMP_OEQ, expr);
}

void    CompileExprImpl::visit(ExprEqu*          expr)
{
}

void    CompileExprImpl::visit(ExprF*            expr)
{
}

void    CompileExprImpl::visit(ExprG*            expr)
{
}

void    CompileExprImpl::visit(ExprGe*           expr)
{
    compare(llvm::CmpInst::Predicate::ICMP_SGE, llvm::CmpInst::Predicate::FCMP_OGE, expr);
}

void    CompileExprImpl::visit(ExprGt*           expr)
{
    compare(llvm::CmpInst::Predicate::ICMP_SGT, llvm::CmpInst::Predicate::FCMP_OGT, expr);
}

void    CompileExprImpl::visit(ExprH*            expr)
{
}

void    CompileExprImpl::visit(ExprImp*          expr)
{
    auto    lhs = make(expr->lhs);
    auto    rhs = make(expr->rhs);
    m_value = m_builder->CreateOr(m_builder->CreateNot(lhs), rhs);
}

void    CompileExprImpl::visit(ExprIndx*         expr)
{
    auto    exprType    = expr->type();
    auto    basePtr     = make(expr->lhs);
    auto    indx        = make(expr->rhs);
    auto    basePtrType = cast<llvm::PointerType>(basePtr->getType());
    auto    baseType    = cast<llvm::ArrayType>(basePtrType->getPointerElementType());
    auto    elemType    = baseType->getElementType();

    m_value = m_builder->CreateGEP(baseType, basePtr, {m_0, indx}, "ptr_[]");
    m_value->print(llvm::outs()); std::cout << std::endl;

    if(dynamic_cast<TypePrimitive*>(exprType))
    {
        m_value = m_builder->CreateLoad(elemType, m_value, indx, "val_[]");
    }
}

void    CompileExprImpl::visit(ExprInt*          expr)
{
}

void    CompileExprImpl::visit(ExprLe*           expr)
{
    compare(llvm::CmpInst::Predicate::ICMP_SLE, llvm::CmpInst::Predicate::FCMP_OLE, expr);
}

void    CompileExprImpl::visit(ExprLt*           expr)
{
    compare(llvm::CmpInst::Predicate::ICMP_SLT, llvm::CmpInst::Predicate::FCMP_OLT, expr);
}

void    CompileExprImpl::visit(ExprMmbr*         expr)
{
    auto    exprType    = expr->type();
    auto    basePtr     = make(expr->arg);
    auto    basePtrType = cast<llvm::PointerType>(basePtr->getType());
    auto    baseType    = basePtrType->getPointerElementType();

    if(dynamic_cast<TypeComposite*>(exprType) || dynamic_cast<TypeArray*>(exprType))
    {
        auto    temp        = dynamic_cast<TypeComposite*>(expr->arg->type());

        m_value = m_builder->CreateStructGEP(baseType, basePtr, temp->index(expr->mmbr), "ptr_" + expr->mmbr);
    }
    else if(auto type = dynamic_cast<TypePrimitive*>(exprType))
    {
        auto    temp        = dynamic_cast<TypeComposite*>(expr->arg->type());

        if(dynamic_cast<TypeEnum*>(expr->arg->type()))
        {
            auto    data        = m_builder->CreateLoad(baseType, basePtr, "val_" + expr->mmbr);
            m_value = m_builder->CreateICmp(llvm::CmpInst::Predicate::ICMP_EQ, data, llvm::ConstantInt::getSigned(baseType, temp->index(expr->mmbr)));
        }
        else
        {
            auto    dataPtr     = m_builder->CreateStructGEP(baseType, basePtr, temp->index(expr->mmbr), "ptr_" + expr->mmbr);
            auto    dataPtrType = cast<llvm::PointerType>(dataPtr->getType());
            auto    dataType    = dataPtrType->getPointerElementType();
            
            m_value = m_builder->CreateLoad(dataType, dataPtr, "val_" + expr->mmbr);
        }
    }
    else
    {
        throw std::runtime_error(__PRETTY_FUNCTION__);    
    }
}

void    CompileExprImpl::visit(ExprMod*          expr)
{
}

void    CompileExprImpl::visit(ExprMul*          expr)
{
    arithmetic(
        expr, 
        [this](llvm::Value* lhs, llvm::Value* rhs) {return m_builder->CreateMul(lhs, rhs);},
        [this](llvm::Value* lhs, llvm::Value* rhs) {return m_builder->CreateFMul(lhs, rhs);});
}

void    CompileExprImpl::visit(ExprNe*           expr)
{
    compare(llvm::CmpInst::Predicate::ICMP_NE, llvm::CmpInst::Predicate::FCMP_ONE, expr);
}

void    CompileExprImpl::visit(ExprNeg*          expr)
{
}

void    CompileExprImpl::visit(ExprNot*          expr)
{
    auto    arg = make(expr->arg);
    m_value = m_builder->CreateNot(arg);
}

void    CompileExprImpl::visit(ExprO*            expr)
{
}

void    CompileExprImpl::visit(ExprOr*           expr)
{
    auto    lhs = make(expr->lhs);
    auto    rhs = make(expr->rhs);
    m_value = m_builder->CreateLogicalOr(lhs, rhs);
}

void    CompileExprImpl::visit(ExprParen*        expr)
{
    m_value = make(expr->arg);
}

void    CompileExprImpl::visit(ExprRs*           expr)
{
}

void    CompileExprImpl::visit(ExprRw*           expr)
{
}

void    CompileExprImpl::visit(ExprSs*           expr)
{
}

void    CompileExprImpl::visit(ExprSub*          expr)
{
    arithmetic(
        expr, 
        [this](llvm::Value* lhs, llvm::Value* rhs) {return m_builder->CreateSub(lhs, rhs);},
        [this](llvm::Value* lhs, llvm::Value* rhs) {return m_builder->CreateFSub(lhs, rhs);});
}

void    CompileExprImpl::visit(ExprSw*           expr)
{
}

void    CompileExprImpl::visit(ExprTs*           expr)
{
}

void    CompileExprImpl::visit(ExprTw*           expr)
{
}

void    CompileExprImpl::visit(ExprUs*           expr)
{
    auto    bbTest      = llvm::BasicBlock::Create(*m_context, "Us-test");
    auto    bbBodyRhs   = llvm::BasicBlock::Create(*m_context, "Us-body-rhs");
    auto    bbBodyLhs   = llvm::BasicBlock::Create(*m_context, "Us-body-lhs");
    auto    bbTail      = llvm::BasicBlock::Create(*m_context, "Us-tail");
    auto    frst        = m_curr.back();
    auto    last        = m_last;

    //  entry
    auto    bbEntry     = m_builder->GetInsertBlock();
    auto    frstGTlast  = m_builder->CreateICmpUGT(frst, last, "frst > last");
    m_builder->CreateCondBr(frstGTlast, bbTail, bbBodyRhs);

    //  bodyRhs
    m_function->getBasicBlockList().push_back(bbBodyRhs);
    m_builder->SetInsertPoint(bbBodyRhs);
    auto    curr        = m_builder->CreatePHI(frst->getType(), 2, "curr");
    m_curr.push_back(curr);
    auto    rhs         = make(expr->rhs);
    m_curr.pop_back();
    bbBodyRhs           = m_builder->GetInsertBlock();
    m_builder->CreateCondBr(rhs, bbTail, bbBodyLhs);

    //  bodyLhs
    m_function->getBasicBlockList().push_back(bbBodyLhs);
    m_builder->SetInsertPoint(bbBodyLhs);
    m_curr.push_back(curr);
    auto    lhs         = make(expr->lhs);
    m_curr.pop_back();    
    bbBodyLhs           = m_builder->GetInsertBlock();
    auto    nlhs        = m_builder->CreateNot(lhs, "!lhs");    
    auto    next        = m_builder->CreateGEP(m_propType, curr, m_1, "next");
    m_builder->CreateCondBr(nlhs, bbTest, bbTail);

    //  test
    m_function->getBasicBlockList().push_back(bbTest);
    m_builder->SetInsertPoint(bbTest);
    auto    nextGTlast  = m_builder->CreateICmpUGT(next, last, "next > last");
    m_builder->CreateCondBr(nextGTlast, bbTail, bbBodyRhs);

    //  tail
    m_function->getBasicBlockList().push_back(bbTail);
    m_builder->SetInsertPoint(bbTail);
    auto    result      = m_builder->CreatePHI(m_builder->getInt1Ty(), 4, "Us");

    //  link
    curr->addIncoming(next, bbTest);
    curr->addIncoming(frst, bbEntry);

    result->addIncoming(m_F, bbEntry);
    result->addIncoming(m_F, bbTest);
    result->addIncoming(m_F, bbBodyLhs);
    result->addIncoming(m_T, bbBodyRhs);

    m_value = result;

/*
    auto    iter    = frst;
    while(iter <= last)
    {
        if(eval(rhs) == true)
            return True
        
        if(eval(lhs) == false)
            return False

        iter++
    }
    return true
*/
}

void    CompileExprImpl::visit(ExprUw*           expr)
{
    auto    bbTest      = llvm::BasicBlock::Create(*m_context, "Us-test");
    auto    bbBodyRhs   = llvm::BasicBlock::Create(*m_context, "Us-body-rhs");
    auto    bbBodyLhs   = llvm::BasicBlock::Create(*m_context, "Us-body-lhs");
    auto    bbTail      = llvm::BasicBlock::Create(*m_context, "Us-tail");
    auto    frst        = m_curr.back();
    auto    last        = m_last;

    //  entry
    auto    bbEntry     = m_builder->GetInsertBlock();
    auto    frstGTlast  = m_builder->CreateICmpUGT(frst, last, "frst > last");
    m_builder->CreateCondBr(frstGTlast, bbTail, bbBodyRhs);

    //  bodyRhs
    m_function->getBasicBlockList().push_back(bbBodyRhs);
    m_builder->SetInsertPoint(bbBodyRhs);
    auto    curr        = m_builder->CreatePHI(frst->getType(), 2, "curr");
    m_curr.push_back(curr);
    auto    rhs         = make(expr->rhs);
    m_curr.pop_back();
    bbBodyRhs           = m_builder->GetInsertBlock();
    m_builder->CreateCondBr(rhs, bbTail, bbBodyLhs);

    //  bodyLhs
    m_function->getBasicBlockList().push_back(bbBodyLhs);
    m_builder->SetInsertPoint(bbBodyLhs);
    m_curr.push_back(curr);
    auto    lhs         = make(expr->lhs);
    m_curr.pop_back();    
    bbBodyLhs           = m_builder->GetInsertBlock();
    auto    nlhs        = m_builder->CreateNot(lhs, "!lhs");    
    auto    next        = m_builder->CreateGEP(m_propType, curr, m_1, "next");
    m_builder->CreateCondBr(nlhs, bbTest, bbTail);

    //  test
    m_function->getBasicBlockList().push_back(bbTest);
    m_builder->SetInsertPoint(bbTest);
    auto    nextGTlast  = m_builder->CreateICmpUGT(next, last, "next > last");
    m_builder->CreateCondBr(nextGTlast, bbTail, bbBodyRhs);

    //  tail
    m_function->getBasicBlockList().push_back(bbTail);
    m_builder->SetInsertPoint(bbTail);
    auto    result      = m_builder->CreatePHI(m_builder->getInt1Ty(), 4, "Uw");

    //  link
    curr->addIncoming(next, bbTest);
    curr->addIncoming(frst, bbEntry);

    result->addIncoming(m_T, bbEntry);
    result->addIncoming(m_T, bbTest);
    result->addIncoming(m_F, bbBodyLhs);
    result->addIncoming(m_T, bbBodyRhs);

    m_value = result;

/*
    auto    iter    = frst;
    while(iter <= last)
    {
        if(eval(rhs) == true)
            return True
        
        if(eval(lhs) == false)
            return False

        iter++
    }
    return true
*/
}

void    CompileExprImpl::visit(ExprXor*          expr)
{
    auto    lhs = make(expr->lhs);
    auto    rhs = make(expr->rhs);
    m_value = m_builder->CreateXor(lhs, rhs);   //  TODO: fix
}

void    CompileExprImpl::visit(ExprXs*           expr)
{
    auto    bbHead  = llvm::BasicBlock::Create(*m_context, "Xs-head");
    auto    bbBody  = llvm::BasicBlock::Create(*m_context, "Xs-body");
    auto    bbTail  = llvm::BasicBlock::Create(*m_context, "Xs-tail");
    auto    frst        = m_curr.back();
    auto    last        = m_last;

    m_builder->CreateBr(bbHead);

    //  head
    m_function->getBasicBlockList().push_back(bbHead);
    m_builder->SetInsertPoint(bbHead);
    auto    frstGTlast  = m_builder->CreateICmpUGT(frst, last, "frst > last");
    m_builder->CreateCondBr(frstGTlast, bbTail, bbBody);

    //  body
    m_function->getBasicBlockList().push_back(bbBody);
    m_builder->SetInsertPoint(bbBody);
    auto    next        = m_builder->CreateGEP(m_propType, frst, m_1, "next");
    m_curr.push_back(next);
    auto    body        = make(expr->arg);
    m_curr.pop_back();
    bbBody              = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbTail);

    //  tail
    m_function->getBasicBlockList().push_back(bbTail);
    m_builder->SetInsertPoint(bbTail);
    auto    result      = m_builder->CreatePHI(m_builder->getInt1Ty(), 2, "Xs");

    //  link
    result->addIncoming(body, bbBody);
    result->addIncoming(m_F, bbHead);

    m_value = result;
}

void    CompileExprImpl::visit(ExprXw*           expr)
{
    auto    bbHead  = llvm::BasicBlock::Create(*m_context, "Xw-head");
    auto    bbBody  = llvm::BasicBlock::Create(*m_context, "Xw-body");
    auto    bbTail  = llvm::BasicBlock::Create(*m_context, "Xw-tail");
    auto    frst        = m_curr.back();
    auto    last        = m_last;

    m_builder->CreateBr(bbHead);

    //  head
    m_function->getBasicBlockList().push_back(bbHead);
    m_builder->SetInsertPoint(bbHead);
    auto    frstGTlast  = m_builder->CreateICmpUGT(frst, last, "frst > last");
    m_builder->CreateCondBr(frstGTlast, bbTail, bbBody);

    //  body
    m_function->getBasicBlockList().push_back(bbBody);
    m_builder->SetInsertPoint(bbBody);
    auto    next        = m_builder->CreateGEP(m_propType, frst, m_1, "next");
    m_curr.push_back(next);
    auto    body        = make(expr->arg);
    m_curr.pop_back();
    bbBody              = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbTail);

    //  tail
    m_function->getBasicBlockList().push_back(bbTail);
    m_builder->SetInsertPoint(bbTail);
    auto    result      = m_builder->CreatePHI(m_builder->getInt1Ty(), 2, "Xs");

    //  link
    result->addIncoming(body, bbBody);
    result->addIncoming(m_T, bbHead);

    m_value = result;
}

void    CompileExprImpl::visit(ExprYs*           expr)
{
}

void    CompileExprImpl::visit(ExprYw*           expr)
{
}

llvm::Value*    CompileExprImpl::make(Expr* expr)
{
    auto    save    = m_value;

    m_value = llvm::ConstantInt::getTrue(*m_context);

    expr->accept(*this);

    auto    result  = m_value;
    m_value = save;
    
    return  result;
}

llvm::Type* Compile::make(llvm::LLVMContext* context, llvm::Module* module, Type* type, std::string name)
{
    CompileTypeImpl impl(context, module);

    return impl.make(type, name);
}

llvm::Value*Compile::make(llvm::LLVMContext* context, llvm::Module* module, Expr* expr)
{
    return nullptr;
}

void Compile::make(llvm::LLVMContext* context, llvm::Module* module, Module* refmod)
{
    auto    builder = std::make_unique<llvm::IRBuilder<>>(*context);

    //  create __conf__
    std::vector<llvm::Type*>    confTypes;
    auto    confNames   = refmod->getConfNames();
    for(auto name: confNames)
    {
        auto    type    = refmod->getConf(name);
        confTypes.push_back(llvm::PointerType::get(make(context, module, type, name), 0));
    }
    auto    confType    = llvm::StructType::create(*context, confTypes, "__conf__");
    auto    confPtrType = llvm::PointerType::get(confType, 0);
    module->getOrInsertGlobal("__conf__", confType);

    //  create __prop__
    auto    propNames   = refmod->getPropNames();
    std::vector<llvm::Type*>    propTypes;
    propTypes.push_back(builder->getInt64Ty()); //  __time__
    for(auto name: propNames)
    {
        if(name == "__time__")
            continue;
            
        std::cout << "prop: " << name << std::endl;
        auto    type    = refmod->getProp(name);
        propTypes.push_back(llvm::PointerType::get(make(context, module, type, name), 0));
    }
    auto    propType    = llvm::StructType::create(*context, propTypes, "__prop__");
    auto    propPtrType = llvm::PointerType::get(propType, 0);
    module->getOrInsertGlobal("__prop__", propPtrType);


    auto    exprs   = refmod->getExprs();
    for(auto expr: exprs)
    {
        auto    pos         = expr->where();
        auto    funcName    = std::to_string(pos.beg.row) + ":" + std::to_string(pos.beg.col) + " .. " + std::to_string(pos.end.row) + ":" + std::to_string(pos.end.col);
        auto    funcType    = llvm::FunctionType::get(builder->getInt8Ty(), {propPtrType, propPtrType, confPtrType}, false);
        auto    funcBody    = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, funcName, module);
        auto    funcArgs    = funcBody->args().begin();

        funcArgs->setName("frst");  funcArgs++;
        funcArgs->setName("last");  funcArgs++;
        funcArgs->setName("conf");

        auto    bb          = llvm::BasicBlock::Create(*context, "entry", funcBody);
        builder->SetInsertPoint(bb);

        CompileExprImpl compExpr(context, module, builder.get(), funcBody, refmod);

        auto    temp        = Rewrite::make(expr);
        TypeCalc::make(refmod, temp);
        Printer::output(std::cout, temp) << std::endl;
        builder->CreateRet(compExpr.make(temp));

        llvm::verifyFunction(*funcBody);
    }
}
