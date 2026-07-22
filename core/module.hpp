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

#include "syntax.hpp"

#include <map>
#include <set>
#include <string>

class Module
{
public:
    Module(std::string name);

    void    addType(std::string const& name, Type* type);
    void    addProp(std::string const& name, Type* data);             // CSV-backed prop
    void    addPropExpr(std::string const& name, Type* type, Expr* expr); // computed prop
    void    addConf(std::string const& name, Type* data);

    //  An external function's signature. The body lives in a .so found on the
    //  -L path; nothing here refers to it, so a specification that declares a
    //  func still compiles without one -- it fails at JIT setup instead.
    struct  Func
    {
        std::vector<Type*>  args;
        Type*               ret;
    };

    //  A name may carry several signatures. Resolution is by the call's
    //  shape, matched exactly -- no widening to reach an overload, since that
    //  would silently pick the wrong one precisely when both exist.
    void                        addFunc(std::string const& name, std::vector<Type*> args, Type* ret);
    std::vector<Func> const&    funcsNamed(std::string const& name);
    Func const*                 resolveFunc(std::string const& name, std::vector<Type*> const& args);

    //  The C symbol a `func` binds to: `referee_`, the name with `::`
    //  replaced by `__`, then a structural hash of the signature.
    //
    //      func std::math::sqrt : (number) -> number;
    //          -> referee_std__math__sqrt__a3f19c02
    //
    //  The hash covers the *layout* each parameter and the return reaches --
    //  a struct's fields and their offsets, an enum's member order and
    //  values, an array's element type and extent -- not the spelling of the
    //  type's name. So adding a field to a struct changes the symbol even
    //  though the signature text is unchanged, which is the case nothing else
    //  detects, and two overloads of one name get different symbols, which is
    //  what makes overloading possible without C++ linkage.
    std::string     symbolFor(std::string const& name, Func const& decl);
    bool            hasFunc(std::string const& name);
    Func const&     getFunc(std::string const& name);   //  the sole overload; throws if ambiguous
    std::vector<std::string> const&  getFuncNames()  {return m_funcNames;}

    Type*   getType(std::string const& name);
    Type*   getProp(std::string const& name);               // type of any data (CSV or computed)
    Expr*   getPropExpr(std::string const& name);           // defining expression (null if CSV-backed)
    Type*   getConf(std::string const& name);

    bool    hasType(std::string const& name);
    bool    hasData(std::string const& name);               // true for both CSV and computed
    bool    hasConf(std::string const& name);
    bool    isExprData(std::string const& name);            // true only for computed data

    // getPropNames() returns ALL props in declaration order (CSV + computed).
    // This drives __prop_t slot layout — both kinds get a slot; ingest uses
    // isExprData() to decide whether to load from CSV or compute via JIT.
    std::vector<std::string> const&  getTypeNames()  {return m_typeNames;}
    std::vector<std::string> const&  getPropNames()  {return m_propNames;}
    std::vector<std::string> const&  getConfNames()  {return m_confNames;}

    void    pushContext(std::string const& name);
    void    popContext();
    bool    hasContext( std::string const& name);

    //  `name` is the optional `@name` a requirement was written with. It
    //  becomes the requirement's label in place of its source position, which
    //  is what lets an external corpus refer to it across edits.
    void    addExpr(    Expr*   expr, std::string name = "");
    std::vector<Expr*> const&   getExprs();
    std::string const&          getExprName(std::size_t index) const;

    void    addSpec(    Spec*   spec, std::string name = "");
    std::vector<Spec*> const&   getSpecs();
    std::string const&          getSpecName(std::size_t index) const;

private:
    std::map<std::string, Type*>    m_name2type;
    std::map<std::string, Type*>    m_name2data;  // ALL props (CSV + computed) for type lookup
    std::map<std::string, Expr*>    m_name2expr;  // only computed props
    std::map<std::string, Type*>    m_name2conf;
    std::map<std::string, std::vector<Func>>    m_name2func;
    std::vector<Expr*>              m_exprs;
    std::vector<Spec*>              m_specs;
    std::vector<std::string>        m_exprNames;    //  parallel to m_exprs
    std::vector<std::string>        m_specNames;    //  parallel to m_specs
    std::set<std::string>           m_reqNames;     //  for duplicate detection

    void    noteName(std::string const& name);
    std::vector<std::string>        m_context;

    std::vector<std::string>        m_propNames;  // ALL props in decl order → __prop_t slot layout
    std::vector<std::string>        m_confNames;
    std::vector<std::string>        m_funcNames;
    std::vector<std::string>        m_typeNames;
};