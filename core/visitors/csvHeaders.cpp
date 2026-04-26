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

#include "csvHeaders.hpp"

struct CsvHeadersImpl
    : Visitor< Type
             , TypeStruct
             , TypeArray>
{
    std::vector<std::string>    make(std::string const& name, Type*   expr);

    void    visit(Type*         type) override;
    void    visit(TypeStruct*   type) override;
    void    visit(TypeArray*    type) override;

    std::vector<std::string>    m_headers;
    std::string                 m_name;
};

void    CsvHeadersImpl::visit(Type*         type)
{
    m_headers.push_back(m_name);
}

void    CsvHeadersImpl::visit(TypeStruct*   type)
{
    auto members    = type->members;
    for(auto member: members)
    {
        auto    headers     = CsvHeaders::make(member.name, member.data);

        for(auto header: headers)
        {
            m_headers.push_back(m_name + "." + header);
        }
    }
}

void    CsvHeadersImpl::visit(TypeArray*    type)
{
    std::vector<unsigned>   sizes;
    Type* curr  = type;

    while(auto array = dynamic_cast<TypeArray*>(curr))
    {
        sizes.push_back(array->count);
        curr    = array->type;
    }

    auto headers    = CsvHeaders::make("", curr);
    for(auto size: sizes)
    {
        if(size == 0)
        {
            m_headers.push_back("#size");
            size = 1;
        }
        
        for(auto i = 0u; i < size; i++)
        {
            auto index  = "[" + std::to_string(i) + "]";

            for(auto header: headers)
            {
                m_headers.push_back(index + header);
            }
        }

        headers     = std::move(m_headers);
        m_headers.clear();
    }

    for(auto header: headers)
    {
        m_headers.push_back(m_name + header);
    }
}

std::vector<std::string>    CsvHeadersImpl::make(std::string const& name, Type* type)
{
    m_name = name;

    type->accept(*this);

    return  m_headers;
}

std::vector<std::string>    CsvHeaders::make(std::string const& name, Type* type)
{
    CsvHeadersImpl impl;

    return impl.make(name, type);
}
