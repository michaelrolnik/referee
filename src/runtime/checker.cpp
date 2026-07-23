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
#include "rdb/ingest.hpp"
#include "module.hpp"
#include "strings.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

//  The checker ABI. One header serves the emitter, both drivers and the test
//  harness, so the struct cannot drift between them -- the drift is exactly
//  how an i1 return came to be read as a 4-byte int once.
#include "referee_checker.h"

int     main(int argc, char** argv)
{
    auto const* mod = referee_module();
    if (mod == nullptr || mod->version != 1)
    {
        std::fprintf(stderr, "checker: unsupported module version\n");
        return 2;
    }

    //  Traces are positional; `--conf FILE` supplies a configuration for the
    //  CSV/YAML paths (a `.rdb` carries its own). Collected up front so the
    //  order of flags and traces does not matter.
    bool                        wantHelp = false;
    std::string                 confPath;
    std::vector<std::string>    tracePaths;
    for (int a = 1; a < argc; a++)
    {
        std::string arg = argv[a];
        if (arg == "--help" || arg == "-h")     wantHelp = true;
        else if (arg == "--conf" && a + 1 < argc) confPath = argv[++a];
        else                                    tracePaths.push_back(arg);
    }

    if (tracePaths.empty() || wantHelp)
    {
        //  A checker built by `referee build --executable` carries its own
        //  identity: what it checks and what shape of trace it expects. Print
        //  that rather than a bare usage line, since there is no `.ref` beside
        //  it to consult.
        std::printf("usage: %s trace.{rdb,csv,yaml} [trace ...]\n\n", argv[0]);
        std::printf("An ahead-of-time referee checker: %u requirement%s over a "
                    "trace of these signals.\n",
                    mod->count, mod->count == 1 ? "" : "s");
        std::printf("It reads .rdb, .csv and .yaml traces, and exits 0 "
                    "iff every requirement holds.\n");

        if (mod->schema != nullptr && mod->schemaBytes > 0)
        {
            std::vector<referee::db::PropDecl>  props, confs;
            std::vector<std::unique_ptr<Type>>  sink;
            auto const* cur = mod->schema;
            referee::db::decodeSchema(cur, mod->schema + mod->schemaBytes, props, confs, sink);

            std::printf("\ndata signals (%zu):", props.size());
            for (auto const& p : props) std::printf(" %s", p.name.c_str());
            std::printf("\n");
            if (!confs.empty())
            {
                std::printf("conf values  (%zu):", confs.size());
                for (auto const& c : confs) std::printf(" %s", c.name.c_str());
                std::printf("\n");
            }
        }
        return tracePaths.empty() && !wantHelp ? 2 : 0;
    }

    //  Re-intern the compiled string literals through this process's table, so
    //  a literal and a trace's string of equal content share one pointer and
    //  equality stays a pointer compare. This object was built in another
    //  process, so the slots start at their own bytes.
    for (std::uint64_t i = 0; i < mod->stringCount; i++)
        *mod->strings[i] = Strings::instance()->getString(*mod->strings[i]);

    //  The schema the checker was built against, decoded once. It doubles as
    //  the `Module` that packs a CSV/YAML trace -- rebuilt from the embedded
    //  types rather than from a `.ref`, which the checker does not carry.
    std::vector<referee::db::PropDecl>  props, confs;
    std::vector<std::unique_ptr<Type>>  sink;
    ::Module                            schema("checker");
    if (mod->schema != nullptr && mod->schemaBytes > 0)
    {
        auto const* cur = mod->schema;
        auto const* end = mod->schema + mod->schemaBytes;
        referee::db::decodeSchema(cur, end, props, confs, sink);
        for (auto const& p : props) schema.addProp(p.name, p.type);
        for (auto const& c : confs) schema.addConf(c.name, c.type);
    }

    auto    endsWith = [](std::string const& s, char const* ext)
    {
        auto    n = std::strlen(ext);
        return s.size() >= n && s.compare(s.size() - n, n, ext) == 0;
    };

    //  A `.rdb` is already the state buffer; anything else is packed against
    //  the embedded schema first, exactly as `rdb build` would, but with no
    //  `.ref` to parse.
    auto    openTrace = [&](std::string const& path) -> std::unique_ptr<referee::db::Reader>
    {
        if (endsWith(path, ".rdb"))
            return std::make_unique<referee::db::Reader>(path);

        std::ifstream       data(path);
        if (!data)
            throw std::runtime_error("cannot open '" + path + "'");

        std::ifstream       conf;
        if (!confPath.empty())
        {
            conf.open(confPath);
            //  Refuse rather than zero-fill: verdicts against thresholds
            //  nobody set are worse than no verdicts.
            if (!conf)
                throw std::runtime_error("cannot open conf '" + confPath + "'");
        }

        std::ostringstream  packed;
        referee::db::ingestWithModule(data, path,
                                      confPath.empty() ? nullptr : &conf, confPath,
                                      &schema, packed);

        auto                str = std::move(packed).str();
        std::vector<std::uint8_t>   bytes(str.begin(), str.end());
        return std::make_unique<referee::db::Reader>(std::move(bytes), path);
    };

    bool    everyTraceOk = true;

    for (auto const& path : tracePaths)
    {
        try
        {
            auto    rdbPtr = openTrace(path);
            auto&   rdb    = *rdbPtr;

            //  Reject a trace whose shape differs from what the checker was
            //  built for, rather than read it into the wrong layout. (A packed
            //  CSV already matches, since it was packed against this schema;
            //  the check still guards a hand-built `.rdb`.)
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

            //  Drain the out-of-bounds channel after each eval: an index
            //  outside its array answered from a zeroed buffer and raised the
            //  flag, and it is this driver's job to turn that into a failure
            //  -- same policy as `referee execute`.
            auto    faulted = [&]() -> bool
            {
                if (mod->oobFlag == nullptr || *mod->oobFlag == 0)
                    return false;
                *mod->oobFlag = 0;
                return true;
            };

            if (faulted())
                std::printf("%-40s FAIL  index %lld is outside an array of %lld\n",
                            "<computed signal>",
                            (long long)(mod->oobIndx ? *mod->oobIndx : 0),
                            (long long)(mod->oobCnt  ? *mod->oobCnt  : 0));

            bool    allPass = true;
            for (std::uint32_t i = 0; i < mod->count; i++)
            {
                bool        ok = mod->requirements[i].eval(rdb.ptrFirst(), rdb.ptrLast(), rdb.confPtr());
                long long   ix = mod->oobIndx ? *mod->oobIndx : 0;
                long long   ct = mod->oobCnt  ? *mod->oobCnt  : 0;

                if (faulted())
                {
                    ok = false;
                    std::printf("%-40s FAIL  index %lld is outside an array of %lld\n",
                                mod->requirements[i].label, ix, ct);
                }
                else
                    std::printf("%-40s %s\n", mod->requirements[i].label, ok ? "PASS" : "FAIL");

                allPass &= ok;
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
