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

#pragma once

#include "visitor.hpp"
#include <map>
#include <vector>

class Expr
    : public Visitable<>
{
public:
    virtual ~Expr() = default;

    virtual bool is_temporal() {return false;}
};

class   TimeInterval
{
public:
    TimeInterval(Expr* lo, Expr* hi);

    Expr* const lo;
    Expr* const hi;
};

class   TimeLowerBound
    : public TimeInterval
{
public:
    TimeLowerBound(Expr* lo);
};

class   TimeUpperBound
    : public TimeInterval
{
public:
    TimeUpperBound(Expr* hi);
};

class ExprNullary
    : public Visitable<Expr, ExprNullary>
{
protected:
    ExprNullary();
};

class ExprUnary
    : public Visitable<Expr, ExprUnary>
{
protected:
    ExprUnary(int op, Expr* arg);

    bool is_temporal() override;

public:
    const int   op;
    Expr* const arg;
};

class ExprBinary
    : public Visitable<Expr, ExprBinary>
{
protected:
    ExprBinary(int op, Expr* lhs, Expr* rhs);

    bool is_temporal() override;

public:
    const int   op;
    Expr* const lhs;
    Expr* const rhs;
};

class ExprTernary
    : public Visitable<Expr, ExprTernary>
{
protected:
    ExprTernary(int op, Expr* lhs, Expr* mhs, Expr* rhs);

    bool is_temporal() override;

public:
    const int   op;
    Expr* const lhs;
    Expr* const mhs;
    Expr* const rhs;
};

template<typename Type>
class ExprConstT
    : public Visitable<ExprNullary, ExprConstT<Type>>
{
public:
    ExprConstT(Type value) : value(value) {} 
    Type const value;
};

template<typename Type, Type Value>
class ExprConstTV
    : public Visitable<ExprConstT<Type>, ExprConstTV<Type, Value>>
{
public:
    ExprConstTV() : Visitable<ExprConstT<Type>, ExprConstTV<Type, Value>>(Value) {}
};

template<typename Expr>
class Temporal
    : public Visitable<Expr, Temporal<Expr>>
{
public:
    template<typename ... Args>
    Temporal(TimeInterval* time, Args ... args)
        : Visitable<Expr, Temporal<Expr>>(args...)
        , time(time)
    {
    }

    template<typename ... Args>
    Temporal(Args ... args, TimeInterval* time)
        : Visitable<Expr, Temporal<Expr>>(args...)
        , time(time)
    {
    }

    template<typename ... Args>
    Temporal(Args ... args)
        : Visitable<Expr, Temporal<Expr>>(args...)
        , time(nullptr)
    {
    }

    bool is_temporal() override {return true;}

    TimeInterval* const time = nullptr;
};

template<int OP, typename Expr>
class SetOper
    : public Visitable<Expr, SetOper<OP, Expr>>
{
public:
    template<typename ... Args>
    SetOper(Args ... args)
        : Visitable<Expr, SetOper<OP, Expr>>(OP, args...)
    {
    }
};

template<typename Class>
class Final final
    : public Visitable<Class, Final<Class>>
{
public:
    template<typename ... Args>
    Final(Args ... args)
        : Visitable<Class, Final<Class>>(args ...)
    {
    }
};

using ExprTrue  = Final<ExprConstTV<bool, true>>;
using ExprFalse = Final<ExprConstTV<bool, false>>;

using ExprNeg   = Final<SetOper<'-',   ExprUnary>>;

using ExprAdd   = Final<SetOper<'+',   ExprBinary>>;
using ExprSub   = Final<SetOper<'-',   ExprBinary>>;
using ExprMul   = Final<SetOper<'*',   ExprBinary>>;
using ExprDiv   = Final<SetOper<'/',   ExprBinary>>;

using ExprEq    = Final<SetOper<'==',  ExprBinary>>;
using ExprNe    = Final<SetOper<'!=',  ExprBinary>>;
using ExprGt    = Final<SetOper<'>',   ExprBinary>>;
using ExprGe    = Final<SetOper<'>=',  ExprBinary>>;
using ExprLt    = Final<SetOper<'<',   ExprBinary>>;
using ExprLe    = Final<SetOper<'<=',  ExprBinary>>;

using ExprNot   = Final<SetOper<'!',   ExprUnary>>;

using ExprOr    = Final<SetOper<'||',  ExprBinary>>;
using ExprAnd   = Final<SetOper<'&&',  ExprBinary>>;
using ExprXor   = Final<SetOper<'^',   ExprBinary>>;
using ExprImp   = Final<SetOper<'=>',  ExprBinary>>;
using ExprEqu   = Final<SetOper<'<=>', ExprBinary>>;

using ExprChoice= Final<SetOper<'?:',  ExprTernary>>;

using ExprG     = Final<Temporal<SetOper<'G',   ExprUnary>>>;
using ExprF     = Final<Temporal<SetOper<'F',   ExprUnary>>>;
using ExprXs    = Final<Temporal<SetOper<'Xs',  ExprUnary>>>;
using ExprXw    = Final<Temporal<SetOper<'Xw',  ExprUnary>>>;
using ExprUs    = Final<Temporal<SetOper<'Us',  ExprBinary>>>;
using ExprUw    = Final<Temporal<SetOper<'Uw',  ExprBinary>>>;
using ExprRs    = Final<Temporal<SetOper<'Rs',  ExprBinary>>>;
using ExprRw    = Final<Temporal<SetOper<'Rw',  ExprBinary>>>;

using ExprH     = Final<Temporal<SetOper<'H',   ExprUnary>>>;
using ExprO     = Final<Temporal<SetOper<'O',   ExprUnary>>>;
using ExprYs    = Final<Temporal<SetOper<'Ys',  ExprUnary>>>;
using ExprYw    = Final<Temporal<SetOper<'Yw',  ExprUnary>>>;
using ExprSs    = Final<Temporal<SetOper<'Ss',  ExprBinary>>>;
using ExprSw    = Final<Temporal<SetOper<'Sw',  ExprBinary>>>;
using ExprTs    = Final<Temporal<SetOper<'Ts',  ExprBinary>>>;
using ExprTw    = Final<Temporal<SetOper<'Tw',  ExprBinary>>>;

class ExprData final
    : public Visitable<ExprNullary, ExprData>
{
public:
    ExprData(std::string data)
        : Visitable<ExprNullary, ExprData>()
        , data(data)
    {
    }

public:
    std::string data;
};

class ExprMmbr final
    : public Visitable<ExprUnary, ExprMmbr>
{
public:
    ExprMmbr(Expr* base, std::string mmbr)
        : Visitable<ExprUnary, ExprMmbr>(0, base)
        , mmbr(mmbr)
    {
    }

public:
    std::string mmbr;
};

using ExprIndx  = Final<SetOper<'[]',  ExprBinary>>;


class Type
    : public Visitable<>
{
public:
    virtual ~Type() = default;
};

class TypeVoid    : public Visitable<Type, TypeVoid> {};
class TypeBoolean : public Visitable<Type, TypeBoolean> {}; 
class TypeInteger : public Visitable<Type, TypeInteger> {}; 
class TypeNumber  : public Visitable<Type, TypeNumber> {}; 
class TypeString  : public Visitable<Type, TypeString> {}; 

template<typename T>
class Named
{
public:
    Named(std::string name, T* data)
        : name(name)
        , data(data)
    {
    }

    std::string name;
    T*          data;
};

class TypeStruct
    : public Visitable<Type, TypeStruct>
{
public:
    TypeStruct(std::vector<Named<Type>> members);

    std::vector<Named<Type>>    members;
};

class TypeArray
    : public Visitable<Type, TypeArray>
{
public:
    TypeArray(Type* type, unsigned size);

    Type* const     type;
    unsigned const  size;   //  if size is 0, array is dynamic otherwise it's static
};

class   TypeEnum
    : public Visitable<Type, TypeEnum>
{
public:
    TypeEnum(std::vector<std::string>   items);

    std::vector<std::string> const  items;
};


class Data
    : public Visitable<>
{
public:
    virtual ~Data() = default;
};

//  real data coming from a DB
class DataReal
    : public Visitable<Data, DataReal>
{
public:
    DataReal(Type* type);
};

//  temp data, result of an external function call 
class DataTemp
    : public Visitable<Data, DataReal>
{
};

//  result of an expression
class DataExpr
    : public Visitable<Data, DataReal>
{
public:
    DataExpr(Expr* expr);
};


class Module
{
public:
};
