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

#include "position.hpp"
#include "visitor.hpp"

#include <assert.h>
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
        , info(info)
    {
        std::ostringstream  os;

        os << info << " at [" << position.text() << "]";

        message     = os.str();
    }

    const char* what() const noexcept override
    {
        return message.c_str();
    }

    //  Structured access for consumers that carry the location themselves (the
    //  language server puts it in an LSP range and shows [info] without the
    //  redundant " at [row:col]" suffix that [what] appends).
    Position            where() const   {return position;}
    std::string const&  reason() const  {return info;}

private:
    Position    position;
    std::string info;
    std::string message;
};

class Type
    : public Visitable<Base, Type>
{
public:
    virtual size_t size()      = 0;
    virtual size_t alignment() = 0;
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

class TypeVoid    : public Visitable<TypePrimitive, TypeVoid>
{
public:
    size_t size()      override { assert(!"void has no size");      return 0; }
    size_t alignment() override { assert(!"void has no alignment"); return 0; }
};

class TypeBoolean : public Visitable<TypePrimitive, TypeBoolean>
{
public:
    size_t size()      override { return 1; }
    size_t alignment() override { return 1; }
};

//  An 8-bit unsigned value. Stored in one byte, but *read* as an integer:
//  expressions never see a distinct byte type, so no arithmetic or comparison
//  rule has to know about it. It exists so that a payload of bytes costs one
//  byte each rather than eight.
class TypeByte    : public Visitable<TypePrimitive, TypeByte>
{
public:
    size_t size()      override { return 1; }
    size_t alignment() override { return 1; }
};

class TypeInteger : public Visitable<TypePrimitive, TypeInteger>
{
public:
    size_t size()      override { return 8; }
    size_t alignment() override { return 8; }
};

class TypeNumber  : public Visitable<TypePrimitive, TypeNumber>
{
public:
    size_t size()      override { return 8; }
    size_t alignment() override { return 8; }
};

class TypeString  : public Visitable<TypePrimitive, TypeString>
{
public:
    // Blob is a host pointer (char const*) interned via Strings::instance().
    // The pointer is only valid for the lifetime of that singleton.
    static_assert(sizeof(void*) == 8, "TypeString assumes 64-bit pointers");
    size_t size()      override { return sizeof(void*); }
    size_t alignment() override { return sizeof(void*); }
};

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

    size_t size()      override { assert(!"context has no size");      return 0; }
    size_t alignment() override { assert(!"context has no alignment"); return 0; }

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

    size_t alignment() override {
        size_t a = 1;
        for (auto& m : members) a = std::max(a, m.data->alignment());
        return a;
    }

    size_t size() override {
        size_t align = alignment(), total = 0;
        for (auto& m : members) {
            size_t ma = m.data->alignment();
            total = (total + ma - 1) / ma * ma;
            total += m.data->size();
        }
        return (total + align - 1) / align * align;
    }

private:
    std::map<std::string, Type*>    m_name2type;
    std::map<std::string, unsigned> m_name2indx;
};

class TypeArray
    : public Visitable<Type, TypeArray>
{
public:
    TypeArray(Type* type, unsigned count);

    Type* const     type;
    unsigned const  count;  //  if count is 0, array is dynamic otherwise it's static

    size_t alignment() override {
        if (count == 0) return 8;
        return type->alignment();
    }

    size_t size() override {
        if (count == 0) return 16; // {size_t count; T const* data;}
        size_t ea = type->alignment(), es = type->size();
        return ((es + ea - 1) / ea * ea) * count;
    }
};

class   TypeEnum
    : public Visitable<TypeComposite, TypeEnum>
{
public:
    TypeEnum(std::vector<std::string>   items);

    Type*       member(std::string name) override;
    unsigned    index(std::string name) override;

    size_t size()      override { return 1; }
    size_t alignment() override { return 1; }

    std::vector<std::string> const  items;
private:
    std::map<std::string, unsigned> m_name2indx;
};


class Expr
    : public Visitable<Base, Expr>
{
public:
    //  Opts every expression node into per-compilation interning. A node
    //  carries mutable state -- the type stamped on it below -- so sharing one
    //  between two specifications lets the first decide the second's types.
    //  Types deliberately do not opt in: they are values, and interning them
    //  globally is what makes them comparable by pointer.
    using factory_scoped = void;

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

//  Bitwise. `^` is deliberately absent: it already exists as logical xor and
//  is extended to integers rather than duplicated, which is why there is no
//  `^^` to pair with `&&` and `||`.
using ExprBnot  = Final<SetOper<'~',   ExprUnary>>;
using ExprBand  = Final<SetOper<'&',   ExprBinary>>;
using ExprBxor  = Final<SetOper<'^',   ExprBinary>>;   //  bitwise xor (`^`)
using ExprBor   = Final<SetOper<'|',   ExprBinary>>;
using ExprShl   = Final<SetOper<'<<',  ExprBinary>>;
using ExprShr   = Final<SetOper<'>>',  ExprBinary>>;

using ExprOr    = Final<SetOper<'||',  ExprBinary>>;
using ExprAnd   = Final<SetOper<'&&',  ExprBinary>>;
using ExprXor   = Final<SetOper<'^^',  ExprBinary>>;   //  logical xor (`^^` / `xor`)
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

//  Discrete accumulation over records, where ExprInt integrates over time.
//  `Sum(v, q)` totals `v` across the states from here up to, but not
//  including, the first where `q` holds. Counting is the same operator with an
//  indicator body, which Antlr2AST builds for `Cnt`.
using ExprSum   = Final<SetOper<'Sum', Temporal<ExprBinary>>>;

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

//  A half-open slice of an array: `raw[lo:hi]` is the elements from `lo` up to
//  but not including `hi`. Its extent is a *value*, not a compile-time
//  constant -- which is the point. An array's own extent is the padded maximum
//  for the run, and a slice is how a specification says how much of that is
//  real, so a callee is handed a length rather than trusted with one.
class ExprSlice final
    : public Visitable<Expr, ExprSlice>
{
public:
    ExprSlice(Expr* arg, Expr* lo, Expr* hi)
        : arg(arg), lo(lo), hi(hi)
    {
    }

    bool    is_temporal() override
    {
        return arg->is_temporal() || lo->is_temporal() || hi->is_temporal();
    }

public:
    Expr* const     arg;
    Expr* const     lo;
    Expr* const     hi;
};

//  The position a quantifier's loop has reached.
//
//  A quantifier over an array with a written extent is expanded when the AST
//  is built: one term per element, each with the binder bound to a constant.
//  Over an array that carries its own length there is nothing to expand -- the
//  count arrives with the data -- so the body is built *once* with this node
//  standing in for the index, and code generation supplies a value per
//  iteration.
//
//  It is a leaf with no meaning outside the `ExprCount` that introduces it,
//  which is why it carries an id: two quantifiers in one expression are two
//  different `i`s, and interning by name alone would make them one.
class ExprBinder final
    : public Visitable<ExprNullary, ExprBinder>
{
public:
    ExprBinder(unsigned id, std::string name)
        : id(id)
        , name(name)
    {
    }

public:
    unsigned const      id;
    std::string const   name;
};

//  How many of an array's elements satisfy a body -- `Cnt` over elements
//  rather than over states.
//
//  Every quantifier form is a comparison on this: `all` is "as many as there
//  are", `none` is zero, `one` is one, `at least k` is `>= k`. That is the
//  same reduction the counting forms already used when they expanded to a sum
//  of indicators, except it is one node rather than n, which is the only form
//  available when n is not known until the trace is read.
//
//  It does cost `all` and `some` their short circuit: the loop always runs to
//  the end. Expansion never short-circuited either -- it built every term and
//  let the folded `&&` do it -- so nothing is lost that was there before.
class ExprCount final
    : public Visitable<Expr, ExprCount>
{
public:
    ExprCount(Expr* arg, ExprBinder* binder, Expr* body)
        : arg(arg)
        , binder(binder)
        , body(body)
    {
    }

    bool    is_temporal() override
    {
        return arg->is_temporal() || body->is_temporal();
    }

public:
    Expr* const         arg;
    ExprBinder* const   binder;
    Expr* const         body;
};

//  A call to an external function. Variadic in a way no other node is, so it
//  carries its arguments in a vector rather than as fixed lhs/rhs members.
class ExprCall final
    : public Visitable<Expr, ExprCall>
{
public:
    ExprCall(std::string name, std::vector<Expr*> args)
        : name(name)
        , args(args)
    {
    }

    //  A call is temporal exactly when one of its arguments is: the callee
    //  sees values, never states, so it cannot introduce time-dependence of
    //  its own.
    bool    is_temporal() override
    {
        for(auto* arg: args)
            if(arg->is_temporal())
                return true;

        return false;
    }

public:
    std::string const           name;
    std::vector<Expr*> const    args;
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
    SpecResponseChain21(Expr* S, Expr* T, Expr* P, Time* tST, Time* tTP, Expr* cST, Expr* cTP)
        : S(S), T(T), P(P), tST(tST), tTP(tTP), cST(cST), cTP(cTP)
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
        : Visitable(spec)
    {
    }
};

class SpecBefore
    : public Visitable<SpecScoped, SpecBefore>
{
public:
    SpecBefore(Expr* arg, Spec* spec)
        : Visitable(spec)
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
        : Visitable(spec)
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
        : Visitable(spec)
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
        : Visitable(spec)
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


