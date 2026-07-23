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

//  The half of ingest that parses a `.ref`. Kept apart from `ingestWithModule`
//  (rdb/ingest.cpp) because parsing pulls in ANTLR and, through
//  `Referee::parseSchema`, the front end -- which the ahead-of-time checker's
//  runtime library must not link. The runtime library compiles ingest.cpp; this
//  file lives only in the main build.

#include "ingest.hpp"

#include "referee.hpp"
#include "loaders/row.hpp"
#include "module.hpp"

#include <fmt/format.h>

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace referee::db
{

void    ingest(std::istream&        refIn,   std::string const& refName,
               std::istream&        dataIn,  std::string const& dataName,
               std::istream*        confIn,  std::string const& confName,
               std::ostream&        out,
               std::vector<std::string> const& includePaths)
{
    //  The trace has to be in hand before the schema is finished: a `T[]`
    //  array's extent is read off its columns. `inferSizes` needs the doc, so
    //  open it, learn the extents, then parse the schema against them.
    std::string refSrc{std::istreambuf_iterator<char>(refIn),
                       std::istreambuf_iterator<char>()};

    //  Snapshot the data stream so it can be read once for extents and again
    //  for the blobs.
    std::string dataSrc{std::istreambuf_iterator<char>(dataIn),
                        std::istreambuf_iterator<char>()};

    Referee::Sizes  sizes;
    {
        std::istringstream  probe(dataSrc);
        auto                doc = loader::Row::open(probe, dataName);
        sizes = inferSizes(*doc);
    }

    std::istringstream  refForSchema(refSrc);
    auto    schema = Referee::parseSchema(refForSchema, refName, includePaths, sizes);

    std::istringstream  dataForBlobs(dataSrc);
    ingestWithModule(dataForBlobs, dataName, confIn, confName, schema.ast, out);
}

void    ingest(std::string const& refPath,
               std::string const& dataPath,
               std::string const& confPath,
               std::string const& outRdbPath,
               std::vector<std::string> const& includePaths)
{
    std::ifstream   refIn(refPath);
    if (!refIn)
        throw std::runtime_error(fmt::format("rdb: cannot open '{}'", refPath));

    std::ifstream   dataIn(dataPath);
    if (!dataIn)
        throw std::runtime_error(fmt::format("rdb: cannot open '{}'", dataPath));

    std::ifstream   confIn;
    std::istream*   confInPtr = nullptr;
    if (!confPath.empty())
    {
        confIn.open(confPath);
        if (!confIn)
            throw std::runtime_error(fmt::format("rdb: cannot open '{}'", confPath));
        confInPtr = &confIn;
    }

    std::ofstream   out(outRdbPath, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error(fmt::format("rdb: cannot create '{}'", outRdbPath));

    ingest(refIn, refPath, dataIn, dataPath, confInPtr, confPath, out, includePaths);
}

} // namespace referee::db
