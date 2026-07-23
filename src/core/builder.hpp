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

#pragma once

#include "factory.hpp"
#include "syntax.hpp"

class   Wrapper
{
public:
    Wrapper(Expr* expr)
        : m_expr(expr)
    {
    }

    Expr*   get()
    {
        return  m_expr;
    }

    operator Expr*()
    {
        return m_expr;
    }

    Wrapper paren()
    {
        return Wrapper(Factory<ExprParen>::create(m_expr));
    }

private:
    Expr*   m_expr;
};

Wrapper     operator+   (Wrapper lhs, Wrapper rhs)  { return Wrapper(Factory<ExprAdd>::create(lhs.get(), rhs.get())).paren();}
Wrapper     operator-   (Wrapper lhs, Wrapper rhs)  { return Wrapper(Factory<ExprSub>::create(lhs.get(), rhs.get())).paren();}
Wrapper     operator*   (Wrapper lhs, Wrapper rhs)  { return Wrapper(Factory<ExprMul>::create(lhs.get(), rhs.get())).paren();}
Wrapper     operator/   (Wrapper lhs, Wrapper rhs)  { return Wrapper(Factory<ExprDiv>::create(lhs.get(), rhs.get())).paren();}
Wrapper     operator%   (Wrapper lhs, Wrapper rhs)  { return Wrapper(Factory<ExprMod>::create(lhs.get(), rhs.get())).paren();}

Wrapper     operator==  (Wrapper lhs, Wrapper rhs)  { return Wrapper(Factory<ExprEq>::create( lhs.get(), rhs.get())).paren();}
Wrapper     operator!=  (Wrapper lhs, Wrapper rhs)  { return Wrapper(Factory<ExprNe>::create( lhs.get(), rhs.get())).paren();}
Wrapper     operator<=  (Wrapper lhs, Wrapper rhs)  { return Wrapper(Factory<ExprLe>::create( lhs.get(), rhs.get())).paren();}
Wrapper     operator>=  (Wrapper lhs, Wrapper rhs)  { return Wrapper(Factory<ExprGe>::create( lhs.get(), rhs.get())).paren();}
Wrapper     operator<   (Wrapper lhs, Wrapper rhs)  { return Wrapper(Factory<ExprLt>::create( lhs.get(), rhs.get())).paren();}
Wrapper     operator>   (Wrapper lhs, Wrapper rhs)  { return Wrapper(Factory<ExprGt>::create( lhs.get(), rhs.get())).paren();}

Wrapper     operator&&  (Wrapper lhs, Wrapper rhs)  { return Wrapper(Factory<ExprAnd>::create(lhs.get(), rhs.get())).paren();}
Wrapper     operator||  (Wrapper lhs, Wrapper rhs)  { return Wrapper(Factory<ExprOr>::create( lhs.get(), rhs.get())).paren();}

Wrapper     operator>>  (Wrapper lhs, Wrapper rhs)  { return Wrapper(Factory<ExprImp>::create( lhs.get(), rhs.get())).paren();}

Wrapper     operator!   (Wrapper arg)               { return Wrapper(Factory<ExprNot>::create(arg.get())).paren();}

Wrapper     G(Expr* arg)                            { return Wrapper(Factory<ExprG>::create( arg));}
Wrapper     F(Expr* arg)                            { return Wrapper(Factory<ExprF>::create( arg));}

Wrapper     G(Time* time, Expr* arg)                { return Wrapper(Factory<ExprG>::create( time, arg));}
Wrapper     F(Time* time, Expr* arg)                { return Wrapper(Factory<ExprF>::create( time, arg));}

Wrapper     H(Expr* arg)                            { return Wrapper(Factory<ExprH>::create( arg));}
Wrapper     O(Expr* arg)                            { return Wrapper(Factory<ExprO>::create( arg));}

Wrapper     H(Time* time, Expr* arg)                { return Wrapper(Factory<ExprH>::create( time, arg));}
Wrapper     O(Time* time, Expr* arg)                { return Wrapper(Factory<ExprO>::create( time, arg));}

Wrapper     Xs(Expr* arg)                           { return Wrapper(Factory<ExprXs>::create( Factory<ExprConstInteger>::create(1), arg));}
Wrapper     Xw(Expr* arg)                           { return Wrapper(Factory<ExprXw>::create( Factory<ExprConstInteger>::create(1), arg));}
Wrapper     Ys(Expr* arg)                           { return Wrapper(Factory<ExprYs>::create( Factory<ExprConstInteger>::create(1), arg));}
Wrapper     Yw(Expr* arg)                           { return Wrapper(Factory<ExprYw>::create( Factory<ExprConstInteger>::create(1), arg));}

Wrapper     Us(Expr* lhs, Expr* rhs)                { return Wrapper(Factory<ExprUs>::create( lhs, rhs));}
Wrapper     Uw(Expr* lhs, Expr* rhs)                { return Wrapper(Factory<ExprUw>::create( lhs, rhs));}
Wrapper     Rs(Expr* lhs, Expr* rhs)                { return Wrapper(Factory<ExprRs>::create( lhs, rhs));}
Wrapper     Rw(Expr* lhs, Expr* rhs)                { return Wrapper(Factory<ExprRw>::create( lhs, rhs));}
Wrapper     Ss(Expr* lhs, Expr* rhs)                { return Wrapper(Factory<ExprSs>::create( lhs, rhs));}
Wrapper     Sw(Expr* lhs, Expr* rhs)                { return Wrapper(Factory<ExprSw>::create( lhs, rhs));}
Wrapper     Ts(Expr* lhs, Expr* rhs)                { return Wrapper(Factory<ExprTs>::create( lhs, rhs));}
Wrapper     Tw(Expr* lhs, Expr* rhs)                { return Wrapper(Factory<ExprTw>::create( lhs, rhs));}

Wrapper     Us(Time* time, Expr* lhs, Expr* rhs)    { return Wrapper(Factory<ExprUs>::create( time, lhs, rhs));}
Wrapper     Uw(Time* time, Expr* lhs, Expr* rhs)    { return Wrapper(Factory<ExprUw>::create( time, lhs, rhs));}
Wrapper     Rs(Time* time, Expr* lhs, Expr* rhs)    { return Wrapper(Factory<ExprRs>::create( time, lhs, rhs));}
Wrapper     Rw(Time* time, Expr* lhs, Expr* rhs)    { return Wrapper(Factory<ExprRw>::create( time, lhs, rhs));}
Wrapper     Ss(Time* time, Expr* lhs, Expr* rhs)    { return Wrapper(Factory<ExprSs>::create( time, lhs, rhs));}
Wrapper     Sw(Time* time, Expr* lhs, Expr* rhs)    { return Wrapper(Factory<ExprSw>::create( time, lhs, rhs));}
Wrapper     Ts(Time* time, Expr* lhs, Expr* rhs)    { return Wrapper(Factory<ExprTs>::create( time, lhs, rhs));}
Wrapper     Tw(Time* time, Expr* lhs, Expr* rhs)    { return Wrapper(Factory<ExprTw>::create( time, lhs, rhs));}

Wrapper     At(std::string name, Expr* arg)         { return Wrapper(Factory<ExprAt>::create( name, arg));}

Wrapper     Not(Expr* arg)                          { return !Wrapper(arg);}
