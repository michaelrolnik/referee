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

#include "canonic.hpp"
#include "negated.hpp"
#include "../factory.hpp"

struct CanonicImpl
    : Visitor< Expr
             , ExprConstBoolean
             , ExprAt
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
    Expr* m_canonic = nullptr;

    Expr* t = Factory<ExprConstBoolean>::create(true);
    Expr* f = Factory<ExprConstBoolean>::create(false);

    Expr*       canonic(Expr* expr);
    Expr*       negated(Expr* expr);

    void        visit(Expr*   expr) override;
    void        visit(ExprConstBoolean* expr) override;

    void        visit(ExprAt*       expr) override;
    void        visit(ExprNot*      expr) override;
    void        visit(ExprOr*       expr) override;
    void        visit(ExprAnd*      expr) override;
    void        visit(ExprXor*      expr) override;
    void        visit(ExprImp*      expr) override;
    void        visit(ExprEqu*      expr) override;
    void        visit(ExprG*        expr) override;
    void        visit(ExprF*        expr) override;
    void        visit(ExprXs*       expr) override;
    void        visit(ExprXw*       expr) override;
    void        visit(ExprUs*       expr) override;
    void        visit(ExprUw*       expr) override;
    void        visit(ExprRs*       expr) override;
    void        visit(ExprRw*       expr) override;
    void        visit(ExprH*        expr) override;
    void        visit(ExprO*        expr) override;
    void        visit(ExprYs*       expr) override;
    void        visit(ExprYw*       expr) override;
    void        visit(ExprSs*       expr) override;
    void        visit(ExprSw*       expr) override;
    void        visit(ExprTs*       expr) override;
    void        visit(ExprTw*       expr) override;
    void        visit(ExprParen*    expr) override;
};

void    CanonicImpl::visit(Expr*        expr) {m_canonic = expr;}
void    CanonicImpl::visit(ExprParen*   expr) {m_canonic = canonic(expr->arg);}
void    CanonicImpl::visit(ExprConstBoolean*    expr) {m_canonic = expr;}
void    CanonicImpl::visit(ExprAt*      expr) {m_canonic = Factory<ExprAt>::create(expr->name, canonic(expr->arg));}
void    CanonicImpl::visit(ExprNot*     expr) {m_canonic = negated(expr->arg);}
void    CanonicImpl::visit(ExprOr*      expr) {m_canonic = Factory<ExprOr>::create(canonic(expr->lhs), canonic(expr->rhs));}
void    CanonicImpl::visit(ExprAnd*     expr) {m_canonic = Factory<ExprAnd>::create(canonic(expr->lhs), canonic(expr->rhs));}
void    CanonicImpl::visit(ExprXor*     expr) {m_canonic = Factory<ExprXor>::create(canonic(expr->lhs), canonic(expr->rhs));}
void    CanonicImpl::visit(ExprImp*     expr) {m_canonic = Factory<ExprOr>::create(canonic(negated(expr->lhs)), canonic(expr->rhs));}
void    CanonicImpl::visit(ExprEqu*     expr) {m_canonic = Factory<ExprEqu>::create(canonic(expr->lhs), canonic(expr->rhs));}
void    CanonicImpl::visit(ExprG*       expr) {m_canonic = Factory<ExprRw>::create(expr->time, f, canonic(expr->arg));}
void    CanonicImpl::visit(ExprF*       expr) {m_canonic = Factory<ExprUs>::create(expr->time, t, canonic(expr->arg));}
void    CanonicImpl::visit(ExprXs*      expr) {m_canonic = Factory<ExprXs>::create(canonic(expr->lhs), canonic(expr->rhs));}
void    CanonicImpl::visit(ExprXw*      expr) {m_canonic = Factory<ExprXw>::create(canonic(expr->lhs), canonic(expr->rhs));}
void    CanonicImpl::visit(ExprUs*      expr) {m_canonic = Factory<ExprUs>::create(expr->time, canonic(expr->lhs), canonic(expr->rhs));}
void    CanonicImpl::visit(ExprUw*      expr) {m_canonic = Factory<ExprUw>::create(expr->time, canonic(expr->lhs), canonic(expr->rhs));}
void    CanonicImpl::visit(ExprRs*      expr) {m_canonic = Factory<ExprRs>::create(expr->time, canonic(expr->lhs), canonic(expr->rhs));}
void    CanonicImpl::visit(ExprRw*      expr) {m_canonic = Factory<ExprRw>::create(expr->time, canonic(expr->lhs), canonic(expr->rhs));}
void    CanonicImpl::visit(ExprH*       expr) {m_canonic = Factory<ExprTw>::create(expr->time, f, canonic(expr->arg));}
void    CanonicImpl::visit(ExprO*       expr) {m_canonic = Factory<ExprSs>::create(expr->time, t, canonic(expr->arg));}
void    CanonicImpl::visit(ExprYs*      expr) {m_canonic = Factory<ExprYs>::create(canonic(expr->lhs), canonic(expr->rhs));}
void    CanonicImpl::visit(ExprYw*      expr) {m_canonic = Factory<ExprYw>::create(canonic(expr->lhs), canonic(expr->rhs));}
void    CanonicImpl::visit(ExprSs*      expr) {m_canonic = Factory<ExprSs>::create(expr->time, canonic(expr->lhs), canonic(expr->rhs));}
void    CanonicImpl::visit(ExprSw*      expr) {m_canonic = Factory<ExprSw>::create(expr->time, canonic(expr->lhs), canonic(expr->rhs));}
void    CanonicImpl::visit(ExprTs*      expr) {m_canonic = Factory<ExprTs>::create(expr->time, canonic(expr->lhs), canonic(expr->rhs));}
void    CanonicImpl::visit(ExprTw*      expr) {m_canonic = Factory<ExprTw>::create(expr->time, canonic(expr->lhs), canonic(expr->rhs));}

Expr* CanonicImpl::canonic(Expr* expr)
{
    m_canonic = expr;

    expr->accept(*this);

    return  m_canonic;
}

Expr* CanonicImpl::negated(Expr* expr)
{
    return  Negated::make(expr);
}

Expr* Canonic::make(Expr* expr)
{
    CanonicImpl impl;

    return impl.canonic(expr);
}

