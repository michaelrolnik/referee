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

#include "ingest.hpp"

#include "database.hpp"
#include "referee.hpp"
#include "loaders/row.hpp"
#include "module.hpp"
#include "syntax.hpp"
#include "visitors/loader.hpp"

#include <fmt/format.h>

#include <fstream>
#include <limits>
#include <stdexcept>

namespace
{

// CSV/YAML cells are looked up by `__time__` for the time column.
std::int64_t    timeAt(loader::Row& doc, std::size_t row)
{
    try { return std::stoll(doc.cell("__time__", row)); }
    catch (...) { return 0; }
}

} // namespace

namespace referee::db
{

//  Read array extents off a trace's flattened column names. `readings[0]`,
//  `readings[1]`, … means three elements; `g[0][1]` contributes to both
//  dimensions. Reported per *path*, outermost dimension first, which is the
//  order the subscripts are written in.
//
//  The key is the dotted path with subscripts elided, so `msg[0].raw[3]`
//  records an extent for `msg` and another for `msg.raw`. Keying by the
//  leading name alone -- which is what this did originally -- meant a nested
//  array had nowhere to record its extent, so `struct { raw : byte[] }` could
//  not be resolved, and two arrays in one struct would have merged their
//  extents into one vector by position.
Referee::Sizes  inferSizes(loader::Row const& doc)
{
    std::map<std::string, std::vector<unsigned>>    out;

    for (auto const& col : doc.columnNames())
    {
        std::string path;

        for (std::size_t pos = 0; pos < col.size(); )
        {
            //  a name segment, up to the next subscript or dot
            auto    beg = pos;
            while (pos < col.size() && col[pos] != '[' && col[pos] != '.')
                pos++;

            if (pos > beg)
                path += (path.empty() ? "" : ".") + col.substr(beg, pos - beg);

            //  every subscript group that follows belongs to this path
            for (unsigned dim = 0; pos < col.size() && col[pos] == '['; dim++)
            {
                auto    close = col.find(']', pos);
                if (close == std::string::npos)
                    break;

                unsigned    index = 0;
                try         { index = std::stoul(col.substr(pos + 1, close - pos - 1)); }
                catch (...) { break; }

                auto&   dims = out[path];
                if (dims.size() <= dim)
                    dims.resize(dim + 1, 0);
                dims[dim] = std::max(dims[dim], index + 1);

                pos = close + 1;
            }

            if (pos < col.size() && col[pos] == '.')
                pos++;
        }
    }

    return out;
}


void    ingestWithModule(std::istream&        dataIn,  std::string const& dataName,
                         std::istream*        confIn,  std::string const& confName,
                         ::Module*            astModule,
                         std::ostream&        out)
{
    //  The trace determines a ragged array's per-record length, so the
    //  document is opened before the blobs are built. The schema is given,
    //  fully resolved -- no parse, no LLVM, no ANTLR.
    auto        doc     = loader::Row::open(dataIn, dataName);

    //  Column extents read off the header -- how far to probe for a ragged
    //  array's elements, nothing more.
    Referee::Sizes  sizes = referee::db::inferSizes(*doc);

    //  Computed props (`data x = expr`) are deliberately absent from the .rdb:
    //  nothing in the trace file backs them, and their values are a function of
    //  the spec rather than of the recording. `referee execute` materialises
    //  them from the .ref at run time (see __prepare__), so a .rdb stays valid
    //  when only a computed prop's defining expression changes.
    std::vector<std::string> propNames;
    for (auto const& n : astModule->getPropNames())
    {
        if (!astModule->isExprData(n))
            propNames.push_back(n);
    }

    auto    confNames   = astModule->getConfNames();
    std::size_t     numRows     = doc->rowCount();
    std::size_t     numProps    = propNames.size();
    std::size_t     numStates   = numRows + 2;          //  sentinels at 0 and N-1

    auto    zeroBlob    = [&](Type* t)
    {
        std::vector<std::uint8_t>   b(t->size(), 0);
        return b;
    };

    // states[state][prop]
    std::vector<blob_t>     blobs(numStates, blob_t(numProps));
    std::vector<std::int64_t>   times(numStates, 0);

    for (std::size_t row = 0; row < numRows; row++)
    {
        std::size_t     si = row + 1;
        times[si] = timeAt(*doc, row);
        for (std::size_t pi = 0; pi < numProps; pi++)
        {
            auto const& name    = propNames[pi];
            auto*       type    = astModule->getProp(name);
            blobs[si][pi].clear();
            Loader::load(blobs[si][pi], name, type,
                [&](std::string const& col) { return doc->cell(col, row); },
                sizes);
        }
    }

    // Sentinels: zero blobs, time just outside the data window.
    {
        constexpr auto kMin = std::numeric_limits<std::int64_t>::min();
        constexpr auto kMax = std::numeric_limits<std::int64_t>::max();
        std::int64_t firstT = numRows ? times[1]       : 0;
        std::int64_t lastT  = numRows ? times[numRows] : 0;
        times[0]            = firstT > kMin ? firstT - 1 : kMin;
        times[numStates - 1]= lastT  < kMax ? lastT  + 1 : kMax;
        for (std::size_t pi = 0; pi < numProps; pi++)
        {
            auto* t = astModule->getProp(propNames[pi]);
            blobs[0][pi]            = zeroBlob(t);
            blobs[numStates - 1][pi]= zeroBlob(t);
        }
    }

    // 3. Conf blob: same alignment + Loader::load rules as Referee::execute.
    std::vector<std::uint8_t>   confBlob;
    if (!confNames.empty())
    {
        if (confIn)
        {
            auto    confDoc = loader::Row::open(*confIn, confName);
            for (auto const& cname : confNames)
            {
                auto* ctype = astModule->getConf(cname);
                alignBuffer(confBlob, ctype->alignment());
                Loader::load(confBlob, cname, ctype,
                    [&](std::string const& col) { return confDoc->cell(col, 0); },
                    referee::db::inferSizes(*confDoc));
            }
        }
        else
        {
            for (auto const& cname : confNames)
            {
                auto* ctype = astModule->getConf(cname);
                alignBuffer(confBlob, ctype->alignment());
                confBlob.resize(confBlob.size() + ctype->size(), 0);
            }
        }
        std::size_t     maxAlign = 1;
        for (auto const& cname : confNames)
            maxAlign = std::max(maxAlign, astModule->getConf(cname)->alignment());
        while (confBlob.size() % maxAlign) confBlob.push_back(0);
    }
    if (confBlob.empty()) confBlob.push_back(0);

    // 4. Hand it all to the Writer.
    std::vector<PropDecl>   propDecls;
    propDecls.reserve(numProps);
    for (auto const& n : propNames)
        propDecls.push_back({n, astModule->getProp(n)});

    std::vector<ConfDecl>   confDecls;
    confDecls.reserve(confNames.size());
    for (auto const& n : confNames)
        confDecls.push_back({n, astModule->getConf(n)});

    Writer  w(out);
    w.setSchema(std::move(propDecls), std::move(confDecls));
    w.setNumStates(numStates);
    w.setConfBlob(std::move(confBlob));
    for (std::size_t si = 0; si < numStates; si++)
        w.writeState(si, times[si], blobs[si]);
    w.finish();
}

} // namespace referee::db
