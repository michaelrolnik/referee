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

/*
 *  The driver of a standalone ahead-of-time checker. `referee build
 *  --executable spec.ref -o checker` links this against the compiled
 *  specification (which defines `referee_module`) and the runtime library, so
 *  the result validates `.rdb` traces with no LLVM, no ANTLR, and no `.ref`.
 *
 *  It is deliberately small and depends only on the runtime half of the
 *  codebase: the `.rdb` Reader, the type classes, string interning, and the
 *  schema codec. `referee execute --checker` runs the same logic in-process by
 *  dlopen; this is the same thing linked, so a machine that only checks logs
 *  needs neither referee nor its dependencies.
 */

#include "rdb/database.hpp"
#include "strings.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

//  The checker ABI (runtime/referee_checker.h), redeclared so this file needs
//  no include path into the header's directory. `referee_module` resolves to
//  the compiled specification linked beside this driver.
extern "C"
{
struct referee_requirement_v1
{
    char const* label;
    bool      (*eval)(void const*, void const*, void const*);
};
struct referee_module_v1
{
    std::uint32_t                       version;
    std::uint32_t                       count;
    referee_requirement_v1 const*       requirements;
    void                              (*prepare)(void*, void*, void const*);
    std::uint8_t const*                 schema;
    std::uint64_t                       schemaBytes;
    char const***                       strings;
    std::uint64_t                       stringCount;
};
const referee_module_v1*    referee_module(void);
}

int     main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s trace.rdb [trace.rdb ...]\n", argv[0]);
        return 2;
    }

    auto const* mod = referee_module();
    if (mod == nullptr || mod->version != 1)
    {
        std::fprintf(stderr, "checker: unsupported module version\n");
        return 2;
    }

    //  Re-intern the compiled string literals through this process's table, so
    //  a literal and a trace's string of equal content share one pointer and
    //  equality stays a pointer compare. This object was built in another
    //  process, so the slots start at their own bytes.
    for (std::uint64_t i = 0; i < mod->stringCount; i++)
        *mod->strings[i] = Strings::instance()->getString(*mod->strings[i]);

    //  The schema the checker was built against, decoded once.
    std::vector<referee::db::PropDecl>  props, confs;
    std::vector<std::unique_ptr<Type>>  sink;
    if (mod->schema != nullptr && mod->schemaBytes > 0)
    {
        auto const* cur = mod->schema;
        auto const* end = mod->schema + mod->schemaBytes;
        referee::db::decodeSchema(cur, end, props, confs, sink);
    }

    bool    everyTraceOk = true;

    for (int a = 1; a < argc; a++)
    {
        std::string     path = argv[a];
        try
        {
            referee::db::Reader rdb(path);

            //  Reject a trace whose shape differs from what the checker was
            //  built for, rather than read it into the wrong layout.
            auto const& rp = rdb.props();
            auto const& rc = rdb.confs();
            bool    shapeOk = rp.size() == props.size() && rc.size() == confs.size();
            for (std::size_t i = 0; shapeOk && i < rp.size(); i++)
                shapeOk = rp[i].name == props[i].name
                       && referee::db::typesEqual(rp[i].type, props[i].type);
            for (std::size_t i = 0; shapeOk && i < rc.size(); i++)
                shapeOk = rc[i].name == confs[i].name
                       && referee::db::typesEqual(rc[i].type, confs[i].type);

            if (!shapeOk)
            {
                std::fprintf(stderr, "checker: '%s' has a different schema than this checker\n",
                             path.c_str());
                everyTraceOk = false;
                continue;
            }

            mod->prepare(const_cast<void*>(rdb.ptrFirst()),
                         const_cast<void*>(rdb.ptrLast()), rdb.confPtr());

            bool    allPass = true;
            for (std::uint32_t i = 0; i < mod->count; i++)
            {
                bool    ok = mod->requirements[i].eval(rdb.ptrFirst(), rdb.ptrLast(), rdb.confPtr());
                allPass &= ok;
                std::printf("%-40s %s\n", mod->requirements[i].label, ok ? "PASS" : "FAIL");
            }

            everyTraceOk &= allPass;
        }
        catch (std::exception const& e)
        {
            std::fprintf(stderr, "checker: %s: %s\n", path.c_str(), e.what());
            everyTraceOk = false;
        }
    }

    return everyTraceOk ? 0 : 1;
}
