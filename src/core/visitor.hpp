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
#include <memory>
#include <cxxabi.h>
#include <sstream>

template<typename ... Types>
class Visitor
    : public Visitor<Types>...
{
public:
    virtual ~Visitor() = default;
};

template<typename Type>
class Visitor<Type>
    : public virtual Visitor<>
{
public:
    virtual void visit(Type*) = 0;
};

class VisitorException
    : public std::runtime_error
{
public:  
    VisitorException(std::string message)
        : std::runtime_error(message)
    {   
    }
};

template<typename Base = void, typename Type = void>
class Visitable
    : public Base
{
public:
    using visitable  = void;

    template<typename ... Args>
    Visitable(Args ... args)
        : Base(args ...)
    {
    }

    virtual ~Visitable() = default;

    virtual void accept(Visitor<>& visitor)
    {
        accept_impl(visitor, 0);
    }
    
private:

    template<typename Hack = Base>
    void  accept_impl(Visitor<>& visitor, int)
    {
        Type* self  = dynamic_cast<Type*>(this);
        auto  temp  = dynamic_cast<Visitor<Type>*>(&visitor);
        if(temp)
        {
            temp->visit(self);
        }
        else
        {
            try
            {
                Hack::accept(visitor);
            }
            catch(VisitorException& e)
            {
//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
                throw VisitorException(std::string(e.what()) + "\n" + error(visitor));
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
            }
        }
    }

    template<typename Hack = Base>
    void accept_impl(Visitor<>& visitor, float)
    {
        Type* self  = dynamic_cast<Type*>(this);
        auto  temp  = dynamic_cast<Visitor<Type>*>(&visitor);
        if(temp)
        {
            temp->visit(self);
        }
        else
        {
//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
            throw VisitorException(error(visitor));
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
        }
    }

    std::string error(Visitor<>& visitor)
    {
        std::unique_ptr<char, decltype(free)*> vstr(nullptr, &free);
        std::unique_ptr<char, decltype(free)*> self(nullptr, &free);

        vstr.reset(abi::__cxa_demangle(typeid(visitor).name(), 0, 0, nullptr));
        self.reset(abi::__cxa_demangle(typeid(Type).name(), 0, 0, nullptr));

        std::ostringstream  os;
        os  << vstr.get() << " does not implement any visitor for " << self.get();

        return os.str();
    }
};

template<>
class Visitable<void, void>
{
public:
    virtual ~Visitable() = default;
    virtual void  accept(Visitor<>& visitor)
    {
//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
        throw VisitorException("");
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
    }
};

