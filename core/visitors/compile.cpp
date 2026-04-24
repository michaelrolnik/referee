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
#include "typecalc.hpp"
#include "strings.hpp"
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
             , ExprConf
             , ExprDiv
             , ExprEq
             , ExprEqu
             , ExprGe
             , ExprGt
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
             , ExprYw
             , Spec
             , SpecGlobally
             , SpecBefore
             , SpecAfter
             , SpecBetweenAnd
             , SpecAfterUntil>
{
    CompileExprImpl(
            llvm::LLVMContext*  context,
            llvm::Module*       module,
            llvm::IRBuilder<>*  builder,
            llvm::Function*     function,
            Module*             refmod,
            llvm::Type*         propType,
            llvm::Type*         confType);

    void    visit(ExprAdd*          expr) override;
    void    visit(ExprAnd*          expr) override;
    void    visit(ExprAt*           expr) override;
    void    visit(ExprChoice*       expr) override;
    void    visit(ExprConstBoolean* expr) override;
    void    visit(ExprConstInteger* expr) override;
    void    visit(ExprConstNumber*  expr) override;
    void    visit(ExprConstString*  expr) override;
    void    visit(ExprContext*      expr) override;
    void    visit(ExprConf*         expr) override;
    void    visit(ExprData*         expr) override;
    void    visit(ExprDiv*          expr) override;
    void    visit(ExprEq*           expr) override;
    void    visit(ExprEqu*          expr) override;
    void    visit(ExprGe*           expr) override;
    void    visit(ExprGt*           expr) override;
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

    void    visit(Spec*             spec) override;
    void    visit(SpecGlobally*     spec) override;
    void    visit(SpecBefore*       spec) override;
    void    visit(SpecAfter*        spec) override;
    void    visit(SpecBetweenAnd*   spec) override;
    void    visit(SpecAfterUntil*   spec) override;

    void    compare(
                llvm::CmpInst::Predicate    ipred,
                llvm::CmpInst::Predicate    fpred, ExprBinary* expr);
    void    arithmetic(
                ExprBinary* expr,
                std::function<llvm::Value*(llvm::Value*, llvm::Value*)> ifunc,
                std::function<llvm::Value*(llvm::Value*, llvm::Value*)> ffunc);

    void    XY( ExprBinary*         expr,
                llvm::Value*        direction,
                llvm::Value*        endValue,
                std::string         name);

    void    UR( Temporal<ExprBinary>*   expr,
                llvm::Value*            rhsV,
                llvm::Value*            lhsV,
                llvm::Value*            endV,
                std::string             name);
    void    ST( Temporal<ExprBinary>*   expr,
                llvm::Value*            rhsV,
                llvm::Value*            lhsV,
                llvm::Value*            endV,
                std::string             name);

    llvm::Value*    make(Expr* expr);
    llvm::Value*    make(Spec* spec);

    llvm::Value*    getNext(llvm::Value* curr);
    llvm::Value*    getPrev(llvm::Value* curr);
    llvm::Value*    getTime(llvm::Value* curr, std::string name = "__time__");
    llvm::Value*    getPropPtr(llvm::Value* var);
    llvm::Value*    setPropPtr(llvm::Value* var, llvm::Value* val);
    llvm::Value*    getBool(llvm::Value* var);
    llvm::Value*    setBool(llvm::Value* var, llvm::Value* val);

private:
    llvm::Value*        add(llvm::Value* lhs, llvm::Value* rhs, std::string const& name);
    llvm::Value*        mul(llvm::Value* lhs, llvm::Value* rhs, std::string const& name);
    llvm::Value*        sub(llvm::Value* lhs, llvm::Value* rhs, std::string const& name);

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
    llvm::Value*        m_p1;
    llvm::Value*        m_m1;
    llvm::Value*        m_T;
    llvm::Value*        m_F;
    std::vector<llvm::Value*>   m_frst;
    std::vector<llvm::Value*>   m_last;
    llvm::Value*        m_conf;
    llvm::Type*         m_propType;
    llvm::Type*         m_propPtrType;
    llvm::Type*         m_confType;
    llvm::Type*         m_confPtrType;
    llvm::Type*         m_boolType;
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
    m_type  = m_builder->getPtrTy();
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
        elements.push_back(llvm::PointerType::get(*m_context, 0));

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
            Module*             refmod,
            llvm::Type*         propType,
            llvm::Type*         confType)
    : m_context(context)
    , m_module(module)
    , m_function(function)
    , m_builder(builder)
    , m_refmod(refmod)
{
    m_0     = llvm::ConstantInt::getSigned(m_builder->getInt64Ty(), 0);
    m_p1    = llvm::ConstantInt::getSigned(m_builder->getInt64Ty(), +1);
    m_m1    = llvm::ConstantInt::getSigned(m_builder->getInt64Ty(), -1);
    m_T     = llvm::ConstantInt::getTrue(*m_context);
    m_F     = llvm::ConstantInt::getFalse(*m_context);

    auto    iter    = function->arg_begin();

    m_frst.push_back(iter++);
    m_last.push_back(iter++);
    m_conf  = iter;
    m_propType      = propType;
    m_propPtrType   = m_frst.front()->getType();
    m_confType      = confType;
    m_confPtrType   = m_conf->getType();
    m_boolType      = m_builder->getInt1Ty();

    m_curr.push_back(getNext(m_frst.front()));
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
    auto    lhs = make(expr->lhs);
    auto    mhs = make(expr->mhs);
    auto    rhs = make(expr->rhs);
    auto    mhsT= mhs->getType();
    auto    rhsT= rhs->getType();

    if(mhsT == rhsT)
    {
        m_value = m_builder->CreateSelect(lhs, mhs, rhs);
    }
    else
    {
        m_value = m_builder->CreateSelect(
            lhs,
            m_builder->CreateSIToFP(mhs, m_builder->getDoubleTy()),
            m_builder->CreateSIToFP(rhs, m_builder->getDoubleTy()));
    }
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
    auto    value   = llvm::ConstantInt::getSigned(m_builder->getInt64Ty(), reinterpret_cast<int64_t>(Strings::instance()->getString(expr->value)));

    m_value = m_builder->CreateIntToPtr(value, m_builder->getPtrTy(), expr->value);
}

void    CompileExprImpl::visit(ExprContext*      expr)
{
    if(expr->name == "__curr__")
    {
        m_value = m_curr.back();
    }
    else if(expr->name == "__conf__")
    {
        m_value = m_conf;
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

//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
        throw std::runtime_error(__PRETTY_FUNCTION__);
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
    }
}

void    CompileExprImpl::visit(ExprConf*         expr)
{
    auto    ctxtPtr     = make(expr->ctxt);
    auto    ctxtType    = m_confType;

    auto    confPtr     = m_builder->CreateStructGEP(ctxtType, ctxtPtr, dynamic_cast<TypeContext*>(expr->ctxt->type())->index(expr->name));

    if(dynamic_cast<TypePrimitive*>(expr->type()))
    {
        auto    fieldType   = Compile::make(m_context, m_module, expr->type(), expr->name);
        m_value = m_builder->CreateLoad(fieldType, m_value, false, "val_" + expr->name);
    }
    else
    {
        m_value = confPtr;
    }
}

void    CompileExprImpl::visit(ExprData*         expr)
{
    auto    ctxtPtr     = make(expr->ctxt);
    auto    ctxtType    = m_propType;

    if(expr->name == "__time__")
    {
        auto    propPtr         = m_builder->CreateStructGEP(ctxtType, ctxtPtr, 0);     //  skip __time__
        m_value = m_builder->CreateLoad(m_builder->getInt64Ty(), propPtr, false, "__time__");
    }
    else
    {
        auto    propPtrPtr      = m_builder->CreateStructGEP(ctxtType, ctxtPtr, dynamic_cast<TypeContext*>(expr->ctxt->type())->index(expr->name) + 1); //  +1 to skip __time__
        auto    propPtrType     = m_builder->getPtrTy();

        m_value = m_builder->CreateLoad(propPtrType, propPtrPtr, false, "ptr_" + expr->name);

        if(dynamic_cast<TypePrimitive*>(expr->type()))
        {
            auto    propFieldType   = Compile::make(m_context, m_module, expr->type(), expr->name);
            m_value = m_builder->CreateLoad(propFieldType, m_value, false, "val_" + expr->name);
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
//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
        throw std::runtime_error(__PRETTY_FUNCTION__);
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
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
//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
        throw std::runtime_error(__PRETTY_FUNCTION__);
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
    }
}

void    CompileExprImpl::visit(ExprEq*           expr)
{
    compare(llvm::CmpInst::Predicate::ICMP_EQ, llvm::CmpInst::Predicate::FCMP_OEQ, expr);
}

void    CompileExprImpl::visit(ExprEqu*          expr)
{
    auto    lhs     = make(expr->lhs);
    auto    rhs     = make(expr->rhs);
    m_value = m_builder->CreateICmp(llvm::CmpInst::Predicate::ICMP_EQ, lhs, rhs);
}

void    CompileExprImpl::visit(ExprGe*           expr)
{
    compare(llvm::CmpInst::Predicate::ICMP_SGE, llvm::CmpInst::Predicate::FCMP_OGE, expr);
}

void    CompileExprImpl::visit(ExprGt*           expr)
{
    compare(llvm::CmpInst::Predicate::ICMP_SGT, llvm::CmpInst::Predicate::FCMP_OGT, expr);
}

void    CompileExprImpl::visit(ExprIndx*         expr)
{
    auto    exprType    = expr->type();
    auto    basePtr     = make(expr->lhs);
    auto    indx        = make(expr->rhs);
    auto    baseLLVMType= Compile::make(m_context, m_module, expr->lhs->type(), "[]");
    auto    baseType    = cast<llvm::ArrayType>(baseLLVMType);
    auto    elemType    = baseType->getElementType();

    m_value = m_builder->CreateGEP(baseType, basePtr, {m_0, indx}, "ptr_[]");

    if(dynamic_cast<TypePrimitive*>(exprType))
    {
        m_value = m_builder->CreateLoad(elemType, m_value, false, "val_[]");
    }
}

llvm::Value*    CompileExprImpl::add(llvm::Value* lhs, llvm::Value* rhs, std::string const& name)
{
    auto    typeInteger = m_builder->getInt64Ty();
    auto    typeNumber  = m_builder->getDoubleTy();

    if(lhs->getType() == typeInteger && rhs->getType() == typeInteger)
    {
        return  m_builder->CreateAdd(lhs, rhs, name);
    }

    if(lhs->getType() == typeInteger)
    {
        lhs = m_builder->CreateSIToFP(lhs, typeNumber);
    }

    if(rhs->getType() == typeInteger)
    {
        rhs = m_builder->CreateSIToFP(rhs, typeNumber);
    }

    return  m_builder->CreateFAdd(lhs, rhs, name);
}

llvm::Value*    CompileExprImpl::mul(llvm::Value* lhs, llvm::Value* rhs, std::string const& name)
{
    auto    typeInteger = m_builder->getInt64Ty();
    auto    typeNumber  = m_builder->getDoubleTy();

    if(lhs->getType() == typeInteger && rhs->getType() == typeInteger)
    {
        return  m_builder->CreateMul(lhs, rhs, name);
    }

    if(lhs->getType() == typeInteger)
    {
        lhs = m_builder->CreateSIToFP(lhs, typeNumber);
    }

    if(rhs->getType() == typeInteger)
    {
        rhs = m_builder->CreateSIToFP(rhs, typeNumber);
    }

    return  m_builder->CreateFMul(lhs, rhs, name);
}

llvm::Value*    CompileExprImpl::sub(llvm::Value* lhs, llvm::Value* rhs, std::string const& name)
{
    auto    typeInteger = m_builder->getInt64Ty();
    auto    typeNumber  = m_builder->getDoubleTy();

    if(lhs->getType() == typeInteger && rhs->getType() == typeInteger)
    {
        return  m_builder->CreateSub(lhs, rhs, name);
    }

    if(lhs->getType() == typeInteger)
    {
        lhs = m_builder->CreateSIToFP(lhs, typeNumber);
    }

    if(rhs->getType() == typeInteger)
    {
        rhs = m_builder->CreateSIToFP(rhs, typeNumber);
    }

    return  m_builder->CreateFSub(lhs, rhs, name);
}

void    CompileExprImpl::visit(ExprInt*          expr)
{
/*
typedef long long uint64_t;
typedef struct prop_t
{
    uint64_t    __time__;
    uint64_t    a;
} prop_t;

bool        boolean(prop_t const* curr);
bool        integer(prop_t const* curr);

uint64_t    integral(uint64_t lo, uint64_t hi, prop_t const* curr, prop_t const* frst, prop_t const* last)
{
    prop_t const*   next    = curr + 1;
    uint64_t        result  = 0;

    lo  = lo + curr->__time__;
    hi  = hi + curr->__time__;

    while(next <= last)
    {
        if(hi <= curr->__time__)
        {
            break;
        }

        uint64_t    _lo = curr->__time__ < lo ? lo : curr->__time__;
        uint64_t    _hi = hi < next->__time__ ? hi : next->__time__;

        if(_lo < _hi)
        {
            if(boolean(curr))
            {
                uint64_t height = eval(curr);
                uint64_t length = (_hi - _lo);
                uint64_t volume = height * length;
                result  +=  volume;
            }
        }

        curr    = next;
        next    = next + 1;
    }

    return  result;
}

uint64_t    integral(prop_t const* curr, prop_t const* frst, prop_t const* last)
{
    prop_t const*   next    = curr + 1;
    uint64_t        result  = 0;

    while(next <= last)
    {
        uint64_t    _lo = curr->__time__;
        uint64_t    _hi = next->__time__;

        if(_lo < _hi)
        {
            if(boolean(curr))
            {
                uint64_t height = eval(curr);
                uint64_t length = (_hi - _lo);
                uint64_t volume = height * length;
                result  +=  volume;
            }
        }

        curr    = next;
        next    = next + 1;
    }

    return  result;
}
*/
    auto    bbHead  = llvm::BasicBlock::Create(*m_context, "I-head", m_function);

    auto    bbTime  = llvm::BasicBlock::Create(*m_context, "I-time", m_function);
    auto    bbCondHi= llvm::BasicBlock::Create(*m_context, "I-cond", m_function);
    auto    bbCondLo = bbCondHi;
    auto    bbBodyHi= llvm::BasicBlock::Create(*m_context, "I-body", m_function);
    auto    bbBodyLo= bbBodyHi;
    auto    bbNext  = llvm::BasicBlock::Create(*m_context, "I-next", m_function);
    auto    bbTail  = llvm::BasicBlock::Create(*m_context, "I-tail", m_function);

    auto    frst    = m_frst.back();
    auto    curr0   = m_curr.back();
    auto    next0   = getNext(curr0);
    auto    last    = m_last.back();

    llvm::Value*    timeLo  = nullptr;
    llvm::Value*    timeHi  = nullptr;
    auto    now     = getTime(curr0);

    if(expr->time && expr->time->lo)
    {
        timeLo  = m_builder->CreateAdd(now, make(expr->time->lo), "now + lo");
    }

    if(expr->time && expr->time->hi)
    {
        timeHi  = m_builder->CreateAdd(now, make(expr->time->hi), "now + hi");
    }

    auto    bbEntry = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbHead);

    auto    type    = expr->rhs->type() == Factory<TypeInteger>::create()
                    ? m_builder->getInt64Ty()
                    : m_builder->getDoubleTy();
    auto    zero    = expr->rhs->type() == Factory<TypeInteger>::create()
                    ? llvm::ConstantInt::getSigned(m_builder->getInt64Ty(), 0)
                    : llvm::ConstantFP::get(m_builder->getDoubleTy(), 0.0);

    //  head
    m_builder->SetInsertPoint(bbHead);
    auto    sumHead = m_builder->CreatePHI(type, 2, "total@head");

    auto    curr    = m_builder->CreatePHI(frst->getType(), 2, "curr->__time__");
    auto    next    = m_builder->CreatePHI(frst->getType(), 2, "next->__time__");
    auto    cont    = m_builder->CreateICmpSLE(next, last, "next <= last");
    if(timeHi)
    {
        auto    currT   = getTime(curr, "curr->__time__");
        auto    timeHiLEcurrT
                        = m_builder->CreateICmpSGT(timeHi, currT, "timeHi > currT");
        cont    = m_builder->CreateAnd(cont, timeHiLEcurrT);
    }
    m_builder->CreateCondBr(cont, bbTime, bbTail);

    //  outer
    m_builder->SetInsertPoint(bbTime);

    auto    currT   = getTime(curr, "curr->__time__");
    auto    nextT   = getTime(next, "next->__time__");

    auto    lo      = timeLo
                    ? m_builder->CreateSelect(m_builder->CreateICmpSLT(currT, timeLo), timeLo, currT)
                    : currT;
    auto    hi      = timeHi
                    ? m_builder->CreateSelect(m_builder->CreateICmpSLT(timeHi, nextT), timeHi, nextT)
                    : nextT;
    auto    loLThi  = m_builder->CreateICmpSLT(lo, hi);

    m_builder->CreateCondBr(loLThi, bbCondHi, bbNext);

    //  inner
    m_builder->SetInsertPoint(bbCondHi);
    m_curr.push_back(curr);
    auto    cond    = make(expr->lhs);
    m_curr.pop_back();
    bbCondLo       = m_builder->GetInsertBlock();
    m_builder->CreateCondBr(cond, bbBodyHi, bbNext);

    //  body
    m_builder->SetInsertPoint(bbBodyHi);
    m_curr.push_back(curr);
    auto    height  = make(expr->rhs);
    auto    length  = sub(hi, lo, "length");
    auto    volume  = mul(height, length, "volume");
    auto    sumBody = add(sumHead, volume, "total@body");
    m_curr.pop_back();

    bbBodyLo        = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbNext);

    //  next
    m_builder->SetInsertPoint(bbNext);
    auto    sumNext = m_builder->CreatePHI(type, 3, "total@next");
    auto    curr1   = getNext(curr);
    auto    next1   = getNext(next);
    m_builder->CreateBr(bbHead);

    //  tail
    m_builder->SetInsertPoint(bbTail);

    //auto    result  = m_builder->CreatePHI(type, 1, "value");

    //  link
    curr->addIncoming(curr0, bbEntry);
    curr->addIncoming(curr1, bbNext);

    next->addIncoming(next0, bbEntry);
    next->addIncoming(next1, bbNext);

    //result->addIncoming(sumHead, bbHead);

    sumNext->addIncoming(sumBody, bbBodyLo);
    sumNext->addIncoming(sumHead, bbCondLo);
    sumNext->addIncoming(sumHead, bbTime);

    sumHead->addIncoming(zero,    bbEntry);
    sumHead->addIncoming(sumNext, bbNext);

    //result->addIncoming( sumHead, bbHead);

    m_value = sumHead;
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
    auto    baseType    = Compile::make(m_context, m_module, expr->arg->type(), expr->mmbr);

    if(dynamic_cast<TypeComposite*>(exprType) || dynamic_cast<TypeArray*>(exprType))
    {
        auto    temp        = dynamic_cast<TypeComposite*>(expr->arg->type());

        m_value = m_builder->CreateStructGEP(baseType, basePtr, temp->index(expr->mmbr), "ptr_" + expr->mmbr);
    }
    else if(dynamic_cast<TypePrimitive*>(exprType))
    {
        auto    temp        = dynamic_cast<TypeComposite*>(expr->arg->type());

        if(dynamic_cast<TypeEnum*>(expr->arg->type()))
        {
            auto    data        = m_builder->CreateLoad(baseType, basePtr, false, "val_" + expr->mmbr);
            m_value = m_builder->CreateICmp(llvm::CmpInst::Predicate::ICMP_EQ, data, llvm::ConstantInt::getSigned(baseType, temp->index(expr->mmbr)));
        }
        else
        {
            auto    dataPtr     = m_builder->CreateStructGEP(baseType, basePtr, temp->index(expr->mmbr), "ptr_" + expr->mmbr);
            auto    dataType    = Compile::make(m_context, m_module, expr->type(), expr->mmbr);

            m_value = m_builder->CreateLoad(dataType, dataPtr, false, "val_" + expr->mmbr);
        }
    }
    else
    {
//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
        throw std::runtime_error(__PRETTY_FUNCTION__);
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
    }
}

void    CompileExprImpl::visit(ExprMod*          expr)
{
    m_value = m_builder->CreateURem(make(expr->lhs), make(expr->rhs));
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
    auto    arg     = make(expr->arg);
    auto    argT    = expr->arg->type();

    if(argT == Factory<TypeNumber>::create())
    {
        m_value = m_builder->CreateFNeg(arg);
    }

    else if(argT == Factory<TypeInteger>::create())
    {
        m_value = m_builder->CreateNeg(arg);
    }
    else
    {
//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
        throw std::runtime_error(__PRETTY_FUNCTION__);
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
    }
}

void    CompileExprImpl::visit(ExprNot*          expr)
{
    auto    arg = make(expr->arg);
    m_value = m_builder->CreateNot(arg);
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
    UR(expr, m_F, m_T, m_F, "Rs");
}

void    CompileExprImpl::visit(ExprRw*           expr)
{
    UR(expr, m_F, m_T, m_T, "Rw");
}

void    CompileExprImpl::visit(ExprSs*           expr)
{
    ST(expr, m_T, m_F, m_F, "Ss");
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
    ST(expr, m_T, m_F, m_T, "Sw");
}

void    CompileExprImpl::visit(ExprTs*           expr)
{
    ST(expr, m_F, m_T, m_F, "Ts");
}

void    CompileExprImpl::visit(ExprTw*           expr)
{
    ST(expr, m_F, m_T, m_T, "Tw");
}

void    CompileExprImpl::visit(ExprUs*           expr)
{
    UR(expr, m_T, m_F, m_F, "Us");
}

void    CompileExprImpl::visit(ExprUw*           expr)
{
    UR(expr, m_T, m_F, m_T, "Uw");
}

void    CompileExprImpl::visit( ExprXor*        expr)
{
    auto    lhs = make(expr->lhs);
    auto    rhs = make(expr->rhs);
    m_value = m_builder->CreateXor(lhs, rhs);   //  TODO: fix
}

void    CompileExprImpl::XY(ExprBinary*     expr,
                            llvm::Value*    direction,
                            llvm::Value*    endV,
                            std::string     name)
{
    auto    bbHeadHi= llvm::BasicBlock::Create(*m_context, name + "-head", m_function);
    auto    bbHeadLo= bbHeadHi;
    auto    bbBodyHi= llvm::BasicBlock::Create(*m_context, name + "-body", m_function);
    auto    bbBodyLo= bbBodyHi;
    auto    bbTail  = llvm::BasicBlock::Create(*m_context, name + "-tail", m_function);
    auto    frst    = m_frst.back();
    auto    curr    = m_curr.back();
    auto    last    = m_last.back();

    m_builder->CreateBr(bbHeadHi);

    //  head
    m_builder->SetInsertPoint(bbHeadHi);
    auto    incr        = m_builder->CreateMul(make(expr->lhs), direction, "incr");
    auto    next        = m_builder->CreateGEP(m_propType, curr, incr, "next");
    auto    nextGElast  = m_builder->CreateICmpUGE(next, last, "next >= last");
    auto    nextLEfrst  = m_builder->CreateICmpULE(next, frst, "next <= frst");
    auto    outside     = m_builder->CreateOr(nextGElast, nextLEfrst);
    bbHeadLo            = m_builder->GetInsertBlock();
    m_builder->CreateCondBr(outside, bbTail, bbBodyHi);

    //  body
    m_builder->SetInsertPoint(bbBodyHi);
    m_curr.push_back(next);
    auto    body        = make(expr->rhs);
    m_curr.pop_back();
    bbBodyLo            = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbTail);

    //  tail
    m_builder->SetInsertPoint(bbTail);
    auto    result      = m_builder->CreatePHI(m_boolType, 2, name);

    //  link
    result->addIncoming(body, bbBodyLo);
    result->addIncoming(endV, bbHeadLo);

    m_value = result;
}

void    CompileExprImpl::UR(Temporal<ExprBinary>*   expr,
                            llvm::Value*            rhsV,
                            llvm::Value*            lhsV,
                            llvm::Value*            endV,
                            std::string             name)
{
/*
typedef long long uint64_t;
typedef struct prop_t
{
    uint64_t    __time__;
    uint64_t    a;
} prop_t;

bool        boolean(prop_t const* curr);
bool        integer(prop_t const* curr);

uint64_t    UR(uint64_t lo, uint64_t hi, prop_t const* curr, prop_t const* frst, prop_t const* last, bool lhsV, bool rhsV, bool endV)
{
    prop_t const*   next    = curr + 1;

    lo  = lo + curr->__time__;
    hi  = hi + curr->__time__;

    while(next <= last)
    {
        if(hi <= curr->__time__)
        {
            break;
        }

        uint64_t    _lo = curr->__time__ < lo ? lo : curr->__time__;
        uint64_t    _hi = hi < next->__time__ ? hi : next->__time__;

        if(_lo < _hi)
        {
            if(eval(rhs) == rhsV)
                return rshV;

            if(eval(lhs) == lhsV)
                return lshV;
        }

        curr    = next;
        next    = next + 1;
    }

    return  endV;
}

uint64_t    UR(prop_t const* curr, prop_t const* frst, prop_t const* last, bool lhsV, bool rhsV, bool endV)
{
    prop_t const*   next    = curr + 1;
    uint64_t        result  = 0;

    while(next <= last)
    {
        uint64_t    _lo = curr->__time__;
        uint64_t    _hi = next->__time__;

        if(_lo < _hi)
        {
            if(eval(rhs) == rhsV)
                return rshV;

            if(eval(lhs) == lhsV)
                return lshV;
        }

        curr    = next;
        next    = next + 1;
    }

    return  endV;
}
*/
    auto    bbWhile = llvm::BasicBlock::Create(*m_context, name + "-while", m_function);
    auto    bbOuter = llvm::BasicBlock::Create(*m_context, name + "-outer", m_function);
    auto    bbInner = llvm::BasicBlock::Create(*m_context, name + "-inner", m_function);
    auto    bbLhsHi = llvm::BasicBlock::Create(*m_context, name + "-body-lhs", m_function);
    auto    bbLhsLo = bbLhsHi;
    auto    bbRhsHi = llvm::BasicBlock::Create(*m_context, name + "-body-rhs", m_function);
    auto    bbRhsLo = bbRhsHi;
    auto    bbNext  = llvm::BasicBlock::Create(*m_context, name + "-next", m_function);
    auto    bbTail  = llvm::BasicBlock::Create(*m_context, name + "-tail", m_function);

    auto    frst    = m_frst.back();
    auto    curr0   = m_curr.back();
    auto    next0   = getNext(curr0);
    auto    last    = m_last.back();

    llvm::Value*    timeLo  = nullptr;
    llvm::Value*    timeHi  = nullptr;
    auto    now     = getTime(curr0, "curr->__time__");

    if(expr->time && expr->time->lo)
    {
        timeLo  = m_builder->CreateAdd(now, make(expr->time->lo), "now + lo");
    }

    if(expr->time && expr->time->hi)
    {
        timeHi  = m_builder->CreateAdd(now, make(expr->time->hi), "now + hi");
    }
    auto    bbEntry = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbWhile);

    //  while
    m_builder->SetInsertPoint(bbWhile);
    auto    curr    = m_builder->CreatePHI(frst->getType(), 2, "curr");
    auto    next    = m_builder->CreatePHI(frst->getType(), 2, "next");
    auto    nextLElast    = m_builder->CreateICmpSLE(next, last, "next <= last");
    m_builder->CreateCondBr(nextLElast, bbOuter, bbTail);

    //  outer
    m_builder->SetInsertPoint(bbOuter);
    if(timeHi)
    {
        auto    currT           = getTime(curr, "curr->__time__");
        auto    timeHiLTcurrT   = m_builder->CreateICmpSGT(timeHi, currT, "timeHi > currT");
        m_builder->CreateCondBr(timeHiLTcurrT, bbInner, bbTail);
    }
    else if(timeLo)
    {
        m_builder->CreateBr(bbInner);
    }
    else
    {
        m_builder->CreateBr(bbRhsHi);
    }

    //  inner
    m_builder->SetInsertPoint(bbInner);
    auto    currT   = getTime(curr, "curr->__time__");
    auto    nextT   = getTime(next, "next->__time__");

    auto    lo      = timeLo
                    ? m_builder->CreateSelect(m_builder->CreateICmpSLT(currT, timeLo), timeLo, currT)
                    : currT;
    auto    hi      = timeHi
                    ? m_builder->CreateSelect(m_builder->CreateICmpSLT(timeHi, nextT), timeHi, nextT)
                    : nextT;
    auto    loLThi  = m_builder->CreateICmpSLT(lo, hi);

    m_builder->CreateCondBr(loLThi, bbRhsHi, bbNext);


    //  rhs
    m_builder->SetInsertPoint(bbRhsHi);
    m_curr.push_back(curr);
    auto    rhs     = make(expr->rhs);
    m_curr.pop_back();
    auto    rhsCond = m_builder->CreateCmp(llvm::CmpInst::Predicate::ICMP_EQ, rhs, rhsV);
    m_builder->CreateCondBr(rhsCond, bbTail, bbLhsHi);
    bbRhsLo = m_builder->GetInsertBlock();

    //  lhs
    m_builder->SetInsertPoint(bbLhsHi);
    m_curr.push_back(curr);
    auto    lhs     = make(expr->lhs);
    m_curr.pop_back();
    auto    lhsCond = m_builder->CreateCmp(llvm::CmpInst::Predicate::ICMP_EQ, lhs, lhsV);
    m_builder->CreateCondBr(lhsCond, bbTail, bbNext);
    bbLhsLo = m_builder->GetInsertBlock();

    //  next
    m_builder->SetInsertPoint(bbNext);
    auto    curr1   = next;
    auto    next1   = getNext(curr1);
    m_builder->CreateBr(bbWhile);

    //  tail
    m_builder->SetInsertPoint(bbTail);
    auto    result  = m_builder->CreatePHI(m_boolType, timeHi ? 4 : 3, "result");

    //  link
    result->addIncoming(lhsV, bbLhsLo);
    result->addIncoming(rhsV, bbRhsLo);
    result->addIncoming(endV, bbWhile);
    if(timeHi)
        result->addIncoming(endV, bbOuter);

    curr->addIncoming(curr0, bbEntry);
    curr->addIncoming(curr1, bbNext);

    next->addIncoming(next0, bbEntry);
    next->addIncoming(next1, bbNext);

    m_value = result;
}

void    CompileExprImpl::ST(Temporal<ExprBinary>*   expr,
                            llvm::Value*            rhsV,
                            llvm::Value*            lhsV,
                            llvm::Value*            endV,
                            std::string             name)
{
/*
typedef long long uint64_t;
typedef struct prop_t
{
    uint64_t    __time__;
    uint64_t    a;
} prop_t;

bool        boolean(prop_t const* curr);
bool        integer(prop_t const* curr);

uint64_t    ST(uint64_t lo, uint64_t hi, prop_t const* curr, prop_t const* frst, prop_t const* last, bool lhsV, bool rhsV, bool endV)
{
    prop_t const*   prev    = curr - 1;

    lo  = curr->__time__ - lo;
    hi  = curr->__time__ - hi;
    swap(lo, hi);

    while(frst <= prev)
    {
        ifcurr->__time__ < lo)
        {
            break;
        }

        uint64_t    _lo = lo > curr->__time__ ? lo : curr->__time__;
        uint64_t    _hi = prev->__time__ < hi ? hi : prev->__time__;

        if(_lo < _hi)
        {
            if(eval(rhs) == rhsV)
                return rshV;

            if(eval(lhs) == lhsV)
                return lshV;
        }

        curr    = prev;
        prev    = prev - 1;
    }

    return  endV;
}

uint64_t    ST(prop_t const* curr, prop_t const* frst, prop_t const* last, bool lhsV, bool rhsV, bool endV)
{
    prop_t const*   prev    = curr - 1;
    uint64_t        result  = 0;

    while(frst <= prev)
    {
        uint64_t    _hi = curr->__time__;
        uint64_t    _lo = prev->__time__;

        if(_lo < _hi)
        {
            if(eval(rhs) == rhsV)
                return rshV;

            if(eval(lhs) == lhsV)
                return lshV;
        }

        curr    = prev;
        prev    = prev + 1;
    }

    return  endV;
}
*/
    auto    bbWhile = llvm::BasicBlock::Create(*m_context, name + "-while", m_function);
    auto    bbOuter = llvm::BasicBlock::Create(*m_context, name + "-outer", m_function);
    auto    bbInner = llvm::BasicBlock::Create(*m_context, name + "-inner", m_function);
    auto    bbRhsHi = llvm::BasicBlock::Create(*m_context, name + "-body-rhs", m_function);
    auto    bbRhsLo = bbRhsHi;
    auto    bbLhsHi = llvm::BasicBlock::Create(*m_context, name + "-body-lhs", m_function);
    auto    bbLhsLo = bbLhsHi;
    auto    bbPrev  = llvm::BasicBlock::Create(*m_context, name + "-prev", m_function);
    auto    bbTail  = llvm::BasicBlock::Create(*m_context, name + "-tail", m_function);

    auto    frst    = m_frst.back();
    auto    curr0   = m_curr.back();
    auto    prev0   = getPrev(curr0);
    __attribute__((unused))
    auto    last    = m_last.back();

    llvm::Value*    timeLo  = nullptr;
    llvm::Value*    timeHi  = nullptr;
    auto    now     = getTime(curr0, "now");

    if(expr->time && expr->time->lo)
    {
        timeHi  = m_builder->CreateSub(now, make(expr->time->lo), "now - lo");
    }

    if(expr->time && expr->time->hi)
    {
        timeLo  = m_builder->CreateSub(now, make(expr->time->hi), "now - hi");
    }
    auto    bbEntry = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbWhile);

    //  while
    m_builder->SetInsertPoint(bbWhile);
    auto    curr    = m_builder->CreatePHI(frst->getType(), 2, "curr");
    auto    prev    = m_builder->CreatePHI(frst->getType(), 2, "prev");
    auto    frstLEprev    = m_builder->CreateICmpSLE(frst, prev, "frst <= prev");
    m_builder->CreateCondBr(frstLEprev, bbOuter, bbTail);

    //  outer
    m_builder->SetInsertPoint(bbOuter);
    if(timeHi)
    {
        auto    currT           = getTime(curr, "curr->__time__");
        auto    timeLoLEcurrT   = m_builder->CreateICmpSLE(timeLo, currT, "timeLo <= currT");
        m_builder->CreateCondBr(timeLoLEcurrT, bbInner, bbTail);
    }
    else if(timeLo)
    {
        m_builder->CreateBr(bbInner);
    }
    else
    {
        m_builder->CreateBr(bbRhsHi);
    }

    //  inner
    m_builder->SetInsertPoint(bbInner);
    auto    currT   = getTime(curr, "curr->__time__");
    auto    prevT   = getTime(prev, "prev->__time__");

    auto    lo      = timeHi
                    ? m_builder->CreateSelect(m_builder->CreateICmpSLT(prevT, timeLo), timeLo, prevT)
                    : prevT;
    auto    hi      = timeLo
                    ? m_builder->CreateSelect(m_builder->CreateICmpSLT(timeHi, currT), timeHi, currT)
                    : currT;

    auto    loLThi  = m_builder->CreateICmpSLT(lo, hi);

    m_builder->CreateCondBr(loLThi, bbRhsHi, bbPrev);


    //  rhs
    m_builder->SetInsertPoint(bbRhsHi);
    m_curr.push_back(curr);
    auto    rhs     = make(expr->rhs);
    m_curr.pop_back();
    auto    rhsCond = m_builder->CreateCmp(llvm::CmpInst::Predicate::ICMP_EQ, rhs, rhsV);
    m_builder->CreateCondBr(rhsCond, bbTail, bbLhsHi);
    bbRhsLo = m_builder->GetInsertBlock();

    //  lhs
    m_builder->SetInsertPoint(bbLhsHi);
    m_curr.push_back(curr);
    auto    lhs     = make(expr->lhs);
    m_curr.pop_back();
    auto    lhsCond = m_builder->CreateCmp(llvm::CmpInst::Predicate::ICMP_EQ, lhs, lhsV);
    m_builder->CreateCondBr(lhsCond, bbTail, bbPrev);
    bbLhsLo = m_builder->GetInsertBlock();

    //  next
    m_builder->SetInsertPoint(bbPrev);
    auto    curr1   = prev;
    auto    prev1   = getPrev(curr1);
    m_builder->CreateBr(bbWhile);

    //  tail
    m_builder->SetInsertPoint(bbTail);
    auto    result  = m_builder->CreatePHI(m_boolType, timeHi ? 4 : 3, "result");

    //  link
    result->addIncoming(lhsV, bbLhsLo);
    result->addIncoming(rhsV, bbRhsLo);
    result->addIncoming(endV, bbWhile);
    if(timeHi)
        result->addIncoming(endV, bbOuter);

    curr->addIncoming(curr0, bbEntry);
    curr->addIncoming(curr1, bbPrev);

    prev->addIncoming(prev0, bbEntry);
    prev->addIncoming(prev1, bbPrev);

    m_value = result;
}

void    CompileExprImpl::visit( ExprXs*         expr)
{
    XY(expr, m_p1, m_F, "Xs");
}

void    CompileExprImpl::visit(ExprXw*           expr)
{
    XY(expr, m_p1, m_T, "Xw");
}

void    CompileExprImpl::visit(ExprYs*           expr)
{
    XY(expr, m_m1, m_F, "Ys");
}

void    CompileExprImpl::visit(ExprYw*           expr)
{
    XY(expr, m_m1, m_T, "Yw");
}

void    CompileExprImpl::visit(Spec*             spec)
{
    auto    expr    = Rewrite::make(spec);
    TypeCalc::make(m_refmod, expr);

    m_value = make(expr);
}

void    CompileExprImpl::visit(SpecGlobally*     spec)
{
    m_value = make(spec->spec);
}

void    CompileExprImpl::visit(SpecBefore*       spec)
{
/*
    auto    curr    = frst + 1;
    auto    result  = true;

    while(curr < last)
    {
        if(eval(curr, spec->arg))
        {
            result  = eval(frst, curr, frst+1, spec->spec);
            break
        }

        curr    = curr + 1;
    }

    return result;
*/

    auto    bbWhile = llvm::BasicBlock::Create(*m_context, "spec-while",m_function);
    auto    bbTestHi= llvm::BasicBlock::Create(*m_context, "spec-test", m_function);
    __attribute__((unused))
    auto    bbTestLo= bbTestHi;
    auto    bbBodyHi= llvm::BasicBlock::Create(*m_context, "spec-body", m_function);
    auto    bbBodyLo= bbBodyHi;
    auto    bbNext  = llvm::BasicBlock::Create(*m_context, "spec-next", m_function);
    auto    bbTail  = llvm::BasicBlock::Create(*m_context, "spec-tail", m_function);
    auto    bbEntry = m_builder->GetInsertBlock();

    auto    frst    = m_frst.back();
    auto    last    = m_last.back();

    auto    curr0   = getNext(frst);

    m_builder->CreateBr(bbWhile);

    //  while
    m_builder->SetInsertPoint(bbWhile);
    auto    curr        = m_builder->CreatePHI(frst->getType(), 2, "curr");
    auto    currLTlast  = m_builder->CreateICmpSLT(curr, last, "curr < last");
    m_builder->CreateCondBr(currLTlast, bbTestHi, bbTail);

    //  test
    m_builder->SetInsertPoint(bbTestHi);
    m_curr.push_back(curr);
    auto    test= make(spec->arg);
    m_curr.pop_back();
    bbTestLo    = m_builder->GetInsertBlock();
    m_builder->CreateCondBr(test, bbBodyHi, bbNext);

    //  body
    m_builder->SetInsertPoint(bbBodyHi);
    m_curr.push_back(curr0);
    m_last.push_back(curr);
    auto    body    = make(spec->spec);
    m_last.pop_back();
    m_last.pop_back();
    bbBodyLo    = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbTail);

    //  next
    m_builder->SetInsertPoint(bbNext);
    auto    curr1   = getNext(curr);
    m_builder->CreateBr(bbWhile);

    //  tail
    m_builder->SetInsertPoint(bbTail);
    auto    result  = m_builder->CreatePHI(m_boolType, 2, "result");

    //  links
    curr->addIncoming(curr0, bbEntry);
    curr->addIncoming(curr1, bbNext);

    result->addIncoming(m_T, bbWhile);
    result->addIncoming(body,bbBodyLo);

    m_value = result;
}

void    CompileExprImpl::visit(SpecAfter*        spec)
{
/*
    auto    curr    = frst + 1;
    auto    result  = true;

    while(curr < last)
    {
        if(eval(curr, spec->arg))
        {
            result  = eval(curr - 1, last, curr, spec->spec);
            break
        }

        prev    = curr;
        curr    = curr + 1;
    }

    return result;
*/

    auto    bbWhile = llvm::BasicBlock::Create(*m_context, "spec-while",m_function);
    auto    bbTestHi= llvm::BasicBlock::Create(*m_context, "spec-test", m_function);
    __attribute__((unused))
    auto    bbTestLo= bbTestHi;
    auto    bbBodyHi= llvm::BasicBlock::Create(*m_context, "spec-body", m_function);
    auto    bbBodyLo= bbBodyHi;
    auto    bbNext  = llvm::BasicBlock::Create(*m_context, "spec-next", m_function);
    auto    bbTail  = llvm::BasicBlock::Create(*m_context, "spec-tail", m_function);
    auto    bbEntry = m_builder->GetInsertBlock();

    auto    frst    = m_frst.back();
    auto    last    = m_last.back();

    auto    curr0   = getNext(frst);

    m_builder->CreateBr(bbWhile);

    //  while
    m_builder->SetInsertPoint(bbWhile);
    auto    curr        = m_builder->CreatePHI(frst->getType(), 2, "curr");
    auto    currLTlast  = m_builder->CreateICmpSLT(curr, last, "curr < last");
    m_builder->CreateCondBr(currLTlast, bbTestHi, bbTail);

    //  test
    m_builder->SetInsertPoint(bbTestHi);
    m_curr.push_back(curr);
    auto    test= make(spec->arg);
    m_curr.pop_back();
    bbTestLo    = m_builder->GetInsertBlock();
    m_builder->CreateCondBr(test, bbBodyHi, bbNext);

    //  body
    m_builder->SetInsertPoint(bbBodyHi);
    m_frst.push_back(getPrev(curr));
    m_curr.push_back(curr);
    auto    body    = make(spec->spec);
    m_curr.pop_back();
    m_frst.pop_back();
    bbBodyLo    = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbTail);

    //  next
    m_builder->SetInsertPoint(bbNext);
    auto    curr1   = getNext(curr);
    m_builder->CreateBr(bbWhile);

    //  tail
    m_builder->SetInsertPoint(bbTail);
    auto    result  = m_builder->CreatePHI(m_boolType, 2, "result");

    //  links
    curr->addIncoming(curr0, bbEntry);
    curr->addIncoming(curr1, bbNext);

    result->addIncoming(m_T, bbWhile);
    result->addIncoming(body,bbBodyLo);

    m_value = result;
}

void    CompileExprImpl::visit(SpecBetweenAnd*   spec)
{
    auto    bbWhile     = llvm::BasicBlock::Create(*m_context, "spec-while", m_function);
    auto    bbEvalCondHi= llvm::BasicBlock::Create(*m_context, "spec-eval-cond", m_function);
    auto    bbEvalCondLo= bbEvalCondHi;
    auto    bbEvalBodyHi= llvm::BasicBlock::Create(*m_context, "spec-eval-body", m_function);
    auto    bbEvalBodyLo= bbEvalBodyHi;
    auto    bbNext      = llvm::BasicBlock::Create(*m_context, "spec-next", m_function);
    auto    bbTail      = llvm::BasicBlock::Create(*m_context, "spec-tail", m_function);
    auto    bbDone      = llvm::BasicBlock::Create(*m_context, "spec-done", m_function);

    //  head
    auto    bbHead      = m_builder->GetInsertBlock();
    auto    outerFrst   = m_frst.back();
    auto    outerLast   = m_last.back();
    auto    curr0       = getNext(outerFrst);
    auto    innerFrst0  = outerFrst;

    m_builder->CreateBr(bbWhile);

    //  while
    m_builder->SetInsertPoint(bbWhile);
    auto    curr        = m_builder->CreatePHI(m_propPtrType, 2, "curr");
    auto    mainInside      = m_builder->CreatePHI(m_boolType, 2, "inside");
    auto    mainInnerFrst   = m_builder->CreatePHI(m_propPtrType, 2, "innerFrst");
    auto    currLTlast  = m_builder->CreateICmpSLT(curr, outerLast, "curr < last");
    m_builder->CreateCondBr(currLTlast, bbEvalCondHi, bbTail);

    //  eval cond
    m_builder->SetInsertPoint(bbEvalCondHi);
    m_curr.push_back(curr);
    auto    lhs         = make(spec->lhs);
    auto    rhs         = make(spec->rhs);
    m_curr.pop_back();
    bbEvalCondLo        = m_builder->GetInsertBlock();
    auto    enter       = m_builder->CreateAnd({
                            m_builder->CreateNot(mainInside),
                            lhs,
                            m_builder->CreateNot(rhs)});
    auto    leave       = m_builder->CreateAnd({
                            mainInside,
                            rhs});
    auto    condInside  = m_builder->CreateSelect(
                            enter,
                            m_T,
                            m_builder->CreateSelect(
                                leave,
                                m_F,
                                mainInside),
                            "inside");
    auto    condInnerFrst
                        = m_builder->CreateSelect(
                            enter,
                            getPrev(curr),
                            mainInnerFrst,
                            "innerFrst");
    m_builder->CreateCondBr(leave, bbEvalBodyHi, bbNext);

    //  eval body
    m_builder->SetInsertPoint(bbEvalBodyHi);
    auto    bodyInnerFrst   = m_builder->CreatePHI(m_propPtrType, 2, "innerFrst");
    auto    bodyInside      = m_builder->CreatePHI(m_boolType, 2, "inside");
    auto    bodyInnerLast   = curr;
    m_frst.push_back(bodyInnerFrst);
    m_last.push_back(bodyInnerLast);
    m_curr.push_back(getNext(m_frst.back()));
    auto    body        = make(spec->spec);
    m_curr.pop_back();
    m_frst.pop_back();
    m_last.pop_back();
    bbEvalBodyLo        = m_builder->GetInsertBlock();
    m_builder->CreateCondBr(body, bbNext, bbDone);

    //  next
    m_builder->SetInsertPoint(bbNext);
    auto    nextInnerFrst   = m_builder->CreatePHI(m_propPtrType, 2, "innerFrst");
    auto    nextInside  = m_builder->CreatePHI(m_boolType, 2, "inside");
    auto    curr1       = getNext(curr);
    m_builder->CreateBr(bbWhile);

    //  tail
    m_builder->SetInsertPoint(bbTail);
    auto    currEQlast  = m_builder->CreateICmpEQ(curr, outerLast, "curr == last");
    auto    tail        = m_builder->CreateAnd({currEQlast, mainInside});
    m_builder->CreateCondBr(tail, bbEvalBodyHi, bbDone);

    //  done
    m_builder->SetInsertPoint(bbDone);
    auto    result      = m_builder->CreatePHI(m_boolType, 2, "result");
    m_value = result;

    //  link
    result->addIncoming(m_T, bbTail);
    result->addIncoming(m_F, bbEvalBodyLo);

    curr->addIncoming(curr0, bbHead);
    curr->addIncoming(curr1, bbNext);

    mainInnerFrst->addIncoming( innerFrst0,     bbHead);
    mainInnerFrst->addIncoming( nextInnerFrst,  bbNext);

    mainInside->addIncoming(    m_F,            bbHead);
    mainInside->addIncoming(    nextInside,     bbNext);

    bodyInnerFrst->addIncoming( condInnerFrst,  bbEvalCondLo);
    bodyInnerFrst->addIncoming( mainInnerFrst,  bbTail);

    bodyInside->addIncoming(    condInside,     bbEvalCondLo);
    bodyInside->addIncoming(    mainInside,     bbTail);

    nextInnerFrst->addIncoming( condInnerFrst,  bbEvalCondLo);
    nextInnerFrst->addIncoming( bodyInnerFrst,  bbEvalBodyLo);

    nextInside->addIncoming(    condInside,     bbEvalCondLo);
    nextInside->addIncoming(    bodyInside,     bbEvalBodyLo);
}

void    CompileExprImpl::visit(SpecAfterUntil*   spec)
{
    auto    bbWhile     = llvm::BasicBlock::Create(*m_context, "spec-while", m_function);
    auto    bbEvalCondHi= llvm::BasicBlock::Create(*m_context, "spec-eval-cond", m_function);
    __attribute__((unused))
    auto    bbEvalCondLo= bbEvalCondHi;
    auto    bbEvalBodyHi= llvm::BasicBlock::Create(*m_context, "spec-eval-body", m_function);
    auto    bbEvalBodyLo= bbEvalBodyHi;
    auto    bbNext      = llvm::BasicBlock::Create(*m_context, "spec-next", m_function);
    auto    bbDone      = llvm::BasicBlock::Create(*m_context, "spec-done", m_function);

    //  head
    auto    bbHead      = m_builder->GetInsertBlock();
    auto    outerFrst   = m_frst.back();
    auto    outerLast   = m_last.back();
    auto    curr0       = getNext(outerFrst);
    auto    innerFrst0  = outerFrst;
    m_builder->CreateBr(bbWhile);

    //  while
    m_builder->SetInsertPoint(bbWhile);
    auto    curr        = m_builder->CreatePHI(m_propPtrType, 2, "curr");
    auto    inside      = m_builder->CreatePHI(m_boolType, 2, "inside");
    auto    innerFrst   = m_builder->CreatePHI(m_propPtrType, 2, "innerFrst");
    auto    currLTlast  = m_builder->CreateICmpSLT(curr, outerLast, "curr < last");
    m_builder->CreateCondBr(currLTlast, bbEvalCondHi, bbDone);

    //  eval cond
    m_builder->SetInsertPoint(bbEvalCondHi);
    m_curr.push_back(curr);
    auto    lhs         = make(spec->lhs);
    auto    rhs         = make(spec->rhs);
    m_curr.pop_back();
    bbEvalCondLo        = m_builder->GetInsertBlock();
    auto    enter       = m_builder->CreateAnd({
                            m_builder->CreateNot(inside),
                            lhs,
                            m_builder->CreateNot(rhs)});
    auto    leave       = m_builder->CreateAnd({
                            inside,
                            rhs});
    auto    inside1     = m_builder->CreateSelect(
                            enter,
                            m_T,
                            m_builder->CreateSelect(
                                leave,
                                m_F,
                                inside),
                            "inside");
    auto    innerFrst1  = m_builder->CreateSelect(
                            enter,
                            getPrev(curr),
                            innerFrst,
                            "innerFrst");
    m_builder->CreateCondBr(leave, bbEvalBodyHi, bbNext);

    //  eval body
    m_builder->SetInsertPoint(bbEvalBodyHi);
    auto    innerLast   = curr;
    m_frst.push_back(innerFrst1);
    m_last.push_back(innerLast);
    m_curr.push_back(getNext(innerFrst1));
    auto    body        = make(spec->spec);
    m_curr.pop_back();
    m_frst.pop_back();
    m_last.pop_back();
    bbEvalBodyLo        = m_builder->GetInsertBlock();
    m_builder->CreateCondBr(body, bbNext, bbDone);

    //  next
    m_builder->SetInsertPoint(bbNext);
    auto    curr1       = getNext(curr);
    m_builder->CreateBr(bbWhile);

    //  done
    m_builder->SetInsertPoint(bbDone);
    auto    result      = m_builder->CreatePHI(m_boolType, 2, "result");
    m_value = result;

    //  link
    result->addIncoming(m_F, bbEvalBodyLo);
    result->addIncoming(m_T, bbWhile);

    curr->addIncoming(curr0, bbHead);
    curr->addIncoming(curr1, bbNext);

    innerFrst->addIncoming(innerFrst0, bbHead);
    innerFrst->addIncoming(innerFrst1, bbNext);

    inside->addIncoming(m_F, bbHead);
    inside->addIncoming(inside1, bbNext);
}

llvm::Value*    CompileExprImpl::getNext(llvm::Value* curr)
{
    return  m_builder->CreateGEP(m_propType, curr, m_p1, "next");
}

llvm::Value*    CompileExprImpl::getPrev(llvm::Value* curr)
{
    return  m_builder->CreateGEP(m_propType, curr, m_m1, "prev");
}

llvm::Value*    CompileExprImpl::getTime(llvm::Value* curr, std::string name)
{
    return  m_builder->CreateLoad(m_builder->getInt64Ty(), m_builder->CreateStructGEP(m_propType, curr, 0), false, name);
}

llvm::Value*    CompileExprImpl::getPropPtr(llvm::Value* var)
{
    return m_builder->CreateLoad(m_propPtrType, var);
}

llvm::Value*    CompileExprImpl::setPropPtr(llvm::Value* var, llvm::Value* val)
{
    return m_builder->CreateStore(val, var);
}

llvm::Value*    CompileExprImpl::getBool(llvm::Value* var)
{
    return m_builder->CreateLoad(m_boolType, var);
}

llvm::Value*    CompileExprImpl::setBool(llvm::Value* var, llvm::Value* val)
{
    return m_builder->CreateStore(val, var);
}

llvm::Value*    CompileExprImpl::make(Expr* expr)
{
    auto    save    = m_value;

    expr->accept(*this);

    auto    result  = m_value;
    m_value = save;

    return  result;
}

llvm::Value*    CompileExprImpl::make(Spec* spec)
{
    auto    save    = m_value;

    spec->accept(*this);

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
        confTypes.push_back(make(context, module, type, name));
    }
    auto    confType    = llvm::StructType::create(*context, confTypes, "__conf_t");
    auto    confPtrType = llvm::PointerType::get(*context, 0);
    module->getOrInsertGlobal("__conf__", confType);

    //  create __prop__
    auto    propNames   = refmod->getPropNames();
    std::vector<llvm::Type*>    propTypes;
    propTypes.push_back(builder->getInt64Ty()); //  __time__
    for(auto name: propNames)
    {
        if(name == "__time__")
            continue;

        (void)refmod->getProp(name);            // keep semantic types reachable
        propTypes.push_back(llvm::PointerType::get(*context, 0));
    }
    auto    propType    = llvm::StructType::create(*context, propTypes, "__prop_t");
    auto    propPtrType = llvm::PointerType::get(*context, 0);
    module->getOrInsertGlobal("__prop__", propPtrType);

    auto    exprs   = refmod->getExprs();
    for(auto expr: exprs)
    {
        auto    pos         = expr->where();
        auto    funcName    = std::to_string(pos.beg.row) + ":" + std::to_string(pos.beg.col) + " .. " + std::to_string(pos.end.row) + ":" + std::to_string(pos.end.col);
        auto    funcType    = llvm::FunctionType::get(builder->getInt1Ty(), {propPtrType, propPtrType, confPtrType}, false);
        auto    funcBody    = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, funcName, module);
        auto    funcArgs    = funcBody->args().begin();

        funcArgs->setName("frst");  funcArgs++;
        funcArgs->setName("last");  funcArgs++;
        funcArgs->setName("conf");

        auto    bb          = llvm::BasicBlock::Create(*context, "entry", funcBody);
        builder->SetInsertPoint(bb);

        CompileExprImpl compExpr(context, module, builder.get(), funcBody, refmod, propType, confType);

        auto    temp        = Rewrite::make(expr);
        TypeCalc::make(refmod, temp);
        builder->CreateRet(compExpr.make(temp));
        if(!llvm::verifyFunction(*funcBody, &llvm::outs()))
        {
//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
            //throw std::runtime_error(__PRETTY_FUNCTION__);
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
        }
    }

    auto    specs   = refmod->getSpecs();
    for(auto spec: specs)
    {
        auto    pos         = spec->where();
        auto    funcName    = std::to_string(pos.beg.row) + ":" + std::to_string(pos.beg.col) + " .. " + std::to_string(pos.end.row) + ":" + std::to_string(pos.end.col);
        auto    funcType    = llvm::FunctionType::get(builder->getInt1Ty(), {propPtrType, propPtrType, confPtrType}, false);
        auto    funcBody    = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, funcName, module);
        auto    funcArgs    = funcBody->args().begin();

        funcArgs->setName("frst");  funcArgs++;
        funcArgs->setName("last");  funcArgs++;
        funcArgs->setName("conf");

        auto    bb          = llvm::BasicBlock::Create(*context, "entry", funcBody);
        builder->SetInsertPoint(bb);

        CompileExprImpl compExpr(context, module, builder.get(), funcBody, refmod, propType, confType);

        builder->CreateRet(compExpr.make(spec));

        if(!llvm::verifyFunction(*funcBody, &llvm::outs()))
        {
//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
            //throw std::runtime_error(__PRETTY_FUNCTION__);
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
        }
    }
}
