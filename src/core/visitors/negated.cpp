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

#include "negated.hpp"
#include "../factory.hpp"

struct NegatedImpl
    : Visitor< Expr
             , ExprConstBoolean
             , ExprAt
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
             , ExprG
             , ExprF
             , ExprXs
             , ExprXw
             , ExprUs
             , ExprUw
             , ExprRs
             , ExprRw
             , ExprH
             , ExprO
             , ExprYs
             , ExprYw
             , ExprSs
             , ExprSw
             , ExprTs
             , ExprTw
             , ExprParen>
{
    Expr*   m_negated   = nullptr;

    Expr*   negated(Expr*   expr);

    void    visit(Expr*   expr) override;
    void    visit(ExprConstBoolean* expr) override;
    void    visit(ExprAt*       expr) override;
    void    visit(ExprEq*       expr) override;
    void    visit(ExprNe*       expr) override;
    void    visit(ExprGt*       expr) override;
    void    visit(ExprGe*       expr) override;
    void    visit(ExprLt*       expr) override;
    void    visit(ExprLe*       expr) override;
    void    visit(ExprNot*      expr) override;
    void    visit(ExprOr*       expr) override;
    void    visit(ExprAnd*      expr) override;
    void    visit(ExprXor*      expr) override;
    void    visit(ExprImp*      expr) override;
    void    visit(ExprEqu*      expr) override;
    void    visit(ExprG*        expr) override;
    void    visit(ExprF*        expr) override;
    void    visit(ExprXs*       expr) override;
    void    visit(ExprXw*       expr) override;
    void    visit(ExprUs*       expr) override;
    void    visit(ExprUw*       expr) override;
    void    visit(ExprRs*       expr) override;
    void    visit(ExprRw*       expr) override;
    void    visit(ExprH*        expr) override;
    void    visit(ExprO*        expr) override;
    void    visit(ExprYs*       expr) override;
    void    visit(ExprYw*       expr) override;
    void    visit(ExprSs*       expr) override;
    void    visit(ExprSw*       expr) override;
    void    visit(ExprTs*       expr) override;
    void    visit(ExprTw*       expr) override;
    void    visit(ExprParen*    expr) override;
};

void    NegatedImpl::visit(Expr*        expr) {m_negated = Factory<ExprNot>::create(expr);}

void    NegatedImpl::visit(ExprConstBoolean*    expr) {m_negated = Factory<ExprConstBoolean>::create(!expr->value);}

void    NegatedImpl::visit(ExprAt*     expr) {m_negated = Factory<ExprAt>::create(expr->name, negated(expr->arg));}

void    NegatedImpl::visit(ExprEq*     expr) {m_negated = Factory<ExprNe>::create((expr->lhs), expr->rhs);}
void    NegatedImpl::visit(ExprNe*     expr) {m_negated = Factory<ExprEq>::create((expr->lhs), expr->rhs);}

void    NegatedImpl::visit(ExprGt*     expr) {m_negated = Factory<ExprLe>::create((expr->lhs), expr->rhs);}
void    NegatedImpl::visit(ExprGe*     expr) {m_negated = Factory<ExprLt>::create((expr->lhs), expr->rhs);}

void    NegatedImpl::visit(ExprLt*     expr) {m_negated = Factory<ExprGe>::create((expr->lhs), expr->rhs);}
void    NegatedImpl::visit(ExprLe*     expr) {m_negated = Factory<ExprGt>::create((expr->lhs), expr->rhs);}

void    NegatedImpl::visit(ExprNot*    expr) {m_negated = (expr->arg);}

void    NegatedImpl::visit(ExprOr*     expr) {m_negated = Factory<ExprAnd>::create(negated(expr->lhs), negated(expr->rhs));}
void    NegatedImpl::visit(ExprAnd*    expr) {m_negated = Factory<ExprOr>::create(negated(expr->lhs), negated(expr->rhs));}

void    NegatedImpl::visit(ExprXor*    expr) {m_negated = Factory<ExprEqu>::create(expr->lhs, expr->rhs);}
void    NegatedImpl::visit(ExprEqu*    expr) {m_negated = Factory<ExprXor>::create(expr->lhs, expr->rhs);}

void    NegatedImpl::visit(ExprImp*    expr) {m_negated = Factory<ExprAnd>::create(expr->lhs, negated(expr->rhs));}

void    NegatedImpl::visit(ExprG*      expr) {m_negated = Factory<ExprF>::create(expr->time, negated(expr->arg));}
void    NegatedImpl::visit(ExprF*      expr) {m_negated = Factory<ExprG>::create(expr->time, negated(expr->arg));}
void    NegatedImpl::visit(ExprXs*     expr) {m_negated = Factory<ExprXw>::create(expr->lhs, negated(expr->rhs));}
void    NegatedImpl::visit(ExprXw*     expr) {m_negated = Factory<ExprXs>::create(expr->lhs, negated(expr->rhs));}
void    NegatedImpl::visit(ExprUs*     expr) {m_negated = Factory<ExprRw>::create(expr->time, negated(expr->lhs), negated(expr->rhs));}
void    NegatedImpl::visit(ExprUw*     expr) {m_negated = Factory<ExprRs>::create(expr->time, negated(expr->lhs), negated(expr->rhs));}
void    NegatedImpl::visit(ExprRs*     expr) {m_negated = Factory<ExprUw>::create(expr->time, negated(expr->lhs), negated(expr->rhs));}
void    NegatedImpl::visit(ExprRw*     expr) {m_negated = Factory<ExprUs>::create(expr->time, negated(expr->lhs), negated(expr->rhs));}

void    NegatedImpl::visit(ExprH*      expr) {m_negated = Factory<ExprO>::create(expr->time, negated(expr->arg));}
void    NegatedImpl::visit(ExprO*      expr) {m_negated = Factory<ExprH>::create(expr->time, negated(expr->arg));}
void    NegatedImpl::visit(ExprYs*     expr) {m_negated = Factory<ExprYw>::create(expr->lhs, negated(expr->rhs));}
void    NegatedImpl::visit(ExprYw*     expr) {m_negated = Factory<ExprYs>::create(expr->lhs, negated(expr->rhs));}
void    NegatedImpl::visit(ExprSs*     expr) {m_negated = Factory<ExprTw>::create(expr->time, negated(expr->lhs), negated(expr->rhs));}
void    NegatedImpl::visit(ExprSw*     expr) {m_negated = Factory<ExprTs>::create(expr->time, negated(expr->lhs), negated(expr->rhs));}
void    NegatedImpl::visit(ExprTs*     expr) {m_negated = Factory<ExprSw>::create(expr->time, negated(expr->lhs), negated(expr->rhs));}
void    NegatedImpl::visit(ExprTw*     expr) {m_negated = Factory<ExprSs>::create(expr->time, negated(expr->lhs), negated(expr->rhs));}

void    NegatedImpl::visit(ExprParen*  expr) {m_negated = negated(expr->arg);}

Expr*   NegatedImpl::negated(Expr* expr)
{
    m_negated   = nullptr;
    expr->accept(*this);
    return m_negated;
}

Expr* Negated::make(Expr* expr)
{
    NegatedImpl impl;

    return  impl.negated(expr);
}
