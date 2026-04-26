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

#pragma once

// Referee Database (.rdb) — a packed binary trace file whose state-buffer
// section is *exactly* the `state_t[]` layout that `Referee::execute` feeds
// to JIT-compiled requirement functions:
//
//   struct state_t {
//       int64_t  time;
//       void*    prop[numProps];   //  one pointer per `data` declaration
//   };
//
// Per-prop blobs live in their own section, byte-for-byte identical to what
// `Loader::load` produces from CSV/YAML for the corresponding AST type. At
// read time the only work performed is pointer fix-up:
//
//   * each row's `prop[pi]` (stored on disk as an int64 offset into the
//     prop-blobs section, -1 for null) is rewritten in place to a host
//     pointer into the in-memory copy of the file;
//   * each `TypeString` slot inside a blob (stored on disk as an int64
//     offset into the string pool, -1 for null) is rewritten in place to a
//     `char const*` pointing at an interned string from `Strings::instance()`.
//
// After fix-up the buffer can be handed directly to a JIT-compiled spec —
// `Referee::execute` does exactly that for `.rdb` inputs.
//
// NOTE on large traces: today `Reader` slurps the whole file into a single
// `std::vector<uint8_t>`. For traces too large to fit in process memory the
// file should be `mmap()`-ed `MAP_PRIVATE` (so the in-place pointer fix-ups
// stay process-local instead of dirtying the on-disk image), or backed by
// shared memory if multiple consumers share the dataset. The fix-up walk
// itself is identical either way; only the storage backing changes.

#include "syntax.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace referee::db
{

inline void alignBuffer(std::vector<std::uint8_t>& buf, std::size_t align)
{
    while (buf.size() % align) buf.push_back(0);
}


/// One named slot in the schema (a `data` or `conf` declaration). The Type*
/// must outlive the Writer / Reader that holds it; in practice it comes from
/// either a parsed `::Module` (writer side) or a Reader-owned schema tree
/// (reader side).
struct PropDecl
{
    std::string name;
    Type*       type = nullptr;
};
using ConfDecl = PropDecl;

/// All prop blobs for a single state, indexed by prop. `blob_t[pi]` is the
/// raw bytes `Loader::load` produces for `props[pi].type`; an empty inner
/// vector means "null pointer for this slot".
using blob_t = std::vector<std::vector<std::uint8_t>>;

/// Build a .rdb file from a sequence of typed prop / conf blobs.
///
/// Typical use:
///
///     std::ofstream     os(path, std::ios::binary);
///     referee::db::Writer w(os);
///     w.setSchema(props, confs);
///     w.setNumStates(numStates);              //  data rows + 2 sentinels
///     w.setConfBlob(confBlob);
///     for (size_t i = 0; i < numStates; i++)
///         w.writeState(i, time[i], propBlobs[i]);
///     w.finish();
///
/// `propBlobs[pi]` must be the exact bytes `Loader::load` produces for
/// `props[pi].type`; the writer copies them verbatim.
class Writer
{
public:
    explicit Writer(std::ostream& os);
    ~Writer();

    Writer(Writer const&)            = delete;
    Writer& operator=(Writer const&) = delete;

    void    setSchema(std::vector<PropDecl> props,
                      std::vector<ConfDecl> confs);
    void    setNumStates(std::size_t numStates);
    void    setConfBlob(std::vector<std::uint8_t> blob);
    void    writeState(std::size_t  stateIdx,
                       std::int64_t time,
                       blob_t const& propBlobs);
    void    finish();

private:
    struct Impl;
    std::unique_ptr<Impl>   m_impl;
};

/// Open a .rdb file and prepare the in-memory state buffer for direct
/// consumption by `Referee::execute`.
class Reader
{
public:
    /// Open `path`, slurp its bytes, and run pointer fix-up.
    explicit Reader(std::string const& path);

    /// Take ownership of an already-materialised `.rdb` buffer (e.g. one
    /// produced by `referee::db::ingest(...)` writing to a stringstream)
    /// and run pointer fix-up. `ctx` is used only in error messages.
    explicit Reader(std::vector<std::uint8_t> bytes,
                    std::string const& ctx = "<memory>");

    ~Reader();

    Reader(Reader const&)            = delete;
    Reader& operator=(Reader const&) = delete;

    /// Schema introspection. The Type* returned via these PropDecls is owned
    /// by the Reader and lives until it is destroyed.
    std::vector<PropDecl> const&    props() const;
    std::vector<ConfDecl> const&    confs() const;

    /// Direct execute interface. After construction the slab is fully
    /// fixed-up: `ptrFirst()` is `&state[0]`, `ptrLast()` is
    /// `&state[numStates() - 1]`, `confPtr()` is the conf blob.
    void*           ptrFirst()      const;
    void*           ptrLast()       const;
    void*           confPtr()       const;
    std::size_t     numStates()     const;
    std::size_t     numProps()      const;
    std::size_t     rowBytes()      const;
    std::size_t     confSize()      const;

    /// Per-row helpers used by `rdb dump` and tests.
    std::int64_t        time(std::size_t stateIdx) const;
    void const*         propBlob(std::size_t stateIdx, std::size_t propIdx) const;

private:
    struct Impl;
    std::unique_ptr<Impl>   m_impl;
};

/// Pretty-print the contents of `path` to `os`, decoding each prop/conf blob
/// using the schema embedded in the file.
void    dump(std::string const& path, std::ostream& os);

} // namespace referee::db
