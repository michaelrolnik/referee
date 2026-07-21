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

#include <algorithm>

Module::Module(std::string name)
{
    m_name2type["boolean"]  = Factory<TypeBoolean>::create();
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

Type*   Module::getType(std::string const& name)
{
    if(!m_name2type.contains(name))
    {
//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
        throw std::runtime_error(__PRETTY_FUNCTION__);
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
        throw std::runtime_error(__PRETTY_FUNCTION__);
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
        throw std::runtime_error(__PRETTY_FUNCTION__);
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

void    Module::addExpr(    Expr*   expr)
{
    m_exprs.push_back(expr);
}

void    Module::addSpec(    Spec*   spec)
{
    m_specs.push_back(spec);
}

std::vector<Expr*> const&   Module::getExprs()
{
    return m_exprs;
}

std::vector<Spec*> const&   Module::getSpecs()
{
    return m_specs;
}
