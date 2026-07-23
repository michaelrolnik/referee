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

#include <iosfwd>
#include <string>
#include <vector>

#include "referee.hpp"
#include "loaders/row.hpp"

namespace referee::db
{

/// Stream-based ingest: build a `.rdb` slab into `out` from a REF schema
/// (`refIn`/`refName`), a tabular trace (`dataIn`/`dataName`, `.csv`/`.yml`),
/// and an optional configuration table (`confIn`/`confName`, same formats —
/// pass `confIn == nullptr` for "no conf file").
///
/// The output is the exact slab `Referee::execute` consumes (state buffer +
/// prop-blobs + interned strings); see rdb/database.hpp for the layout.
/// Sentinel rows are added at indices 0 and N-1, mirroring what
/// `Referee::execute` does for CSV traces.
///
/// `*Name` arguments are used both for loader extension probing
/// (`.csv` vs `.yaml`) and for error messages.
/// `includePaths` are forwarded to the schema parse, so a spec that pulls its
/// `data`/`conf` declarations in via `import` packs the same as an inline one.
/// Array extents read off a trace's flattened column names, for a
/// specification that leaves them out. Outermost dimension first.
Referee::Sizes  inferSizes(loader::Row const& doc);

void    ingest(std::istream&        refIn,   std::string const& refName,
               std::istream&        dataIn,  std::string const& dataName,
               std::istream*        confIn,  std::string const& confName,
               std::ostream&        out,
               std::vector<std::string> const& includePaths = {});

/// Pack a trace against an already-built schema `Module`, without parsing a
/// `.ref`. This is the LLVM-and-ANTLR-free half of ingest: the `ingest`
/// overloads above are just a `parseSchema` in front of it. An ahead-of-time
/// checker, which carries its schema rather than a specification, calls this
/// directly to accept CSV/YAML the same way the JIT path does.
void    ingestWithModule(std::istream&        dataIn,  std::string const& dataName,
                         std::istream*        confIn,  std::string const& confName,
                         ::Module*            astModule,
                         std::ostream&        out);

/// File-paths convenience wrapper around the stream-based variant.
/// `confPath` may be empty for "no conf file".
void    ingest(std::string const& refPath,
               std::string const& dataPath,
               std::string const& confPath,
               std::string const& outRdbPath,
               std::vector<std::string> const& includePaths = {});

} // namespace referee::db
