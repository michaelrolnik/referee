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

#include <tuple>
#include <map>
#include <iostream>


template<typename T>
class Factory
{
public:
    template<typename ... Args>
    static T*  create(Args ... args)
    {
        auto    key = std::tuple<Args...>(args...);
        auto&   obj = get<decltype(key), T>(key);

        if(!obj)
        {
            obj = std::make_unique<T>(args...);
        }

        return obj.get();
    }

    template<typename ... Args>
    static T*  create(Position where, Args ... args)
    {
        auto    obj = create(args...);
        obj->where(where);
        return obj;
    }

private:
    template<typename Key, typename Val>
    static std::unique_ptr<Val>&    get(Key const key)
    {
        static std::map<Key, std::unique_ptr<Val>>  storage;

        return  storage[key];
    }
};
