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

#include "syntax.hpp"

#include <map>
#include <set>
#include <string>

class Module
{
public:
    Module(std::string name);

    void    addType(std::string name, Type* type);
    void    addProp(std::string name, Type* data);
    void    addConf(std::string name, Type* data);

    Type*   getType(std::string name);
    Type*   getProp(std::string name);
    Type*   getConf(std::string name);

    bool    hasType(std::string name);
    bool    hasData(std::string name);
    bool    hasConf(std::string name);

    std::vector<std::string>    getTypeNames()  {return m_typeNames;}
    std::vector<std::string>    getPropNames()  {return m_propNames;}
    std::vector<std::string>    getConfNames()  {return m_confNames;}

    void    pushContext(std::string name);
    void    popContext();
    bool    hasContext( std::string name);

    void    addExpr(    Expr*   expr);
    std::vector<Expr*> const&   getExprs();

    void    addSpec(    Spec*   spec);
    std::vector<Spec*> const&   getSpecs();

private:
    std::map<std::string, Type*>    m_name2type;
    std::map<std::string, Type*>    m_name2data;
    std::map<std::string, Type*>    m_name2conf;
    std::vector<Expr*>              m_exprs;
    std::vector<Spec*>              m_specs;
    std::vector<std::string>        m_context;

    std::vector<std::string>        m_propNames;
    std::vector<std::string>        m_confNames;
    std::vector<std::string>        m_typeNames;
};