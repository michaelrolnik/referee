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
#include "position.hpp"

#include <map>
#include <vector>

class Module;

class Base
    : public Visitable<>
{
public:
    virtual ~Base() = default;

    Position    where()     {return m_where;}
    void        where(Position where)
                            {m_where = where;}

private:
    Position    m_where;
}; 

class Exception
    : public std::exception
{
public:
    Exception(Position position, std::string info)
        : position(position)
    {
        std::ostringstream  os;

        os << info << " at [" << position.beg.row << ":" << position.beg.col << " .. " << position.end.row << ":" << position.end.col << "]";

        message     = os.str();
    }

    const char* what() const noexcept override 
    {
        return message.c_str();
    }

private:
    Position    position;
    std::string message;
};

class Type
    : public Visitable<Base, Type>
{
};

class TypePrimitive
    : public Visitable<Type, TypePrimitive>
{
};

class TypeComposite
    : public Visitable<Type, TypePrimitive>
{
public:
    virtual Type*       member(std::string name) = 0;
    virtual unsigned    index(std::string name) = 0;
};

class TypeVoid    : public Visitable<TypePrimitive, TypeVoid> {};
class TypeBoolean : public Visitable<TypePrimitive, TypeBoolean> {}; 
class TypeInteger : public Visitable<TypePrimitive, TypeInteger> {}; 
class TypeNumber  : public Visitable<TypePrimitive, TypeNumber> {}; 
class TypeString  : public Visitable<TypePrimitive, TypeString> {}; 

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

class TypeContext
    : public Visitable<TypeComposite, TypeContext>
{
public:
    TypeContext(Module* module);

    Type*       member(std::string name) override;
    unsigned    index(std::string name) override;

private:
    Module* const   m_module;
};

class TypeStruct
    : public Visitable<TypeComposite, TypeStruct>
{
public:
    TypeStruct(std::vector<Named<Type>> members);

    std::vector<Named<Type>>        members;

    Type*       member(std::string name) override;
    unsigned    index(std::string name) override;

private:
    std::map<std::string, Type*>    m_name2type;
    std::map<std::string, unsigned> m_name2indx;
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
    : public Visitable<TypeComposite, TypeEnum>
{
public:
    TypeEnum(std::vector<std::string>   items);

    Type*       member(std::string name) override;
    unsigned    index(std::string name) override;

    std::vector<std::string> const  items;
private:
    std::map<std::string, unsigned> m_name2indx;
};   


class Expr
    : public Visitable<Base, Expr>
{
public:
    virtual bool is_temporal() {return false;}

    Type*   type()              {return m_type;}
    void    type(Type* type)    {m_type = type;}

private:
    Type*   m_type  = nullptr;
};

class   Time
    : public Visitable<Expr, Time>
{
public:
    Time(Expr* lo, Expr* hi);

    Expr* const lo;
    Expr* const hi;
};

class   TimeMin
    : public Visitable<Time, TimeMin>
{
public:
    TimeMin(Expr* lo);
};

class   TimeMax
    : public Visitable<Time, TimeMax>
{
public:
    TimeMax(Expr* hi);
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
    Temporal(int op, Time* time, Args ... args)
        : Visitable<Expr, Temporal<Expr>>(op, args...)
        , time(time)
    {
        //  assert(time != nullptr);    //  TODO: put it back
    }

    template<typename ... Args>
    Temporal(int op, Args ... args)
        : Visitable<Expr, Temporal<Expr>>(op, args...)
        , time(nullptr)
    {
    }

    bool is_temporal() override {return true;}

    Time* const time    = nullptr;
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

using ExprConstInteger  = Final<ExprConstT<int64_t>>;
using ExprConstNumber   = Final<ExprConstT<double>>;
using ExprConstString   = Final<ExprConstT<std::string>>;
using ExprConstBoolean  = Final<ExprConstT<bool>>;

using ExprParen = Final<SetOper<'()',  ExprUnary>>;

using ExprNeg   = Final<SetOper<'-',   ExprUnary>>;

using ExprAdd   = Final<SetOper<'+',   ExprBinary>>;
using ExprSub   = Final<SetOper<'-',   ExprBinary>>;
using ExprMul   = Final<SetOper<'*',   ExprBinary>>;
using ExprDiv   = Final<SetOper<'/',   ExprBinary>>;
using ExprMod   = Final<SetOper<'%',   ExprBinary>>;

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

using ExprXs    = Final<SetOper<'Xs',  ExprBinary>>;
using ExprXw    = Final<SetOper<'Xw',  ExprBinary>>;
using ExprG     = Final<SetOper<'G',   Temporal<ExprUnary>>>;
using ExprF     = Final<SetOper<'F',   Temporal<ExprUnary>>>;
using ExprUs    = Final<SetOper<'Us',  Temporal<ExprBinary>>>;
using ExprUw    = Final<SetOper<'Uw',  Temporal<ExprBinary>>>;
using ExprRs    = Final<SetOper<'Rs',  Temporal<ExprBinary>>>;
using ExprRw    = Final<SetOper<'Rw',  Temporal<ExprBinary>>>;
                                                
using ExprYs    = Final<SetOper<'Ys',  ExprBinary>>;
using ExprYw    = Final<SetOper<'Yw',  ExprBinary>>;
using ExprH     = Final<SetOper<'H',   Temporal<ExprUnary>>>;
using ExprO     = Final<SetOper<'O',   Temporal<ExprUnary>>>;
using ExprSs    = Final<SetOper<'Ss',  Temporal<ExprBinary>>>;
using ExprSw    = Final<SetOper<'Sw',  Temporal<ExprBinary>>>;
using ExprTs    = Final<SetOper<'Ts',  Temporal<ExprBinary>>>;
using ExprTw    = Final<SetOper<'Tw',  Temporal<ExprBinary>>>;
using ExprInt   = Final<SetOper<'I',   Temporal<ExprBinary>>>;

class ExprAt final
    : public Visitable<SetOper<'@', ExprUnary>, ExprAt>
{
public:
    ExprAt(std::string name, Expr* expr)
        : Visitable<SetOper<'@', ExprUnary>, ExprAt>(expr)
        , name(name)
    {
    }

public:
    std::string name;
};

class ExprContext final
    : public Visitable<ExprNullary, ExprContext>
{
public:
    ExprContext(std::string name)
        : Visitable<ExprNullary, ExprContext>()
        , name(name)
    {
    }

public:
    std::string const   name;
};

class ExprConf final
    : public Visitable<ExprNullary, ExprConf>
{
public:
    ExprConf(ExprContext* ctxt, std::string name)
        : ctxt(ctxt)
        , name(name)
    {
    }

public:
    ExprContext* const  ctxt;
    std::string const   name;
};

class ExprData final
    : public Visitable<ExprNullary, ExprData>
{
public:
    ExprData(ExprContext* ctxt, std::string name)
        : ctxt(ctxt)
        , name(name)
    {
    }

public:
    ExprContext* const  ctxt;
    std::string const   name;
};

class ExprMmbr final
    : public Visitable<ExprUnary, ExprMmbr>
{
public:
    ExprMmbr(Expr* base, std::string mmbr)
        : Visitable(0, base)
        , mmbr(mmbr)
    {
    }

public:
    std::string const   mmbr;
};

using ExprIndx  = Final<SetOper<'[]',  ExprBinary>>;

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

class DataConf
    : public Visitable<Data, DataConf>
{
public:
    DataConf(Type* type);
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

class Spec
    : public Visitable<Base, Spec>
{
};

class SpecScoped
    : public Visitable<Spec, SpecScoped>
{
public:
    SpecScoped(Spec* spec)
        : spec(spec)
    {
    }

    Spec*  const    spec;
};

class SpecUniversality
    : public Visitable<Spec, SpecUniversality>
{
public:    
    SpecUniversality(Expr* p, Time* tP)
        : P(p), tP(tP)
    {
    }

    Expr*   P;
    Time*   tP;
};

class SpecAbsence
    : public Visitable<Spec, SpecAbsence>
{
public:  
    SpecAbsence(Expr* p, Time* tP)
        : P(p), tP(tP)
    {
    }

    Expr*   P;
    Time*   tP;  
};

class SpecExistence
    : public Visitable<Spec, SpecExistence>
{
public:    
    SpecExistence(Expr* p, Time* tP)
        : P(p), tP(tP)
    {
    }

    Expr*   P;
    Time*   tP; 
};

class SpecTransientState
    : public Visitable<Spec, SpecTransientState>
{
public:    
    SpecTransientState(Expr* p, Time* tP)
        : P(p), tP(tP)
    {
    }

    Expr*   P;
    Time*   tP;  
};

class SpecSteadyState
    : public Visitable<Spec, SpecSteadyState>
{
public:    
    SpecSteadyState(Expr* p)
        : P(p)
    {
    }

    Expr*   P;
};

class SpecMinimunDuration
    : public Visitable<Spec, SpecMinimunDuration>
{
public:    
    SpecMinimunDuration(Expr* p, Time* tP)
        : P(p), tP(tP)
    {
    }

    Expr*   P;
    Time*   tP;  
};

class SpecMaximumDuration
    : public Visitable<Spec, SpecMaximumDuration>
{
public:    
    SpecMaximumDuration(Expr* p, Time* tP)
        : P(p), tP(tP)
    {
    }

    Expr*   P;
    Time*   tP;  
};

class SpecRecurrence
    : public Visitable<Spec, SpecRecurrence>
{
public:    
    SpecRecurrence(Expr* p, Time* tP)
        : P(p), tP(tP)
    {
    }

    Expr*   P;
    Time*   tP;  
};

class SpecPrecedence
    : public Visitable<Spec, SpecPrecedence>
{
public:  
    SpecPrecedence(Expr* p, Expr* s, Time* tPS)
        : P(p), S(s), tPS(tPS)
    {
    }

    Expr*   P;
    Expr*   S;
    Time*   tPS;    
};

class SpecPrecedenceChain12
    : public Visitable<Spec, SpecPrecedenceChain12>
{
public:    
    SpecPrecedenceChain12(Expr* S, Expr* T, Expr* P, Time* tST, Time* tPS)
        : S(S), T(T), P(P), tST(tST), tPS(tPS)
    {
    }

    Expr*   S;
    Expr*   T;
    Expr*   P;
    Time*   tST;    //  time between S and T  
    Time*   tPS;    //  time between P and S
};

class SpecPrecedenceChain21
    : public Visitable<Spec, SpecPrecedenceChain21>
{
public:   
    SpecPrecedenceChain21(Expr* P, Expr* S, Expr* T, Time* tST, Time* tPS)
        : P(P), S(S), T(T), tST(tST), tPS(tPS)
    {
    }

    Expr*   P;
    Expr*   S;
    Expr*   T;
    Time*   tST;    //  time between S and T  
    Time*   tPS;    //  time between P and S 
};

class SpecResponse
    : public Visitable<Spec, SpecResponse>
{
public:    
    SpecResponse(Expr* P, Expr* S, Time* tPS, Expr* cPS)
        : P(P), S(S), tPS(tPS), cPS(cPS)
    {
    }

    Expr*   P;
    Expr*   S;
    Time*   tPS;  
    Expr*   cPS;
};

class SpecResponseChain12
    : public Visitable<Spec, SpecResponseChain12>
{
public:  
    SpecResponseChain12(Expr* P, Expr* S, Expr* T, Time* tPS, Time* tST, Expr* cPS, Expr* cST)
        : P(P), S(S), T(T), tPS(tPS), tST(tST), cPS(cPS), cST(cST)
    {
    }

    Expr*   P;
    Expr*   S;
    Expr*   T;
    Time*   tPS;    //  time between P and S   
    Time*   tST;    //  time between S and T  
    Expr*   cPS;
    Expr*   cST;
};

class SpecResponseChain21
    : public Visitable<Spec, SpecResponseChain21>
{
public:    
    SpecResponseChain21(Expr* S, Expr* T, Expr* P, Time* tST, Time* tTP, Expr* cPS, Expr* cTP)
        : S(S), T(T), P(P), tST(tST), tTP(tTP), cST(tST), cTP(cTP)
    {
    }

    Expr*   S;
    Expr*   T;
    Expr*   P;
    Time*   tST;    //  time between S and T  
    Time*   tTP;    //  time between T and P
    Expr*   cST;
    Expr*   cTP;
};

class SpecResponseInvariance
    : public Visitable<Spec, SpecResponseInvariance>
{
public:  
    SpecResponseInvariance(Expr* P, Expr* S, Time* tPS)
        : P(P), S(S), tPS(tPS)
    {
    }

    Expr*   P;
    Expr*   S;
    Time*   tPS;    
};

class SpecUntil
    : public Visitable<Spec, SpecUntil>
{
public:    
    SpecUntil(Expr* P, Expr* S, Time* tPS)
        : P(P), S(S), tPS(tPS)
    {
    }

    Expr*   P;
    Expr*   S;
    Time*   tPS;    
};

class SpecGlobally
    : public Visitable<SpecScoped, SpecGlobally>
{
public:
    SpecGlobally(Spec* spec)
        : Visitable<SpecScoped, SpecGlobally>(spec)
    {
    }
};

class SpecBefore
    : public Visitable<SpecScoped, SpecBefore>
{
public:
    SpecBefore(Expr* arg, Spec* spec)
        : Visitable<SpecScoped, SpecBefore>(spec)
        , arg(arg)
    {
    }

    Expr* const arg;
};

class SpecAfter
    : public Visitable<SpecScoped, SpecAfter>
{
public:
    SpecAfter(Expr* arg, Spec* spec)
        : Visitable<SpecScoped, SpecAfter>(spec)
        , arg(arg)
    {
    }

    Expr* const arg;
};

class SpecBetweenAnd
    : public Visitable<SpecScoped, SpecBetweenAnd>
{
public:
    SpecBetweenAnd(Expr* lhs, Expr* rhs, Spec* spec)
        : Visitable<SpecScoped, SpecBetweenAnd>(spec)
        , lhs(lhs)
        , rhs(rhs)
    {
    }

    Expr* const lhs;
    Expr* const rhs;
};

class SpecAfterUntil
    : public Visitable<SpecScoped, SpecAfterUntil>
{
public:
    SpecAfterUntil(Expr* lhs, Expr* rhs, Spec* spec)
        : Visitable<SpecScoped, SpecAfterUntil>(spec)
        , lhs(lhs)
        , rhs(rhs)
    {
    }

    Expr* const lhs;
    Expr* const rhs;
};

class SpecWhile
    : public Visitable<SpecBetweenAnd, SpecWhile>
{
public:
    SpecWhile(Expr* arg, Spec* spec);
};


