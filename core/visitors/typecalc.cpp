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

#include "typecalc.hpp"
#include "../factory.hpp"

#include <exception>

struct TypeCalcImpl
    : Visitor< ExprConstBoolean
             , ExprConstInteger
             , ExprConstNumber
             , ExprConstString
             , ExprContext
             , ExprConf
             , ExprParen
             , ExprData
             , ExprMmbr
             , ExprIndx
             , ExprInt
             , ExprEq
             , ExprNe
             , ExprGt
             , ExprGe
             , ExprLt
             , ExprLe
             , ExprNot
             , ExprOr
             , ExprAnd
             , ExprXor
             , ExprImp
             , ExprEqu
             , ExprAdd
             , ExprSub
             , ExprDiv
             , ExprMul
             , ExprMod
             , Temporal<ExprUnary>
             , Temporal<ExprBinary>
             , ExprXs
             , ExprXw
             , ExprYs
             , ExprYw
             , Time
             , ExprAt
             , SpecUniversality
             , SpecAbsence
             , SpecExistence
             , SpecTransientState
             , SpecSteadyState
             , SpecMinimunDuration
             , SpecMaximumDuration
             , SpecRecurrence
             , SpecPrecedence
             , SpecPrecedenceChain12
             , SpecPrecedenceChain21
             , SpecResponse
             , SpecResponseChain12
             , SpecResponseChain21
             , SpecResponseInvariance
             , SpecUntil
             , SpecGlobally
             , SpecBefore
             , SpecAfter
             , SpecBetweenAnd
             , SpecAfterUntil>
{
    TypeCalcImpl(Module* module)
        : m_module(module)
    {
    }
    
    Type*   m_type  = nullptr;

    Type*   make(Expr*          expr, Type* type = nullptr);
    Type*   make(Spec*          spec);
    Type*   safe(Expr*          expr, Type* type = nullptr);
    Type*   safe(Time*          time);

    void    visit(ExprAdd*              expr) override;
    void    visit(ExprAnd*              expr) override;
    void    visit(ExprAt*               expr) override;
    void    visit(ExprConstBoolean*     expr) override;
    void    visit(ExprConstInteger*     expr) override;
    void    visit(ExprConstNumber*      expr) override;
    void    visit(ExprConstString*      expr) override;
    void    visit(ExprContext*          expr) override;
    void    visit(ExprData*             expr) override;
    void    visit(ExprConf*             expr) override;
    void    visit(ExprDiv*              expr) override;
    void    visit(ExprEq*               expr) override;
    void    visit(ExprEqu*              expr) override;
    void    visit(ExprGe*               expr) override;
    void    visit(ExprGt*               expr) override;
    void    visit(ExprImp*              expr) override;
    void    visit(ExprLe*               expr) override;
    void    visit(ExprLt*               expr) override;
    void    visit(ExprMmbr*             expr) override;
    void    visit(ExprIndx*             expr) override;
    void    visit(ExprInt*              expr) override;
    void    visit(ExprMod*              expr) override;
    void    visit(ExprMul*              expr) override;
    void    visit(ExprNe*               expr) override;
    void    visit(ExprNot*              expr) override;
    void    visit(ExprOr*               expr) override;
    void    visit(ExprParen*            expr) override;
    void    visit(ExprSub*              expr) override;
    void    visit(ExprXor*              expr) override;
    void    visit(Temporal<ExprBinary>* expr) override;
    void    visit(Temporal<ExprUnary>*  expr) override;
    void    visit(Time*                 time) override;
    void    visit(ExprXs*               expr) override;
    void    visit(ExprXw*               expr) override;
    void    visit(ExprYs*               expr) override;
    void    visit(ExprYw*               expr) override;
    void    visit(SpecUniversality*         spec)  override;
    void    visit(SpecAbsence*              spec)  override;
    void    visit(SpecExistence*            spec)  override;
    void    visit(SpecTransientState*       spec)  override;
    void    visit(SpecSteadyState*          spec)  override;
    void    visit(SpecMinimunDuration*      spec)  override;
    void    visit(SpecMaximumDuration*      spec)  override;
    void    visit(SpecRecurrence*           spec)  override;
    void    visit(SpecPrecedence*           spec)  override;
    void    visit(SpecPrecedenceChain12*    spec)  override;
    void    visit(SpecPrecedenceChain21*    spec)  override;
    void    visit(SpecResponse*             spec)  override;
    void    visit(SpecResponseChain12*      spec)  override;
    void    visit(SpecResponseChain21*      spec)  override;
    void    visit(SpecResponseInvariance*   spec)  override;
    void    visit(SpecUntil*                spec)  override;
    void    visit(SpecGlobally*            spec)  override;
    void    visit(SpecBefore*              spec)  override;
    void    visit(SpecAfter*               spec)  override;
    void    visit(SpecBetweenAnd*          spec)  override;
    void    visit(SpecAfterUntil*          spec)  override;


    Type*    boolBool2Bool(  
                    Expr*       expr,
                    Expr*       lhs,
                    Expr*       rhs);
    Type*   bool2Bool(  
                    Expr*       expr,
                    Expr*       arg);
    Type*   nmbrNmbr2Bool(  
                    Expr*       expr,
                    Expr*       lhs,
                    Expr*       rhs);

    Type*   typeBoolean = Factory<TypeBoolean>::create();
    Type*   typeInteger = Factory<TypeInteger>::create();
    Type*   typeNumber  = Factory<TypeNumber>::create();
    Type*   typeString  = Factory<TypeString>::create();
    Type*   typeVoid    = Factory<TypeVoid>::create();

private:
    Module* m_module;
};

void    TypeCalcImpl::visit(ExprAdd*                expr)
{
    auto    lhs = make(expr->lhs);
    auto    rhs = make(expr->rhs);

    if(lhs != typeInteger && lhs != typeNumber && rhs != typeInteger && rhs != typeNumber)
    {
        throw Exception(expr->where(), "bad type");
    }

    if(lhs == rhs)
        m_type  = lhs;
    else 
        m_type  = typeNumber;
}

void    TypeCalcImpl::visit(ExprAnd*                expr)
{
    m_type = boolBool2Bool(expr, expr->lhs, expr->rhs);
}

void    TypeCalcImpl::visit(ExprAt*                 expr)
{
    m_type = make(expr->arg);
}

void    TypeCalcImpl::visit(ExprConstBoolean*       expr)
{
    m_type = Factory<TypeBoolean>::create();
}

void    TypeCalcImpl::visit(ExprConstInteger*       expr)
{
    m_type = Factory<TypeInteger>::create();
}

void    TypeCalcImpl::visit(ExprConstNumber*        expr)
{
    m_type = Factory<TypeNumber>::create();
}

void    TypeCalcImpl::visit(ExprConstString*        expr)
{
    m_type = Factory<TypeString>::create();
}

void    TypeCalcImpl::visit(ExprContext*            expr)
{
    m_type = Factory<TypeContext>::create(m_module);
}

void    TypeCalcImpl::visit(ExprDiv*                expr)
{
    auto    lhs = make(expr->lhs);
    auto    rhs = make(expr->rhs);

    if(lhs != typeInteger && lhs != typeNumber && rhs != typeInteger && rhs != typeNumber)
    {
        throw Exception(expr->where(), "bad type");
    }

    if(lhs == rhs)
        m_type  = lhs;
    else 
        m_type  = typeNumber;
}

void    TypeCalcImpl::visit(ExprEq*                 expr)
{
    m_type = nmbrNmbr2Bool(expr, expr->lhs, expr->rhs);
}

void    TypeCalcImpl::visit(ExprEqu*                expr)
{
    m_type = boolBool2Bool(expr, expr->lhs, expr->rhs);
}

void    TypeCalcImpl::visit(ExprGe*                 expr)
{
    m_type = nmbrNmbr2Bool(expr, expr->lhs, expr->rhs);
}

void    TypeCalcImpl::visit(ExprGt*                 expr)
{
    m_type = nmbrNmbr2Bool(expr, expr->lhs, expr->rhs);
}

void    TypeCalcImpl::visit(ExprImp*                expr)
{
    m_type = boolBool2Bool(expr, expr->lhs, expr->rhs);
}

void    TypeCalcImpl::visit(ExprLe*                 expr)
{
    m_type = nmbrNmbr2Bool(expr, expr->lhs, expr->rhs);
}

void    TypeCalcImpl::visit(ExprLt*                 expr)
{
    m_type = nmbrNmbr2Bool(expr, expr->lhs, expr->rhs);
}

void    TypeCalcImpl::visit(ExprMmbr*               expr)
{
    auto    base    = dynamic_cast<TypeComposite*>(make(expr->arg));

    if(base == nullptr)
    {
        throw Exception(expr->where(), "bad type");
    }

    m_type  = base->member(expr->mmbr);

    if(m_type == nullptr)
    {
        throw Exception(expr->where(), "no such a member");
    }
}

void    TypeCalcImpl::visit(ExprInt*              expr)
{
    auto    lhs = make(expr->lhs);
    auto    rhs = make(expr->rhs);

    if(lhs != typeBoolean)
    {
        throw Exception(expr->lhs->where(), "bad type");
    }

    if(rhs != typeInteger && rhs != typeNumber)
    {
        throw Exception(expr->rhs->where(), "bad type");
    }

    m_type  = rhs;
}

void    TypeCalcImpl::visit(ExprIndx*               expr)
{
    auto    lhs     = make(expr->lhs);
    auto    rhs     = make(expr->rhs);
    auto    base    = dynamic_cast<TypeArray*>(lhs);

    if(base == nullptr)
    {
        throw Exception(expr->where(), "bad type");
    }

    if(rhs != Factory<TypeInteger>::create())
    {
        throw Exception(expr->where(), "index should be integer");
    }

    m_type  = base->type;
}


void    TypeCalcImpl::visit(ExprData*               expr)
{
    auto    ctxt    = dynamic_cast<TypeComposite*>(make(expr->ctxt));

    if(ctxt == nullptr)
    {
        throw Exception(expr->where(), "bad type");
    }

    m_type  = ctxt->member(expr->name);

    if(m_type == nullptr)
    {
        throw Exception(expr->where(), "no such a member");
    }
}

void    TypeCalcImpl::visit(ExprConf*               expr)
{
    auto    ctxt    = Factory<TypeContext>::create(m_module);

    m_type  = ctxt->member(expr->name);

    if(m_type == nullptr)
    {
        throw Exception(expr->where(), "no such a member");
    }
}

void    TypeCalcImpl::visit(ExprMod*                expr)
{
    auto    lhs = make(expr->lhs);
    auto    rhs = make(expr->rhs);

    if(lhs != typeInteger && rhs != typeInteger)
    {
        throw Exception(expr->where(), "bad type");
    }

    m_type  = typeInteger;
}


void    TypeCalcImpl::visit(ExprMul*                expr)
{
    auto    lhs = make(expr->lhs);
    auto    rhs = make(expr->rhs);

    if(lhs != typeInteger && lhs != typeNumber && rhs != typeInteger && rhs != typeNumber)
    {
        throw Exception(expr->where(), "bad type");
    }

    if(lhs == rhs)
        m_type  = lhs;
    else 
        m_type  = typeNumber;
}

void    TypeCalcImpl::visit(ExprNe*                 expr)
{
    m_type = nmbrNmbr2Bool(expr, expr->lhs, expr->rhs);
}

void    TypeCalcImpl::visit(ExprNot*                expr)
{
    m_type = bool2Bool(expr, expr->arg);
}

void    TypeCalcImpl::visit(ExprOr*                 expr)
{
    m_type = boolBool2Bool(expr, expr->lhs, expr->rhs);
}

void    TypeCalcImpl::visit(ExprParen*              expr)
{
    m_type  = make(expr->arg);
}

void    TypeCalcImpl::visit(ExprSub*                expr)
{
    auto    lhs = make(expr->lhs);
    auto    rhs = make(expr->rhs);

    if(lhs != typeInteger && lhs != typeNumber && rhs != typeInteger && rhs != typeNumber)
    {
        throw Exception(expr->where(), "bad type");
    }

    if(lhs == rhs)
        m_type  = lhs;
    else 
        m_type  = typeNumber;
}

void    TypeCalcImpl::visit(ExprXor*                expr)
{   
    m_type = boolBool2Bool(expr, expr->lhs, expr->rhs);
}

void    TypeCalcImpl::visit(Temporal<ExprBinary>*   expr)
{
    safe(expr->time);

    m_type = boolBool2Bool(expr, expr->lhs, expr->rhs);
}

void    TypeCalcImpl::visit(Temporal<ExprUnary>*    expr)
{
    safe(expr->time);

    m_type = bool2Bool(expr, expr->arg);
}


void    TypeCalcImpl::visit(Time*                   time)
{
    auto    lo  = time->lo ? TypeCalcImpl::make(time->lo) : typeInteger;
    auto    hi  = time->hi ? TypeCalcImpl::make(time->hi) : typeInteger;
    
    if (lo != typeInteger)
        throw std::runtime_error(__PRETTY_FUNCTION__);

    if (hi != typeInteger)
        throw std::runtime_error(__PRETTY_FUNCTION__);

    m_type = typeVoid;
}

void    TypeCalcImpl::visit(ExprXs*               expr)
{
    auto    lhs = make(expr->lhs);
    auto    rhs = make(expr->rhs);

    if (lhs != typeInteger)
        throw std::runtime_error(__PRETTY_FUNCTION__);

    if (rhs != typeBoolean)
        throw std::runtime_error(__PRETTY_FUNCTION__);

    m_type = typeBoolean;
}

void    TypeCalcImpl::visit(ExprXw*               expr)
{
    auto    lhs = make(expr->lhs);
    auto    rhs = make(expr->rhs);

    if (lhs != typeInteger)
        throw std::runtime_error(__PRETTY_FUNCTION__);

    if (rhs != typeBoolean)
        throw std::runtime_error(__PRETTY_FUNCTION__);

    m_type = typeBoolean;
}

void    TypeCalcImpl::visit(ExprYs*               expr)
{
    auto    lhs = make(expr->lhs);
    auto    rhs = make(expr->rhs);

    if (lhs != typeInteger)
        throw std::runtime_error(__PRETTY_FUNCTION__);

    if (rhs != typeBoolean)
        throw std::runtime_error(__PRETTY_FUNCTION__);

    m_type = typeBoolean;
}

void    TypeCalcImpl::visit(ExprYw*               expr)
{
    auto    lhs = make(expr->lhs);
    auto    rhs = make(expr->rhs);

    if (lhs != typeInteger)
        throw std::runtime_error(__PRETTY_FUNCTION__);

    if (rhs != typeBoolean)
        throw std::runtime_error(__PRETTY_FUNCTION__);

    m_type = typeBoolean;
}

Type*   TypeCalcImpl::boolBool2Bool(  
                    Expr*       expr,
                    Expr*       lhs,
                    Expr*       rhs)
{
    auto    typeLhs = make(lhs);    
    auto    typeRhs = make(rhs);

    if(typeLhs != typeBoolean || typeRhs != typeBoolean)
    {
        throw Exception(expr->where(), "bad type");
    }

    return  typeBoolean;
}

Type*   TypeCalcImpl::bool2Bool(  
                    Expr*       expr,
                    Expr*       arg)
{
    auto    typeArg = make(arg);    

    if(typeArg != typeBoolean)
    {
        throw Exception(expr->where(), "bad type");
    }

    return  typeBoolean;
}

Type*   TypeCalcImpl::nmbrNmbr2Bool(  
                    Expr*       expr,
                    Expr*       lhs,
                    Expr*       rhs)
{
    auto    typeLhs = make(lhs);    
    auto    typeRhs = make(rhs);

    if(     (typeLhs == typeInteger || typeLhs == typeNumber)
        &&  (typeRhs == typeInteger || typeRhs == typeNumber)
        ||  (typeLhs == typeBoolean && typeRhs == typeBoolean)
        ||  (typeLhs == typeString  && typeRhs == typeString))
        
    {
        return  typeBoolean;
    }
    
    throw Exception(expr->where(), "bad type");
}

void    TypeCalcImpl::visit(SpecUniversality*         spec)
{
    make(spec->P, typeBoolean);
    safe(spec->tP);
}

void    TypeCalcImpl::visit(SpecAbsence*              spec)
{
    make(spec->P, typeBoolean);
    safe(spec->tP);
}

void    TypeCalcImpl::visit(SpecExistence*            spec)
{
    make(spec->P, typeBoolean);
    safe(spec->tP);
}

void    TypeCalcImpl::visit(SpecTransientState*       spec)
{
    make(spec->P, typeBoolean);
    safe(spec->tP);
}

void    TypeCalcImpl::visit(SpecSteadyState*          spec)
{
    make(spec->P, typeBoolean);
}

void    TypeCalcImpl::visit(SpecMinimunDuration*      spec)
{
    make(spec->P, typeBoolean);
    safe(spec->tP);
}

void    TypeCalcImpl::visit(SpecMaximumDuration*      spec)
{
    make(spec->P, typeBoolean);
    safe(spec->tP);
}

void    TypeCalcImpl::visit(SpecRecurrence*           spec)
{
    make(spec->P, typeBoolean);
    safe(spec->tP);
}

void    TypeCalcImpl::visit(SpecPrecedence*           spec)
{
    make(spec->P, typeBoolean);
    make(spec->S, typeBoolean);
    safe(spec->tPS);
}

void    TypeCalcImpl::visit(SpecPrecedenceChain12*    spec)
{
    make(spec->P, typeBoolean);
    make(spec->S, typeBoolean);
    make(spec->T, typeBoolean);
    safe(spec->tST);
    safe(spec->tPS);
}

void    TypeCalcImpl::visit(SpecPrecedenceChain21*    spec)
{
    make(spec->P, typeBoolean);
    make(spec->S, typeBoolean);
    make(spec->T, typeBoolean);
    safe(spec->tST);
    safe(spec->tPS);
}

void    TypeCalcImpl::visit(SpecResponse*             spec)
{
    make(spec->P, typeBoolean);
    make(spec->S, typeBoolean);
    safe(spec->cPS, typeBoolean);
    safe(spec->tPS);
}

void    TypeCalcImpl::visit(SpecResponseChain12*      spec)
{
    make(spec->P, typeBoolean);
    make(spec->S, typeBoolean);
    make(spec->T, typeBoolean);
    safe(spec->cST, typeBoolean);
    safe(spec->tST);
    safe(spec->cPS, typeBoolean);
    safe(spec->tPS);
}

void    TypeCalcImpl::visit(SpecResponseChain21*      spec)
{
    make(spec->P, typeBoolean);
    make(spec->S, typeBoolean);
    make(spec->T, typeBoolean);
    safe(spec->cST, typeBoolean);
    safe(spec->tST);
    safe(spec->cTP, typeBoolean);
    safe(spec->tTP);
}

void    TypeCalcImpl::visit(SpecResponseInvariance*   spec)
{
    make(spec->P, typeBoolean);
    make(spec->S, typeBoolean);
    safe(spec->tPS);
}

void    TypeCalcImpl::visit(SpecUntil*                spec)
{
    make(spec->P, typeBoolean);
    make(spec->S, typeBoolean);
    safe(spec->tPS);
}

void    TypeCalcImpl::visit(SpecGlobally*            spec)
{
    make(spec->spec);
}

void    TypeCalcImpl::visit(SpecBefore*              spec)
{
    make(spec->arg);
    make(spec->spec);
}

void    TypeCalcImpl::visit(SpecAfter*               spec)
{
    make(spec->arg);
    make(spec->spec);
}

void    TypeCalcImpl::visit(SpecBetweenAnd*          spec)
{
    make(spec->lhs);
    make(spec->rhs);
    make(spec->spec);
}

void    TypeCalcImpl::visit(SpecAfterUntil*          spec)
{
    make(spec->lhs);
    make(spec->rhs);
    make(spec->spec);
}

Type*   TypeCalcImpl::make(Expr* expr, Type* type)
{
    if(expr->type() == nullptr)
    {
        auto    save    = m_type;

        m_type = nullptr;
        expr->accept(*this);
        expr->type(m_type);
    
        m_type  = save;
    }
    
    if(type != nullptr)
    {
        if(type != expr->type())
        {
            throw Exception(expr->where(), "bad type");   
        }
    }

    return expr->type();
}

Type*   TypeCalcImpl::make(Spec* spec)
{
    auto    save    = m_type;

    m_type = nullptr;
    spec->accept(*this);

    m_type  = save;
    
    return Factory<TypeBoolean>::create();
}

Type*   TypeCalcImpl::safe(Expr*          expr, Type* type)
{
    if(expr != nullptr)
    {
        return make(expr, type);
    }

    return  nullptr;
}

Type*   TypeCalcImpl::safe(Time*          time)
{
    if(time != nullptr)
    {
        return make(time);
    }
    return  nullptr;
}

Type*   TypeCalc::make(Module* module, Expr* expr)
{
    TypeCalcImpl impl(module);

    return  impl.make(expr);
}

Type*   TypeCalc::make(Module* module, Spec* spec)
{
    TypeCalcImpl impl(module);

    return  impl.make(spec);
}
