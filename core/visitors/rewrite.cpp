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

#include "rewrite.hpp"
#include "negated.hpp"
#include "canonic.hpp"
#include "../factory.hpp"
#include "../builder.hpp"

#include <exception>
#include <assert.h>

struct RewriteImpl
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
             , ExprF
             , ExprG
             , ExprGe
             , ExprGt
             , ExprH
             , ExprImp
             , ExprIndx
             , ExprInt
             , ExprSum
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
             , ExprCall
             , ExprBand
             , ExprBor
             , ExprShl
             , ExprShr
             , ExprBnot
             , ExprXs
             , ExprXw
             , ExprYs
             , ExprYw
             , Time
             , TimeMin
             , TimeMax
             
             , Spec
             , SpecAbsence
             , SpecExistence
             , SpecMaximumDuration
             , SpecMinimunDuration
             , SpecPrecedence
             , SpecRecurrence
             , SpecResponse
             , SpecResponseChain12
             , SpecResponseChain21
             , SpecResponseInvariance
             , SpecUniversality
             , SpecUntil>

{
public:
    Expr*   make(   Expr*           expr,
                    std::string     curr = "__curr__");
    Time*   make(   Time*           time,
                    std::string     curr = "__curr__");
    Expr*   make(   Spec*           spec,
                    std::string     curr = "__curr__");

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
    void    visit(ExprConf*         expr) override;
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
    void    visit(ExprSum*          expr) override;
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
    void    visit(ExprCall*         expr) override;
    void    visit(ExprBand*      expr) override;
    void    visit(ExprBor*       expr) override;
    void    visit(ExprShl*       expr) override;
    void    visit(ExprShr*       expr) override;
    void    visit(ExprBnot*      expr) override;
    void    visit(ExprXs*           expr) override;
    void    visit(ExprXw*           expr) override;
    void    visit(ExprYs*           expr) override;
    void    visit(ExprYw*           expr) override;

    void    visit(Time*             expr) override;
    void    visit(TimeMin*          expr) override;
    void    visit(TimeMax*          expr) override;

    void    visit(Spec*                     spec) override;
    void    visit(SpecAbsence*              spec) override;
    void    visit(SpecExistence*            spec) override;
    void    visit(SpecMaximumDuration*      spec) override;
    void    visit(SpecMinimunDuration*      spec) override;
    void    visit(SpecPrecedence*           spec) override;
    void    visit(SpecRecurrence*           spec) override;
    void    visit(SpecResponse*             spec) override;
    void    visit(SpecResponseChain12*      spec) override;
    void    visit(SpecResponseChain21*      spec) override;
    void    visit(SpecResponseInvariance*   spec) override;
    void    visit(SpecUniversality*         spec) override;
    void    visit(SpecUntil*                spec) override;

    Expr*   canonic(Expr* expr);
    Expr*   negated(Expr* expr);

private:
    Expr*       m_expr  = nullptr;
    Time*       m_time  = nullptr;
    std::string m_bind  = "__curr__";
};

void    RewriteImpl::visit( ExprAdd*            expr)
{
    m_expr =  Factory<ExprAdd>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->rhs));
}

void    RewriteImpl::visit( ExprAnd*            expr)
{
    m_expr =  Factory<ExprAnd>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->rhs));
}

void    RewriteImpl::visit( ExprAt*             expr) 
{
    m_expr =  Factory<ExprAt>::create(
        expr->where(),
        expr->name,
        make(expr->arg));
}

void    RewriteImpl::visit( ExprChoice*         expr) 
{
    m_expr =  Factory<ExprChoice>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->mhs),
        make(expr->rhs));
}

void    RewriteImpl::visit( ExprConstBoolean*   expr) 
{
    m_expr =  Factory<ExprConstBoolean>::create(
        expr->where(),
        expr->value);
}

void    RewriteImpl::visit( ExprConstInteger*   expr) 
{
    m_expr =  Factory<ExprConstInteger>::create(
        expr->where(),
         expr->value);
}

void    RewriteImpl::visit( ExprConstNumber*    expr) 
{
    m_expr =  Factory<ExprConstNumber>::create(
        expr->where(),
        expr->value);
}

void    RewriteImpl::visit( ExprConstString*    expr)
{
    m_expr =  Factory<ExprConstString>::create(
        expr->where(),
        expr->value);
}

void    RewriteImpl::visit( ExprContext*        expr)
{
    if(expr->name == "__curr__")
    {
        m_expr =  Factory<ExprContext>::create(expr->where(), m_bind);
    }
    else
    {
        m_expr =  Factory<ExprContext>::create(expr->where(), expr->name);
    }

}

void    RewriteImpl::visit( ExprData*           expr)
{
    if(expr->ctxt->name == "__curr__" && expr->ctxt->name != m_bind)
    {
        m_expr =  Factory<ExprData>::create(
            expr->where(),
            Factory<ExprContext>::create(m_bind),
            expr->name);
    }
    else
    {
        m_expr =  Factory<ExprData>::create(
            expr->where(),
            expr->ctxt,
            expr->name);
    }
}

void    RewriteImpl::visit( ExprConf*           expr)
{
    m_expr =  Factory<ExprConf>::create(
        expr->where(),
        expr->ctxt,
        expr->name);
}

void    RewriteImpl::visit( ExprDiv*            expr)
{
    m_expr =  Factory<ExprDiv>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->rhs));
}

void    RewriteImpl::visit( ExprEq*             expr)
{
    m_expr =  Factory<ExprEq>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->rhs));
}

void    RewriteImpl::visit( ExprEqu*            expr)
{
    m_expr =  Factory<ExprEqu>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->rhs));
}

void    RewriteImpl::visit( ExprF*              expr)
{
    m_expr =  Factory<ExprF>::create(
        expr->where(),
        make(expr->time),
        make(expr->arg));
}

void    RewriteImpl::visit( ExprG*              expr) 
{
    m_expr =  Factory<ExprG>::create(
        expr->where(),
        make(expr->time),
        make(expr->arg));
}

void    RewriteImpl::visit( ExprGe*             expr)
{
    m_expr =  Factory<ExprGe>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->rhs));
}

void    RewriteImpl::visit( ExprGt*             expr) 
{
    m_expr =  Factory<ExprGt>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->rhs));
}

void    RewriteImpl::visit( ExprH*              expr)
{
    m_expr =  Factory<ExprH>::create(
        expr->where(),
        make(expr->time),
        make(expr->arg));
}

void    RewriteImpl::visit( ExprImp*            expr) 
{
    m_expr =  Factory<ExprImp>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->rhs));
}

void    RewriteImpl::visit( ExprIndx*           expr) 
{
    m_expr =  Factory<ExprIndx>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->rhs));
}

void    RewriteImpl::visit( ExprSum*            expr)
{
    if(expr->time)
    {
        m_expr =  Factory<ExprSum>::create(
            expr->where(),
            make(expr->time),
            make(expr->lhs),
            make(expr->rhs));
    }
    else
    {
        m_expr =  Factory<ExprSum>::create(
            expr->where(),
            make(expr->lhs),
            make(expr->rhs));
    }
}

void    RewriteImpl::visit( ExprInt*            expr)
{
    if(expr->time)
    {
        m_expr =  Factory<ExprInt>::create(
            expr->where(),
            make(expr->time),
            make(expr->lhs),
            make(expr->rhs));
    }
    else
    {
        m_expr =  Factory<ExprInt>::create(
            expr->where(),
            make(expr->lhs),
            make(expr->rhs));
    }
}

void    RewriteImpl::visit( ExprLe*             expr)
{
    m_expr =  Factory<ExprLe>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->rhs));
}

void    RewriteImpl::visit( ExprLt*             expr) 
{
    m_expr =  Factory<ExprLt>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->rhs));
}

void    RewriteImpl::visit( ExprMmbr*           expr) 
{
    m_expr =  Factory<ExprMmbr>::create(
        expr->where(),
        make(expr->arg),
        expr->mmbr);
}

void    RewriteImpl::visit( ExprMod*            expr)
{
    m_expr =  Factory<ExprMod>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->rhs));
}

void    RewriteImpl::visit( ExprMul*            expr)
{
    m_expr =  Factory<ExprMul>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->rhs));
}

void    RewriteImpl::visit( ExprNe*             expr)
{
    m_expr =  Factory<ExprNe>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->rhs));
}

void    RewriteImpl::visit( ExprNeg*            expr) 
{
    m_expr =  Factory<ExprNeg>::create(
        expr->where(),
        make(expr->arg));
}

void    RewriteImpl::visit( ExprNot*            expr)
{
    m_expr =  negated(
        make(expr->arg));
}

void    RewriteImpl::visit( ExprO*              expr) 
{
    m_expr =  Factory<ExprO>::create(
        expr->where(),
        make(expr->time),
        make(expr->arg));
}

void    RewriteImpl::visit( ExprOr*             expr)
{
    m_expr =  Factory<ExprOr>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->rhs));
}


void    RewriteImpl::visit( ExprParen*          expr) 
{
    auto    arg     = make(expr->arg);

    if(     dynamic_cast<ExprParen*>(arg) 
        ||  dynamic_cast<ExprNullary*>(arg)
        ||  dynamic_cast<ExprContext*>(arg)
        ||  dynamic_cast<ExprData*>(arg)
        ||  dynamic_cast<ExprAt*>(arg)
        ||  dynamic_cast<ExprMmbr*>(arg)
        ||  dynamic_cast<Temporal<ExprUnary>*>(arg)
        ||  dynamic_cast<Temporal<ExprBinary>*>(arg))
    {
        m_expr = make(arg);
    }
    else 
    {
        m_expr = Factory<ExprParen>::create(
            expr->where(),
            arg);
    }
}

void    RewriteImpl::visit( ExprRs*             expr)
{
    if(expr->time)
    {
        m_expr =  Factory<ExprRs>::create(
            expr->where(),
            make(expr->time),
            make(expr->lhs),
            make(expr->rhs));    }
    else 
    {
        m_expr =  Factory<ExprRs>::create(
            expr->where(),
            make(expr->lhs),
            make(expr->rhs));
    }
}

void    RewriteImpl::visit( ExprRw*             expr)
{
    if(expr->time)
    {
        m_expr =  Factory<ExprRw>::create(
            expr->where(),
            make(expr->time),
            make(expr->lhs),
            make(expr->rhs));    }
    else 
    {
        m_expr =  Factory<ExprRw>::create(
            expr->where(),
            make(expr->lhs),
            make(expr->rhs));
    }
}

void    RewriteImpl::visit( ExprSs*             expr)
{
    m_expr =  Factory<ExprSs>::create(
        expr->where(),
        make(expr->time),
        make(expr->lhs),
        make(expr->rhs));

}

void    RewriteImpl::visit( ExprSub*            expr)
{
    m_expr =  Factory<ExprSub>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->rhs));
}


void    RewriteImpl::visit( ExprSw*             expr)
{
    m_expr =  Factory<ExprSw>::create(
        expr->where(),
        make(expr->time),
        make(expr->lhs),
        make(expr->rhs));

}

void    RewriteImpl::visit( ExprTs*             expr)
{
    m_expr =  Factory<ExprTs>::create(
        expr->where(),
        make(expr->time),
        make(expr->lhs),
        make(expr->rhs));    

}

void    RewriteImpl::visit( ExprTw*             expr)
{
    m_expr =  Factory<ExprTw>::create(
        expr->where(),
        make(expr->time),
        make(expr->lhs),
        make(expr->rhs));
}

void    RewriteImpl::visit( ExprUs*             expr) 
{
    m_expr =  Factory<ExprUs>::create(
        expr->where(),
        make(expr->time),
        make(expr->lhs),
        make(expr->rhs));

}

void    RewriteImpl::visit( ExprUw*             expr)
{
    m_expr =  Factory<ExprUw>::create(
        expr->where(),
        make(expr->time),
        make(expr->lhs),
        make(expr->rhs));

}

void    RewriteImpl::visit( ExprCall*           expr)
{
    std::vector<Expr*>  args;

    for(auto* arg: expr->args)
        args.push_back(make(arg));

    m_expr = Factory<ExprCall>::create(expr->where(), expr->name, args);
}

void    RewriteImpl::visit( ExprXor*            expr) 
{
    m_expr =  Factory<ExprXor>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->rhs));
}

void    RewriteImpl::visit( ExprBand*        expr)
{
    m_expr =  Factory<ExprBand>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->rhs));
}

void    RewriteImpl::visit( ExprBor*         expr)
{
    m_expr =  Factory<ExprBor>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->rhs));
}

void    RewriteImpl::visit( ExprShl*         expr)
{
    m_expr =  Factory<ExprShl>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->rhs));
}

void    RewriteImpl::visit( ExprShr*         expr)
{
    m_expr =  Factory<ExprShr>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->rhs));
}

void    RewriteImpl::visit( ExprBnot*        expr)
{
    m_expr =  Factory<ExprBnot>::create(
        expr->where(),
        make(expr->arg));
}


void    RewriteImpl::visit( ExprXs*             expr) 
{
    m_expr =  Factory<ExprXs>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->rhs));
}

void    RewriteImpl::visit( ExprXw*             expr)
{
    m_expr =  Factory<ExprXw>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->rhs));
}

void    RewriteImpl::visit( ExprYs*             expr)
{
    m_expr =  Factory<ExprYs>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->rhs));
}

void    RewriteImpl::visit( ExprYw*             expr)
{
    m_expr =  Factory<ExprYw>::create(
        expr->where(),
        make(expr->lhs),
        make(expr->rhs));
}

void    RewriteImpl::visit( Time*               expr)
{
    m_time = Factory<Time>::create(
        expr->where(),
        make(expr->lo),
        make(expr->hi));
}

void    RewriteImpl::visit(TimeMin*             expr)
{
    m_time = Factory<TimeMin>::create(
        expr->where(),
        make(expr->lo));
}

void    RewriteImpl::visit(TimeMax*                 expr)
{
    m_time = Factory<TimeMax>::create(
        expr->where(),
        make(expr->hi));
}

void    RewriteImpl::visit( Spec*                   spec)
{
    m_expr  = Factory<ExprConstBoolean>::create(true);
}

void    RewriteImpl::visit( SpecAbsence*            spec)
{
    m_expr  = G(make(spec->tP), Not(make(spec->P)));
}

void    RewriteImpl::visit( SpecExistence*          spec)
{
    m_expr  = F(make(spec->tP), make(spec->P));
}

void    RewriteImpl::visit( SpecMaximumDuration*    spec)
{
    auto    P   = Wrapper(make(spec->P));
    auto    tP  = make(spec->tP);

    m_expr  = G(P || Uw(!P, P && F(tP, !P)));
}

void    RewriteImpl::visit( SpecMinimunDuration*    spec)
{
    auto    P   = Wrapper(make(spec->P));
    auto    tP  = make(spec->tP);

    m_expr  = G(P || Uw(!P, G(tP, P)));
}

void    RewriteImpl::visit( SpecPrecedence*         spec)
{
    auto    P   = Wrapper(make(spec->P));
    auto    S   = Wrapper(make(spec->S));
    auto    tPS = make(spec->tPS);

    m_expr  = G(P >> O(tPS, S));
}

void    RewriteImpl::visit( SpecRecurrence*         spec)
{
    m_expr  = G(F(make(spec->tP), make(spec->P)));
}

void    RewriteImpl::visit( SpecResponse*           spec)
{
    auto    P   = Wrapper(make(spec->P));
    auto    S   = Wrapper(make(spec->S));
    auto    Z   = Wrapper(make(spec->cPS));
    auto    tPS = make(spec->tPS);

    m_expr  = G(P >> Us(tPS, !Z, S));
}

void    RewriteImpl::visit(SpecResponseChain12*     spec)
{
    auto    P   = Wrapper(make(spec->P));
    auto    S   = Wrapper(make(spec->S));
    auto    T   = Wrapper(make(spec->T));
    auto    tPS = make(spec->tPS);
    auto    tST = make(spec->tST);
    auto    cPS = Wrapper(make(spec->cPS));
    auto    cST = Wrapper(make(spec->cST));

    m_expr  = G(P >> (Us(tPS, !cPS, S && !cST && Xs(Us(tST, !cST, T)))));   //  TODO: do we need this Xs ???
}

void    RewriteImpl::visit(SpecResponseChain21*     spec)
{
    auto    S   = Wrapper(make(spec->S));
    auto    T   = Wrapper(make(spec->T));
    auto    P   = Wrapper(make(spec->P));
    auto    tST = make(spec->tST);
    auto    tTP = make(spec->tTP);
    auto    cST = Wrapper(make(spec->cST));
    auto    cTP = Wrapper(make(spec->cTP));

    m_expr  = G((S && !cST && Xs(Us(tTP, !cST, T && !cTP))) >> Xs(Us(tST, !cST, T && Us(tTP, !cTP, P))));   //  TODO: do we need this Xs ???
}

void    RewriteImpl::visit( SpecResponseInvariance* spec)
{
    auto    P   = Wrapper(make(spec->P));
    auto    S   = Wrapper(make(spec->S));
    auto    tPS = make(spec->tPS);

    m_expr  = G(P >> G(tPS, S));
}

void    RewriteImpl::visit( SpecUniversality*       spec)
{
    m_expr  = G(make(spec->tP), make(spec->P));
}

void    RewriteImpl::visit( SpecUntil*              spec)
{
    auto    P   = Wrapper(make(spec->P));
    auto    S   = Wrapper(make(spec->S));
    auto    tPS = make(spec->tPS);

    m_expr  = Us(tPS, P, S);
}

Expr*   RewriteImpl::make(Expr* expr, std::string curr)
{
    m_expr  = Factory<ExprConstBoolean>::create(false);

    if(expr)
    {
        if(curr != "__curr__")
        {
            assert(m_bind == "__curr__");

            m_bind  = curr;
            expr->accept(*this);
            m_bind  = "__curr__";
        }
        else
        {
            expr->accept(*this);
        }
    }    
    
    return  canonic(m_expr);
}

Time*   RewriteImpl::make(Time* time, std::string curr)
{
    if(time)
    {
        if(curr != "__curr__")
        {
            assert(m_bind == "__curr__");

            m_bind  = curr;
            time->accept(*this);
            m_bind  = "__curr__";
        }
        else
        {
            time->accept(*this);
        }        
        return  m_time;
    }
    return nullptr;
}

Expr*   RewriteImpl::make(Spec* spec, std::string curr)
{
    if(curr != "__curr__")
    {
        assert(m_bind == "__curr__");

        m_bind  = curr;
        spec->accept(*this);
        m_bind  = "__curr__";
    }
    else
    {
        spec->accept(*this);
    }
    
    return  canonic(m_expr);
}

Expr*   RewriteImpl::canonic(Expr* expr)
{
    return Canonic::make(expr);
}

Expr*   RewriteImpl::negated(Expr* expr)
{
    return Negated::make(expr);
}

Expr*   Rewrite::make(Expr* expr)
{
    RewriteImpl impl;

    return  impl.make(expr);
}

Time*   Rewrite::make(Time* time)
{
    RewriteImpl impl;

    return  impl.make(time);
}

Expr*   Rewrite::make(Spec* spec)
{
    RewriteImpl impl;

    return  impl.make(spec);
}
