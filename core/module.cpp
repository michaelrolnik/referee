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


#include "module.hpp"
#include "factory.hpp"
#include <cstdio>
#include <cstdint>
#include "factory.hpp"

#include <algorithm>

Module::Module(std::string name)
{
    m_name2type["boolean"]  = Factory<TypeBoolean>::create();
    m_name2type["byte"]     = Factory<TypeByte>::create();
    m_name2type["integer"]  = Factory<TypeInteger>::create();
    m_name2type["string"]   = Factory<TypeString>::create();
    m_name2type["number"]   = Factory<TypeNumber>::create();

    m_name2data["__time__"] = Factory<TypeInteger>::create();
}

void    Module::addType(std::string const& name, Type* type)
{
    if(m_name2type.contains(name))
    {
        throw std::runtime_error(
            "duplicate type declaration '" + name + "'"
            " -- declared twice, possibly via two imports that both define it");
    }
    m_name2type[name]   = type;
    m_typeNames.push_back(name);
}

void    Module::addProp(std::string const& name, Type* data)
{
    if(m_name2data.contains(name))
    {
        throw std::runtime_error(
            "duplicate data declaration '" + name + "'"
            " -- declared twice, possibly via two imports that both define it");
    }
    m_name2data[name]   = data;
    m_propNames.push_back(name);
}

void    Module::addPropExpr(std::string const& name, Type* type, Expr* expr)
{
    if(m_name2data.contains(name))
    {
        throw std::runtime_error(
            "duplicate data declaration '" + name + "'"
            " -- declared twice, possibly via two imports that both define it");
    }
    m_name2data[name]   = type;     // in the shared type map → typecalc resolves it
    m_name2expr[name]   = expr;     // expression for ingest-time evaluation
    m_propNames.push_back(name);    // same list as CSV props → gets a __prop_t slot
}

void    Module::addConf(std::string const& name, Type* data)
{
    if(m_name2conf.contains(name))
    {
        throw std::runtime_error(
            "duplicate conf declaration '" + name + "'"
            " -- declared twice, possibly via two imports that both define it");
    }
    m_name2conf[name]   = data;
    m_confNames.push_back(name);
}

void    Module::addFunc(std::string const& name, std::vector<Type*> args, Type* ret, bool state)
{
    auto&   overloads = m_name2func[name];

    //  Two declarations of one name are fine; two with the same argument
    //  types are not, since no call could tell them apart. `(__state__)` and
    //  `()` are both empty argument lists at the call, so they collide too.
    for(auto const& have: overloads)
        if(have.args == args && have.state == state)
            throw std::runtime_error(
                "function '" + name + "' is declared more than once with the same argument types");

    if(args.empty())
        for(auto const& have: overloads)
            if(have.args.empty())
                throw std::runtime_error(
                    "function '" + name + "' is declared both with '(__state__)' and without it"
                    " -- a call gives no arguments either way, so nothing could tell them apart");

    if(overloads.empty())
        m_funcNames.push_back(name);

    overloads.push_back(Func{std::move(args), ret, state});
}

std::vector<Module::Func> const&    Module::funcsNamed(std::string const& name)
{
    static std::vector<Func> const  none;

    auto    it = m_name2func.find(name);
    return it == m_name2func.end() ? none : it->second;
}

Module::Func const*     Module::resolveFunc(std::string const& name, std::vector<Type*> const& args)
{
    for(auto const& cand: funcsNamed(name))
        if(cand.args == args)
            return &cand;

    return nullptr;
}

bool    Module::hasFunc(std::string const& name)
{
    return m_name2func.contains(name);
}

Module::Func const&     Module::getFunc(std::string const& name)
{
    auto const& overloads = funcsNamed(name);

    if(overloads.empty())
        throw std::runtime_error("no such function: '" + name + "'");

    return overloads.front();
}

Type*   Module::getType(std::string const& name)
{
    if(!m_name2type.contains(name))
    {
//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
        throw std::runtime_error("no such type: '" + name + "'");
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
    }
    return m_name2type[name];
}

Type*   Module::getProp(std::string const& name)
{
    if(!m_name2data.contains(name))
    {
//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
        throw std::runtime_error("no such signal: '" + name + "'");
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
    }
    return m_name2data[name];
}

Type*   Module::getConf(std::string const& name)
{
    if(!m_name2conf.contains(name))
    {
//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
        throw std::runtime_error("no such conf value: '" + name + "'");
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
    }
    return m_name2conf[name];
}

bool    Module::hasType(std::string const& name)
{
    return m_name2type.contains(name);
}

bool    Module::hasData(std::string const& name)
{
    return m_name2data.contains(name);
}

bool    Module::isExprData(std::string const& name)
{
    return m_name2expr.contains(name);
}

Expr*   Module::getPropExpr(std::string const& name)
{
    auto it = m_name2expr.find(name);
    return it != m_name2expr.end() ? it->second : nullptr;
}

bool    Module::hasConf(std::string const& name)
{
    return m_name2conf.contains(name);
}

void    Module::pushContext(std::string const& name)
{
    m_context.push_back(name);
}

void    Module::popContext()
{
    m_context.pop_back();
}

bool    Module::hasContext(std::string const& name)
{
    return std::find(m_context.begin(), m_context.end(), name) != m_context.end();
}

//  A requirement name has to be unique across the whole program, imports
//  included: it replaces the source position as the requirement's identity, so
//  two requirements sharing one would collide into a single generated function
//  and a single, ambiguous report line -- exactly the problem file-qualified
//  positions were introduced to solve.
void    Module::noteName(std::string const& name)
{
    if(name.empty())
        return;

    if(!m_reqNames.insert(name).second)
        throw std::runtime_error(
            "duplicate requirement name '@" + name + "'"
            " -- a name identifies one requirement across the whole program");
}

void    Module::addExpr(    Expr*   expr, std::string name)
{
    noteName(name);
    m_exprs.push_back(expr);
    m_exprNames.push_back(std::move(name));
}

std::string const&  Module::getExprName(std::size_t index) const
{
    return m_exprNames[index];
}

void    Module::addSpec(    Spec*   spec, std::string name)
{
    noteName(name);
    m_specs.push_back(spec);
    m_specNames.push_back(std::move(name));
}

std::string const&  Module::getSpecName(std::size_t index) const
{
    return m_specNames[index];
}

std::vector<Expr*> const&   Module::getExprs()
{
    return m_exprs;
}

std::vector<Spec*> const&   Module::getSpecs()
{
    return m_specs;
}

namespace {

//  A canonical spelling of everything a caller and a callee must agree on.
//  Names of *types* are deliberately absent: renaming a struct changes
//  nothing about how it is passed, while adding a field changes everything.
void    describe(Type* type, std::string& out)
{
    if(type == Factory<TypeBoolean>::create()) { out += "b;";  return; }
    if(type == Factory<TypeByte>::create())    { out += "h;";  return; }
    if(type == Factory<TypeInteger>::create()) { out += "i;";  return; }
    if(type == Factory<TypeNumber>::create())  { out += "n;";  return; }
    if(type == Factory<TypeString>::create())  { out += "s;";  return; }

    if(auto* e = dynamic_cast<TypeEnum*>(type))
    {
        //  Member order *is* the value: TypeEnum::index() is the declaration
        //  position plus one, so inserting a member renumbers every later one.
        out += "e(";
        for(auto const& item: e->items)
            out += item + ",";
        out += ");";
        return;
    }

    if(auto* a = dynamic_cast<TypeArray*>(type))
    {
        out += "a" + std::to_string(a->count) + "(";
        describe(a->type, out);
        out += ");";
        return;
    }

    if(auto* st = dynamic_cast<TypeStruct*>(type))
    {
        out += "t" + std::to_string(st->size()) + ":" + std::to_string(st->alignment()) + "(";
        for(auto const& m: st->members)
        {
            out += m.name + ":";
            describe(m.data, out);
        }
        out += ");";
        return;
    }

    out += "?;";
}

//  FNV-1a, truncated to eight hex digits. Not cryptographic: this detects
//  drift, it does not defend against a crafted collision. Fixed width matters
//  -- `__` already separates namespace components.
std::string     digest(std::string const& text)
{
    std::uint64_t   h = 1469598103934665603ull;

    for(unsigned char c: text)
    {
        h ^= c;
        h *= 1099511628211ull;
    }

    char    buf[9];
    std::snprintf(buf, sizeof(buf), "%08x", static_cast<unsigned>(h ^ (h >> 32)));

    return buf;
}

} // namespace

std::uint64_t   Module::stateLayoutVersion()
{
    std::string shape = "state(";

    for(auto const& name: m_propNames)
    {
        shape += name;
        shape += ":";
        describe(getProp(name), shape);
        shape += ";";
    }
    shape += ")";

    return std::stoull(digest(shape), nullptr, 16);
}

std::string     Module::symbolFor(std::string const& name, Func const& decl)
{
    std::string mangled = name;
    for(auto at = mangled.find("::"); at != std::string::npos; at = mangled.find("::", at))
        mangled.replace(at, 2, "__");

    std::string shape = name + "(";
    if(decl.state)
        shape += "__state__";
    for(auto* arg: decl.args)
        describe(arg, shape);
    shape += ")->";
    describe(decl.ret, shape);

    return "referee_" + mangled + "__" + digest(shape);
}
