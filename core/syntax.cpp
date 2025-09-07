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

#include <algorithm>

#include "syntax.hpp"
#include "module.hpp"
#include "factory.hpp"

ExprNullary::ExprNullary()
{
}

ExprUnary::ExprUnary(int op, Expr* arg)
    : op(op)
    , arg(arg)
{
}

bool ExprUnary::is_temporal()
{
    return arg->is_temporal();
}

ExprBinary::ExprBinary(int op, Expr* lhs, Expr* rhs)
    : op(op)
    , lhs(lhs)
    , rhs(rhs)
{
}

bool ExprBinary::is_temporal()
{
    return lhs->is_temporal() || rhs->is_temporal();
}

ExprTernary::ExprTernary(int op, Expr* lhs, Expr* mhs, Expr* rhs)
    : op(op)
    , lhs(lhs)
    , mhs(mhs)
    , rhs(rhs)
{
}

bool ExprTernary::is_temporal()
{
    return lhs->is_temporal() || mhs->is_temporal() || rhs->is_temporal();
}

Time::Time(Expr* lo, Expr* hi)
    : Visitable<Expr, Time>()
    , lo(lo)
    , hi(hi)
{
}

TimeMin::TimeMin(Expr* lo)
    : Visitable<Time, TimeMin>(lo, nullptr)
{
}

TimeMax::TimeMax(Expr* hi)
    : Visitable<Time, TimeMax>(nullptr, hi)
{
}

TypeContext::TypeContext(Module* module)
    : m_module(module)
{
}

Type*   TypeContext::member(std::string member)
{
    if(m_module->hasConf(member))
    {
        return m_module->getConf(member);
    }

    return  m_module->getProp(member);
}

unsigned    TypeContext::index(std::string name)
{
    auto    names   = m_module->hasConf(name) 
                    ? m_module->getConfNames() 
                    : m_module->getPropNames();
    auto    index   = 0;

    for(auto it: names)
    {
        if(it == name)
            return index;

        index ++;
    }
    
//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
    throw std::runtime_error(__PRETTY_FUNCTION__);
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
}


TypeStruct::TypeStruct(std::vector<Named<Type>> members)
    : members(members)
{
    for(auto member: members)
    {
        m_name2type[member.name] = member.data;
        m_name2indx[member.name] = m_name2indx.size();
    }
}

Type*   TypeStruct::member(std::string name)
{
    if(m_name2type.contains(name))
    {
        return m_name2type[name];
    }

    return nullptr;
}

unsigned    TypeStruct::index(std::string name)
{
    if(m_name2indx.contains(name))
    {
        return m_name2indx[name];
    }

//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
    throw std::runtime_error(__PRETTY_FUNCTION__);
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
}


TypeArray::TypeArray(Type* type, unsigned size)
    : type(type)
    , size(size)
{
}

TypeEnum::TypeEnum(std::vector<std::string>   items)
    : items(items)
{
    for(auto item: items)
    {
        m_name2indx[item] = m_name2indx.size();
    }
}

Type*   TypeEnum::member(std::string name)
{
    if(std::find(items.begin(), items.end(), name) != items.end())
    {
        return  Factory<TypeBoolean>::create();
    }

    return  nullptr;
}

unsigned    TypeEnum::index(std::string name)
{
    if(m_name2indx.contains(name))
    {
        return m_name2indx[name] + 1;
    }

//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
    throw std::runtime_error(__PRETTY_FUNCTION__);
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
}

SpecWhile::SpecWhile(Expr* arg, Spec* spec)
    : Visitable<SpecBetweenAnd, SpecWhile>(arg, Factory<ExprNot>::create(arg), spec)
{
}
