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
#include <type_traits>
#include <map>
#include <mutex>
#include <vector>
#include <utility>
#include <iostream>


//  Interning has two different right answers, and using one for both is the
//  bug this exists to fix.
//
//  A *type* is a value: `integer` is `integer` in every specification,
//  forever. Interning types process-wide is load-bearing -- every rule in
//  typecalc compares them by pointer -- and types are also built outside any
//  compilation, when a .rdb schema is decoded.
//
//  An *expression node* is not a value. It refers to a declaration in a
//  particular module and it carries mutable state: TypeCalc stamps the
//  resolved type onto it. Two specifications' `g` are different things that
//  happen to share a spelling, so sharing a node means the first one compiled
//  decides the second one's types.
//
//  So nodes belong to an Arena, one per compilation, and die with it. Which
//  also ends the other half of the problem: the interned maps were `static`
//  and never emptied, so every compilation leaked its whole AST for the life
//  of the process.
class Arena
{
public:
    Arena()                         = default;
    Arena(Arena const&)             = delete;
    Arena&  operator=(Arena const&) = delete;

    ~Arena();       //  defined after factoryMutex(): the sweep mutates the
                    //  factory maps, so it takes the same lock create() does

    //  Which arena new nodes belong to. Thread-local, so concurrent
    //  compilations do not fight over it -- the previous design would simply
    //  have raced.
    static Arena*&  current()
    {
        thread_local Arena* arena = nullptr;
        return arena;
    }

    //  Makes an arena current for a dynamic extent. Note it governs *which*
    //  arena is used, not how long the nodes live: lifetime belongs to the
    //  Arena object, which the compilation owns. A scope that freed on exit
    //  would hand back an AST of dangling pointers.
    class Scope
    {
    public:
        explicit Scope(Arena& arena) : m_prev(current()) { current() = &arena; }
        ~Scope()                                         { current() = m_prev; }

        Scope(Scope const&)             = delete;
        Scope&  operator=(Scope const&) = delete;

    private:
        Arena*  m_prev;
    };

    //  Each Factory<T>::get instantiation registers one of these the first
    //  time it is used, so an arena can empty every map that holds its nodes
    //  without anything having to know what those maps are.
    using Sweeper = void (*)(Arena*);

    static std::vector<Sweeper>&    sweepers()
    {
        static std::vector<Sweeper> list;
        return list;
    }
};

//  Opt in by declaring `using factory_scoped = void;`. Expr does, so every
//  expression node inherits it; Type does not, so types stay global.
template<typename T, typename = void>
struct  FactoryScoped : std::false_type {};

template<typename T>
struct  FactoryScoped<T, std::void_t<typename T::factory_scoped>> : std::true_type {};


//  One lock for every factory map and the sweeper list. The maps are function-
//  local statics mutated on every intern, so two threads compiling
//  concurrently -- the language server's ordinary future -- raced them.
//  Recursive, because constructing a node may itself intern (a nested type's
//  members, a rewritten child), re-entering create() on the same thread.
inline std::recursive_mutex&    factoryMutex()
{
    static std::recursive_mutex m;
    return m;
}

inline Arena::~Arena()
{
    std::lock_guard<std::recursive_mutex>   lock(factoryMutex());
    for(auto sweep: sweepers())
        sweep(this);
}

template<typename T>
class Factory
{
public:
    template<typename ... Args>
    static T*  create(Args ... args)
    {
        std::lock_guard<std::recursive_mutex>   lock(factoryMutex());

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
        //  The arena is part of the key rather than a separate store, which
        //  keeps lookup a single map probe and needs no type erasure.
        using Slot = std::pair<Arena*, Key>;

        static std::map<Slot, std::unique_ptr<Val>> storage;

        //  Registered once per instantiation. The lambda is captureless --
        //  `storage` has static storage duration, so it is referred to rather
        //  than captured -- which lets it convert to a plain function pointer.
        static bool const   registered = []
        {
            Arena::sweepers().push_back(+[](Arena* arena)
            {
                for(auto it = storage.begin(); it != storage.end(); )
                    it = (it->first.first == arena) ? storage.erase(it) : std::next(it);
            });
            return true;
        }();
        (void) registered;

        auto    owner = FactoryScoped<Val>::value ? Arena::current() : nullptr;

        return  storage[Slot{owner, key}];
    }
};
