/*
 *  The standard library: functions referee implements itself.
 *
 *  Deliberately *not* shipped as a .so. A user should not need a plugin
 *  directory to call `sqrt`, and a specification that uses only built-ins
 *  keeps the property that a .ref plus a trace determines the verdict --
 *  nothing external, nothing to version, nothing to install. They also need
 *  no header and no ABI, and work identically under a JIT and an AOT checker.
 */
#pragma once

#include "syntax.hpp"
#include "factory.hpp"

#include <string>
#include <vector>

struct  Builtin
{
    enum class Kind
    {
        IAbs, IMin, IMax,           //  emitted as IR: a compare and a select
        FAbs, FMin, FMax,           //  llvm.fabs / llvm.minnum / llvm.maxnum
        Sqrt, Exp, Log, Log2, Log10,
        Sin,  Cos,  Pow,
        Floor, Ceil, Round, Trunc,  //  number -> *integer*: REF has no cast,
                                    //  so this is the only way to convert

        //  Strings. Unlike the maths, these are host functions linked into
        //  referee and registered with the JIT -- the same mechanism `debug`
        //  uses -- because there is no intrinsic for them.
        //
        //  Every one returns a number or a boolean. None returns a *string*,
        //  and that is deliberate: with no allocator and no ownership model,
        //  a function that built a new string would have nobody to free it.
        //  So there is no substr, no concat, no to_upper.
        StrLen, StrAt, StrCmp, StrStarts, StrEnds, StrFind,
    };

    static bool     isString(Kind k)
    {
        return k >= Kind::StrLen;
    }

    char const*         name;
    Kind                kind;
    unsigned            arity;
    bool                takesNumber;    //  false: integer arguments
    bool                returnsNumber;  //  false: integer result
};

//  Looked up by name when a call names no declared `func`. Names are
//  namespaced, so `std::math::sqrt` cannot collide with anything a
//  specification declares for itself.
inline std::vector<Builtin> const&  builtins()
{
    static std::vector<Builtin> const   table = {
        { "std::math::abs",   Builtin::Kind::IAbs,  1, false, false },
        { "std::math::min",   Builtin::Kind::IMin,  2, false, false },
        { "std::math::max",   Builtin::Kind::IMax,  2, false, false },

        //  Overloads of the three above, on numbers. Resolution is by
        //  argument type, so `abs` serves both and C's `fabs` workaround --
        //  a second name for the want of overloading -- is not needed.
        { "std::math::abs",   Builtin::Kind::FAbs,  1, true,  true  },
        { "std::math::min",   Builtin::Kind::FMin,  2, true,  true  },
        { "std::math::max",   Builtin::Kind::FMax,  2, true,  true  },

        { "std::math::sqrt",  Builtin::Kind::Sqrt,  1, true,  true  },
        { "std::math::exp",   Builtin::Kind::Exp,   1, true,  true  },
        { "std::math::log",   Builtin::Kind::Log,   1, true,  true  },
        { "std::math::log2",  Builtin::Kind::Log2,  1, true,  true  },
        { "std::math::log10", Builtin::Kind::Log10, 1, true,  true  },
        { "std::math::sin",   Builtin::Kind::Sin,   1, true,  true  },
        { "std::math::cos",   Builtin::Kind::Cos,   1, true,  true  },
        { "std::math::pow",   Builtin::Kind::Pow,   2, true,  true  },

        //  These return an integer on purpose. REF has no cast, so
        //  number-to-integer conversion is otherwise not expressible at all,
        //  and rounding is where a specification actually wants it.
        { "std::math::floor", Builtin::Kind::Floor, 1, true,  false },
        { "std::math::ceil",  Builtin::Kind::Ceil,  1, true,  false },
        { "std::math::round", Builtin::Kind::Round, 1, true,  false },
        { "std::math::trunc", Builtin::Kind::Trunc, 1, true,  false },

        //  arity counts REF arguments; the string kinds take strings, with
        //  StrAt taking a string and an index.
        { "std::string::len",      Builtin::Kind::StrLen,    1, false, false },
        { "std::string::nth",       Builtin::Kind::StrAt,     2, false, false },
        { "std::string::compare",  Builtin::Kind::StrCmp,    2, false, false },
        { "std::string::starts",   Builtin::Kind::StrStarts, 2, false, false },
        { "std::string::ends",     Builtin::Kind::StrEnds,   2, false, false },
        { "std::string::find",     Builtin::Kind::StrFind,   2, false, false },
    };

    return table;
}

//  Every candidate of that name, in declaration order. More than one means
//  the name is overloaded, and the caller picks by argument type.
inline std::vector<Builtin const*>  findBuiltins(std::string const& name)
{
    std::vector<Builtin const*> out;

    for(auto const& b: builtins())
        if(name == b.name)
            out.push_back(&b);

    return out;
}

//  True when this candidate accepts a call of that shape. Argument types are
//  matched exactly -- an integer does not become a number to reach an
//  overload, because that would silently pick the wrong one whenever both
//  exist, which is precisely when it matters.
inline bool     builtinAccepts(Builtin const& b, unsigned arity, bool argsAreNumber)
{
    if(b.arity != arity)
        return false;

    return Builtin::isString(b.kind) ? true : b.takesNumber == argsAreNumber;
}

//  Any candidate at all, for diagnostics and for the string kinds, whose
//  argument shapes are checked separately.
inline Builtin const*   findBuiltin(std::string const& name)
{
    auto    all = findBuiltins(name);
    return all.empty() ? nullptr : all.front();
}
