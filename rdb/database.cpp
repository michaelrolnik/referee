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

#include "database.hpp"

#include "strings.hpp"
#include "syntax.hpp"

#include <fmt/format.h>

#include <array>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <typeinfo>
#include <unordered_map>

namespace referee::db
{

namespace
{

constexpr char      kMagic[8]    = {'R', 'E', 'F', '-', 'R', 'D', 'B', '1'};
constexpr uint32_t  kVersion     = 1;
constexpr int64_t   kNullOffset  = -1;

#pragma pack(push, 1)
/// Byte range inside the .rdb file:
///   * `fileOffs` — distance from the start of the header, in bytes.
///   * `fileSize` — section length, in bytes.
///   * `itemNmbr` — number of logical entries the section holds (rows for
///                  `states`, decls for `schema` / `conf`, 0 for
///                  variable-shape blob pools).
///
/// `fileOffs == 0 && fileSize == 0` is reserved for empty sections that have
/// not been laid out yet.
struct Section
{
    uint64_t    fileOffs;
    uint64_t    fileSize;
    uint64_t    itemNmbr;
};

struct OnDiskHeader
{
    char        magic[8];
    uint32_t    version;
    uint32_t    flags;
    Section     schema;     // itemNmbr = number of `data` decls (== numProps)
    Section     conf;       // itemNmbr = number of `conf` decls
    Section     states;     // itemNmbr = number of state rows; stride = rowBytes
    Section     propBlobs;  // heterogeneous; itemNmbr = 0
    Section     stringPool; // heterogeneous; itemNmbr = 0
    uint64_t    rowBytes;   // stride of `states`; equals 8 + 8 * schema.itemNmbr
};
#pragma pack(pop)

enum TypeTag : uint8_t
{
    TAG_BOOLEAN = 1,
    TAG_INTEGER = 2,
    TAG_NUMBER  = 3,
    TAG_STRING  = 4,
    TAG_ENUM    = 5,
    TAG_STRUCT  = 6,
    TAG_ARRAY   = 7,
    TAG_BYTE    = 8,
};

template<typename T>
void    appendBytes(std::vector<uint8_t>& out, T const& v)
{
    auto const* p = reinterpret_cast<uint8_t const*>(&v);
    out.insert(out.end(), p, p + sizeof(T));
}

void    appendString(std::vector<uint8_t>& out, std::string const& s)
{
    uint32_t    n = static_cast<uint32_t>(s.size());
    appendBytes(out, n);
    out.insert(out.end(), s.begin(), s.end());
}

template<typename T>
T       readBytes(uint8_t const*& cur, uint8_t const* end)
{
    if (cur + sizeof(T) > end)
        throw std::runtime_error("rdb: truncated read");
    T   v;
    std::memcpy(&v, cur, sizeof(T));
    cur += sizeof(T);
    return v;
}

std::string     readString(uint8_t const*& cur, uint8_t const* end)
{
    auto    n = readBytes<uint32_t>(cur, end);
    if (cur + n > end)
        throw std::runtime_error("rdb: truncated string");
    std::string s(reinterpret_cast<char const*>(cur), n);
    cur += n;
    return s;
}

struct TypeEncoder
    : Visitor<TypeBoolean, TypeByte, TypeInteger, TypeNumber, TypeString,
              TypeEnum, TypeStruct, TypeArray>
{
    std::vector<uint8_t>&   out;
    explicit TypeEncoder(std::vector<uint8_t>& o) : out(o) {}

    void    visit(TypeBoolean*) override { appendBytes<uint8_t>(out, TAG_BOOLEAN); }
    void    visit(TypeByte*)    override { appendBytes<uint8_t>(out, TAG_BYTE);    }
    void    visit(TypeInteger*) override { appendBytes<uint8_t>(out, TAG_INTEGER); }
    void    visit(TypeNumber*)  override { appendBytes<uint8_t>(out, TAG_NUMBER);  }
    void    visit(TypeString*)  override { appendBytes<uint8_t>(out, TAG_STRING);  }

    void    visit(TypeEnum* e) override
    {
        appendBytes<uint8_t>(out, TAG_ENUM);
        appendBytes<uint32_t>(out, static_cast<uint32_t>(e->items.size()));
        for (auto const& it : e->items)
            appendString(out, it);
    }
    void    visit(TypeStruct* s) override
    {
        appendBytes<uint8_t>(out, TAG_STRUCT);
        appendBytes<uint32_t>(out, static_cast<uint32_t>(s->members.size()));
        for (auto const& m : s->members)
        {
            appendString(out, m.name);
            m.data->accept(*this);
        }
    }
    void    visit(TypeArray* a) override
    {
        appendBytes<uint8_t>(out, TAG_ARRAY);
        appendBytes<uint32_t>(out, a->count);
        a->type->accept(*this);
    }
};

void    encodeType(std::vector<uint8_t>& out, Type* type)
{
    TypeEncoder enc(out);
    type->accept(enc);
}

Type*   decodeType(uint8_t const*& cur, uint8_t const* end,
                   std::vector<std::unique_ptr<Type>>& sink);

using TypeCtor = Type* (*)(uint8_t const*&, uint8_t const*,
                           std::vector<std::unique_ptr<Type>>&);

static std::array<TypeCtor, 9> const kTypeCtors = {
    /* 0  (unused)  */ nullptr,
    /* TAG_BOOLEAN  */ [](uint8_t const*&, uint8_t const*,
                          std::vector<std::unique_ptr<Type>>&) -> Type*
    {
        return new TypeBoolean();
    },
    /* TAG_INTEGER  */ [](uint8_t const*&, uint8_t const*,
                          std::vector<std::unique_ptr<Type>>&) -> Type*
    {
        return new TypeInteger();
    },
    /* TAG_NUMBER   */ [](uint8_t const*&, uint8_t const*,
                          std::vector<std::unique_ptr<Type>>&) -> Type*
    {
        return new TypeNumber();
    },
    /* TAG_STRING   */ [](uint8_t const*&, uint8_t const*,
                          std::vector<std::unique_ptr<Type>>&) -> Type*
    {
        return new TypeString();
    },
    /* TAG_ENUM     */ [](uint8_t const*& cur, uint8_t const* end,
                          std::vector<std::unique_ptr<Type>>&) -> Type*
    {
        auto                        n = readBytes<uint32_t>(cur, end);
        std::vector<std::string>    items;
        items.reserve(n);
        for (uint32_t i = 0; i < n; i++)
            items.push_back(readString(cur, end));
        return new TypeEnum(items);
    },
    /* TAG_STRUCT   */ [](uint8_t const*& cur, uint8_t const* end,
                          std::vector<std::unique_ptr<Type>>& sink) -> Type*
    {
        auto                        n = readBytes<uint32_t>(cur, end);
        std::vector<Named<Type>>    members;
        members.reserve(n);
        for (uint32_t i = 0; i < n; i++)
        {
            auto    name = readString(cur, end);
            auto*   t    = decodeType(cur, end, sink);
            members.push_back(Named<Type>{name, t});
        }
        return new TypeStruct(members);
    },
    /* TAG_ARRAY    */ [](uint8_t const*& cur, uint8_t const* end,
                          std::vector<std::unique_ptr<Type>>& sink) -> Type*
    {
        auto    count = readBytes<uint32_t>(cur, end);
        auto*   t     = decodeType(cur, end, sink);
        return new TypeArray(t, count);
    },
    /* TAG_BYTE     */ [](uint8_t const*&, uint8_t const*,
                          std::vector<std::unique_ptr<Type>>&) -> Type*
    {
        return new TypeByte();
    },
};

Type*   decodeType(uint8_t const*& cur, uint8_t const* end,
                   std::vector<std::unique_ptr<Type>>& sink)
{
    auto    tag = readBytes<uint8_t>(cur, end);
    if (tag >= kTypeCtors.size() || kTypeCtors[tag] == nullptr)
        throw std::runtime_error(fmt::format("rdb: unknown type tag {}",
                                             static_cast<int>(tag)));
    Type*   raw = kTypeCtors[tag](cur, end, sink);
    sink.emplace_back(raw);
    return raw;
}

class BlobWalker
    : public Visitor<TypeBoolean, TypeByte, TypeInteger, TypeNumber, TypeString,
                     TypeEnum, TypeStruct, TypeArray>
{
public:
    BlobWalker(uint8_t* base, size_t size) : m_base(base), m_size(size), m_cur(base) {}

    void    walk(Type* type)
    {
        align(type->alignment());
        type->accept(*this);
    }

    size_t  consumed() const { return static_cast<size_t>(m_cur - m_base); }

protected:
    virtual void    visitString(uint8_t* slot) = 0;

    //  An array with no written extent. `slot` is its `{count, offset}`
    //  descriptor; the elements have already been walked by the time this is
    //  called, so a subclass is free to overwrite the offset.
    virtual void    visitArray(uint8_t* slot, int64_t count, int64_t delta) {}

    void    visit(TypeBoolean* t) override { advance(t); }
    void    visit(TypeByte*    t) override { advance(t); }
    void    visit(TypeInteger* t) override { advance(t); }
    void    visit(TypeNumber*  t) override { advance(t); }
    void    visit(TypeEnum*    t) override { advance(t); }

    void    visit(TypeString*  t) override
    {
        check(t->size());
        visitString(m_cur);
        advance(t);
    }
    void    visit(TypeStruct*  s) override
    {
        uint8_t*    start = m_cur;
        for (auto const& m : s->members)
            walk(m.data);
        // Pad up to the struct's full aligned size, matching `TypeStruct::size()`.
        size_t  written = static_cast<size_t>(m_cur - start);
        size_t  full    = s->size();
        if (written < full)
            m_cur += (full - written);
    }
    void    visit(TypeArray*   a) override
    {
        //  An array that carries its own length is sixteen bytes here -- a
        //  count and an offset -- with the elements placed after the fixed
        //  layout. The offset is relative to the descriptor, so it survives
        //  the blob being appended into the file and then mapped somewhere
        //  else entirely; it is turned into a pointer on the way in.
        if (a->count == 0)
        {
            check(a->size());

            uint8_t*    slot  = m_cur;
            int64_t     count = 0;
            int64_t     delta = 0;

            std::memcpy(&count, slot,     sizeof(count));
            std::memcpy(&delta, slot + 8, sizeof(delta));

            //  Elements live outside the fixed layout, so the walk detours to
            //  them and comes back. They may hold strings, which is the whole
            //  reason this walker exists.
            if (count > 0)
            {
                m_cur = slot + delta;
                for (int64_t i = 0; i < count; i++)
                    walk(a->type);
            }

            m_cur = slot + a->size();
            visitArray(slot, count, delta);
            return;
        }

        for (uint32_t i = 0; i < a->count; i++)
            walk(a->type);
    }

private:
    void    advance(Type* t)
    {
        check(t->size());
        m_cur += t->size();
    }

    void    align(size_t a)
    {
        size_t  off = static_cast<size_t>(m_cur - m_base);
        size_t  rem = off % a;
        if (rem)
            m_cur += (a - rem);
    }

    void    check(size_t n)
    {
        if (m_cur + n > m_base + m_size)
            throw std::runtime_error("rdb: blob walker ran past end");
    }

    uint8_t*    m_base;
    size_t      m_size;
    uint8_t*    m_cur;
};

class StringInterner final : public BlobWalker
{
public:
    StringInterner(uint8_t*                                 base,
                   size_t                                   size,
                   std::unordered_map<std::string, uint64_t>& dict,
                   std::vector<uint8_t>&                    pool)
        : BlobWalker(base, size)
        , m_dict(dict)
        , m_pool(pool)
    {
    }

protected:
    void    visitString(uint8_t* slot) override
    {
        char const* hostPtr = nullptr;
        std::memcpy(&hostPtr, slot, sizeof(hostPtr));

        int64_t off = kNullOffset;
        if (hostPtr != nullptr)
        {
            std::string key(hostPtr);
            auto        it = m_dict.find(key);
            if (it == m_dict.end())
            {
                uint64_t pos = m_pool.size();
                m_pool.insert(m_pool.end(), key.begin(), key.end());
                m_pool.push_back(0);
                it = m_dict.emplace(std::move(key), pos).first;
            }
            off = static_cast<int64_t>(it->second);
        }
        std::memcpy(slot, &off, sizeof(off));
    }

private:
    std::unordered_map<std::string, uint64_t>&  m_dict;
    std::vector<uint8_t>&                       m_pool;
};

class StringResolver final : public BlobWalker
{
public:
    StringResolver(uint8_t* base, size_t size, char const* pool, size_t poolSize)
        : BlobWalker(base, size)
        , m_pool(pool)
        , m_poolSize(poolSize)
    {
    }

protected:
    //  The offset becomes a host pointer, now that the blob has an address.
    //  Exactly what happens to a string, one level up: the file stores a
    //  displacement because it has to, and the running program wants a
    //  pointer.
    void    visitArray(uint8_t* slot, int64_t count, int64_t delta) override
    {
        void*   host = slot + delta;

        std::memcpy(slot + 8, &host, sizeof(host));
    }

protected:
    void    visitString(uint8_t* slot) override
    {
        int64_t off = 0;
        std::memcpy(&off, slot, sizeof(off));

        char const* hostPtr = nullptr;
        if (off != kNullOffset)
        {
            if (off < 0 || static_cast<uint64_t>(off) >= m_poolSize)
                throw std::runtime_error("rdb: string offset out of range");
            hostPtr = Strings::instance()->getString(m_pool + off);
        }
        std::memcpy(slot, &hostPtr, sizeof(hostPtr));
    }

private:
    char const*     m_pool;
    size_t          m_poolSize;
};

} // namespace

void    encodeSchema(std::vector<uint8_t>&             out,
                     std::vector<PropDecl> const&      props,
                     std::vector<ConfDecl> const&      confs)
{
    appendBytes<uint32_t>(out, static_cast<uint32_t>(props.size()));
    for (auto const& p : props)
    {
        appendString(out, p.name);
        encodeType(out, p.type);
    }
    appendBytes<uint32_t>(out, static_cast<uint32_t>(confs.size()));
    for (auto const& c : confs)
    {
        appendString(out, c.name);
        encodeType(out, c.type);
    }
}

void    decodeSchema(uint8_t const*&                         cur,
                     uint8_t const*                          end,
                     std::vector<PropDecl>&                  props,
                     std::vector<ConfDecl>&                  confs,
                     std::vector<std::unique_ptr<Type>>&     sink)
{
    auto    np = readBytes<uint32_t>(cur, end);
    props.reserve(np);
    for (uint32_t i = 0; i < np; i++)
    {
        PropDecl    d;
        d.name = readString(cur, end);
        d.type = decodeType(cur, end, sink);
        props.push_back(std::move(d));
    }
    auto    nc = readBytes<uint32_t>(cur, end);
    confs.reserve(nc);
    for (uint32_t i = 0; i < nc; i++)
    {
        ConfDecl    d;
        d.name = readString(cur, end);
        d.type = decodeType(cur, end, sink);
        confs.push_back(std::move(d));
    }
}

// ============================================================================
//  Writer
// ============================================================================

struct Writer::Impl
{
    std::ostream&                                       os;
    std::vector<PropDecl>                               props;
    std::vector<ConfDecl>                               confs;
    size_t                                              numStates  = 0;
    bool                                                hasNumStates = false;
    std::vector<uint8_t>                                confBlob;
    bool                                                hasConfBlob  = false;
    // For each prop, the per-state blob bytes. blobs[pi][si] is the bytes for
    // (state si, prop pi). Empty inner vector means "null pointer for this
    // state's slot". Equivalent to `std::vector<blob_t>`, but the outer index
    // is prop-major rather than state-major (the `blob_t` exposed by the API
    // is one row's worth, indexed by prop).
    std::vector<blob_t>                                 blobs;
    std::vector<std::optional<int64_t>>                 times;

    explicit Impl(std::ostream& s) : os(s) {}
};

Writer::Writer(std::ostream& os) : m_impl(std::make_unique<Impl>(os)) {}
Writer::~Writer() = default;

void    Writer::setSchema(std::vector<PropDecl> props,
                          std::vector<ConfDecl> confs)
{
    m_impl->props = std::move(props);
    m_impl->confs = std::move(confs);
    m_impl->blobs.assign(m_impl->props.size(), {});
}

void    Writer::setNumStates(std::size_t numStates)
{
    m_impl->numStates    = numStates;
    m_impl->hasNumStates = true;
    m_impl->times.assign(numStates, {});
    for (auto& vec : m_impl->blobs)
        vec.assign(numStates, {});
}

void    Writer::setConfBlob(std::vector<std::uint8_t> blob)
{
    m_impl->confBlob    = std::move(blob);
    m_impl->hasConfBlob = true;
}

void    Writer::writeState(std::size_t      stateIdx,
                           std::int64_t     time,
                           blob_t const&    propBlobs)
{
    if (!m_impl->hasNumStates)
        throw std::runtime_error("rdb: writer.setNumStates() not called");
    if (stateIdx >= m_impl->numStates)
        throw std::runtime_error(fmt::format("rdb: state index {} out of range (0..{})",
                                             stateIdx, m_impl->numStates));
    if (propBlobs.size() != m_impl->props.size())
        throw std::runtime_error(fmt::format("rdb: state {}: expected {} prop blobs, got {}",
                                             stateIdx, m_impl->props.size(), propBlobs.size()));

    m_impl->times[stateIdx]   = time;
    for (size_t pi = 0; pi < propBlobs.size(); pi++)
        m_impl->blobs[pi][stateIdx] = propBlobs[pi];
}

void    Writer::finish()
{
    if (!m_impl->hasNumStates)
        throw std::runtime_error("rdb: writer.setNumStates() not called");
    if (!m_impl->hasConfBlob)
        throw std::runtime_error("rdb: writer.setConfBlob() not called");
    for (size_t si = 0; si < m_impl->numStates; si++)
        if (!m_impl->times[si].has_value())
            throw std::runtime_error(fmt::format("rdb: writeState() never called for index {}", si));

    auto const  numProps     = m_impl->props.size();
    auto const  numStates    = m_impl->numStates;
    auto const  rowBytes     = sizeof(int64_t) + numProps * sizeof(int64_t);

    // Encode schema.
    std::vector<uint8_t>    schemaBytes;
    encodeSchema(schemaBytes, m_impl->props, m_impl->confs);

    // Build the string pool incrementally, replacing each TypeString slot in
    // every blob with an offset into the (yet-unfinished) pool. Pool offset 0
    // is unused — the empty string is stored there explicitly so callers that
    // happen to default-init a string to empty still get a real entry.
    std::unordered_map<std::string, uint64_t>   dict;
    std::vector<uint8_t>                        stringPool;
    stringPool.push_back(0);                              // offset 0 = ""
    dict.emplace("", uint64_t{0});

    auto    internOne = [&](uint8_t* base, size_t size, Type* type)
    {
        if (size == 0) return;
        StringInterner walker(base, size, dict, stringPool);
        walker.walk(type);
    };

    // The conf blob is the concatenation of all conf members, each aligned
    // with `alignBuffer(buf, ctype->alignment())` before Loader::load fills
    // it. Replicate that exact walk so per-member alignment math stays
    // buffer-relative — same as Loader::load.
    {
        size_t cur = 0;
        for (auto const& c : m_impl->confs)
        {
            size_t a   = c.type->alignment();
            size_t rem = cur % a;
            if (rem) cur += (a - rem);
            StringInterner sub(m_impl->confBlob.data() + cur,
                               m_impl->confBlob.size() - cur,
                               dict, stringPool);
            sub.walk(c.type);
            cur += sub.consumed();
        }
        if (cur > m_impl->confBlob.size())
            throw std::runtime_error("rdb: conf blob size disagrees with schema");
    }

    for (size_t pi = 0; pi < numProps; pi++)
    {
        for (size_t si = 0; si < numStates; si++)
        {
            auto& blob = m_impl->blobs[pi][si];
            if (blob.empty()) continue;
            internOne(blob.data(), blob.size(), m_impl->props[pi].type);
        }
    }

    // Build prop-blobs section and a parallel offset table.
    std::vector<uint8_t>                        propBlobs;
    std::vector<std::vector<int64_t>>           offsets(numStates,
                                                        std::vector<int64_t>(numProps, kNullOffset));
    for (size_t si = 0; si < numStates; si++)
    {
        for (size_t pi = 0; pi < numProps; pi++)
        {
            auto& blob = m_impl->blobs[pi][si];
            if (blob.empty()) continue;
            // Align inside the prop-blobs section to the prop type's alignment
            // so that the host pointer the reader hands to JIT'd code respects
            // the alignment Loader::load assumed.
            size_t a   = m_impl->props[pi].type->alignment();
            size_t rem = propBlobs.size() % a;
            if (rem)
                propBlobs.insert(propBlobs.end(), a - rem, uint8_t{0});
            offsets[si][pi] = static_cast<int64_t>(propBlobs.size());
            propBlobs.insert(propBlobs.end(), blob.begin(), blob.end());
        }
    }

    // Build state buffer with int64 offsets (later relocated by Reader).
    std::vector<uint8_t>    states(numStates * rowBytes, uint8_t{0});
    for (size_t si = 0; si < numStates; si++)
    {
        uint8_t*    row = states.data() + si * rowBytes;
        int64_t     t   = m_impl->times[si].value();
        std::memcpy(row, &t, sizeof(t));
        for (size_t pi = 0; pi < numProps; pi++)
        {
            int64_t off = offsets[si][pi];
            std::memcpy(row + sizeof(int64_t) + pi * sizeof(int64_t),
                        &off, sizeof(off));
        }
    }

    // Lay out the file. Each section is 8-byte aligned for cleanliness so a
    // future mmap-backed reader can directly cast section pointers without
    // worrying about misalignment.
    auto    align8 = [](uint64_t v) { return (v + 7u) & ~uint64_t{7}; };

    OnDiskHeader    hdr{};
    std::memcpy(hdr.magic, kMagic, sizeof(kMagic));
    hdr.version        = kVersion;
    hdr.flags          = 0;

    auto    place = [&](Section& sec, uint64_t& cursor, uint64_t sz, uint64_t cnt)
    {
        cursor       = align8(cursor);
        sec.fileOffs = cursor;
        sec.fileSize = sz;
        sec.itemNmbr = cnt;
        cursor      += sz;
    };

    uint64_t cursor = sizeof(OnDiskHeader);
    place(hdr.schema,     cursor, schemaBytes.size(),     numProps);
    place(hdr.conf,       cursor, m_impl->confBlob.size(), m_impl->confs.size());
    place(hdr.states,     cursor, states.size(),          numStates);
    place(hdr.propBlobs,  cursor, propBlobs.size(),       0);
    place(hdr.stringPool, cursor, stringPool.size(),      0);

    hdr.rowBytes         = rowBytes;

    auto    write = [&](void const* p, size_t n)
    {
        m_impl->os.write(reinterpret_cast<char const*>(p), static_cast<std::streamsize>(n));
    };
    auto    pad = [&](uint64_t curOff, uint64_t target)
    {
        while (curOff < target)
        {
            uint8_t z = 0;
            write(&z, 1);
            curOff++;
        }
        return curOff;
    };

    uint64_t off = 0;
    write(&hdr, sizeof(hdr)); off += sizeof(hdr);
    auto    emit = [&](Section const& sec, void const* data)
    {
        off = pad(off, sec.fileOffs);
        write(data, sec.fileSize);
        off += sec.fileSize;
    };
    emit(hdr.schema,     schemaBytes.data());
    emit(hdr.conf,       m_impl->confBlob.data());
    emit(hdr.states,     states.data());
    emit(hdr.propBlobs,  propBlobs.data());
    emit(hdr.stringPool, stringPool.data());

    m_impl->os.flush();
}

// ============================================================================
//  Reader
// ============================================================================

struct Reader::Impl
{
    std::vector<uint8_t>                    data;
    OnDiskHeader                            hdr{};
    std::vector<std::unique_ptr<Type>>      typeSink;
    std::vector<PropDecl>                   props;
    std::vector<ConfDecl>                   confs;

    // Validate the slab in `data` and run the in-place pointer fix-up.
    // `ctx` is purely for error messages (file path or "<memory>").
    void    fixUp(std::string const& ctx)
    {
        if (data.size() < sizeof(OnDiskHeader))
            throw std::runtime_error(fmt::format("rdb: '{}' is too small to be a .rdb file", ctx));

        std::memcpy(&hdr, data.data(), sizeof(OnDiskHeader));

        if (std::memcmp(hdr.magic, kMagic, sizeof(kMagic)) != 0)
            throw std::runtime_error(fmt::format("rdb: bad magic in '{}'", ctx));
        if (hdr.version != kVersion)
            throw std::runtime_error(fmt::format("rdb: unsupported version {} in '{}'",
                                                 hdr.version, ctx));

        auto    needRange = [&](Section const& sec, char const* what)
        {
            if (sec.fileOffs + sec.fileSize > data.size())
                throw std::runtime_error(fmt::format("rdb: {} section out of bounds", what));
        };
        needRange(hdr.schema,     "schema");
        needRange(hdr.conf,       "conf");
        needRange(hdr.states,     "states");
        needRange(hdr.propBlobs,  "prop-blobs");
        needRange(hdr.stringPool, "string-pool");

        if (hdr.states.itemNmbr * hdr.rowBytes != hdr.states.fileSize)
            throw std::runtime_error("rdb: states section size mismatch");

        // Decode schema, then cross-check all the redundant counts/strides.
        {
            uint8_t const*  cur = data.data() + hdr.schema.fileOffs;
            uint8_t const*  end = cur + hdr.schema.fileSize;
            decodeSchema(cur, end, props, confs, typeSink);
            if (cur != end)
                throw std::runtime_error("rdb: trailing bytes in schema section");
        }
        if (props.size() != hdr.schema.itemNmbr)
            throw std::runtime_error("rdb: schema/schema.itemNmbr disagree");
        if (confs.size() != hdr.conf.itemNmbr)
            throw std::runtime_error("rdb: schema/conf.itemNmbr disagree");
        if (hdr.rowBytes != sizeof(int64_t) + hdr.schema.itemNmbr * sizeof(int64_t))
            throw std::runtime_error("rdb: rowBytes inconsistent with schema.itemNmbr");

        // Resolve all string offsets to interned host pointers, in place.
        char const*     poolBase   = reinterpret_cast<char const*>(
                                        data.data() + hdr.stringPool.fileOffs);
        size_t          poolSize   = hdr.stringPool.fileSize;
        uint8_t*        propBase   = data.data() + hdr.propBlobs.fileOffs;
        uint64_t        propSize   = hdr.propBlobs.fileSize;

        {
            // conf blob: same per-member alignment walk as the writer.
            uint8_t*    confBase = data.data() + hdr.conf.fileOffs;
            size_t      confSz   = hdr.conf.fileSize;
            size_t      cur      = 0;
            for (auto const& c : confs)
            {
                size_t  a   = c.type->alignment();
                size_t  rem = cur % a;
                if (rem) cur += (a - rem);
                StringResolver  sub(confBase + cur, confSz - cur, poolBase, poolSize);
                sub.walk(c.type);
                cur += sub.consumed();
            }
        }

        // Walk every state row: rewrite each int64 prop offset to a host pointer
        // into the prop-blobs section, and run the string resolver over the blob.
        {
            uint8_t*    rows     = data.data() + hdr.states.fileOffs;
            auto const  numProps = hdr.schema.itemNmbr;
            auto const  rowBytes = hdr.rowBytes;
            for (uint64_t si = 0; si < hdr.states.itemNmbr; si++)
            {
                uint8_t*    row = rows + si * rowBytes;
                for (uint64_t pi = 0; pi < numProps; pi++)
                {
                    uint8_t*    slot = row + sizeof(int64_t) + pi * sizeof(int64_t);
                    int64_t     off  = 0;
                    std::memcpy(&off, slot, sizeof(off));
                    void*       host = nullptr;
                    if (off != kNullOffset)
                    {
                        if (off < 0 || static_cast<uint64_t>(off) >= propSize)
                            throw std::runtime_error("rdb: prop offset out of range");
                        host = propBase + off;
                        // Resolve any TypeString slots within this blob.
                        Type*   t = props[pi].type;
                        // The blob's size on disk is determined by walking the
                        // type; the walker doesn't read past its end.
                        StringResolver  sub(propBase + off, propSize - off,
                                            poolBase, poolSize);
                        sub.walk(t);
                    }
                    std::memcpy(slot, &host, sizeof(host));
                }
            }
        }
    }
};

Reader::Reader(std::string const& path) : m_impl(std::make_unique<Impl>())
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error(fmt::format("rdb: cannot open '{}'", path));
    in.seekg(0, std::ios::end);
    auto    size = in.tellg();
    if (size < static_cast<std::streamoff>(sizeof(OnDiskHeader)))
        throw std::runtime_error(fmt::format("rdb: '{}' is too small to be a .rdb file", path));
    in.seekg(0, std::ios::beg);
    m_impl->data.resize(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(m_impl->data.data()),
            static_cast<std::streamsize>(m_impl->data.size()));
    if (!in)
        throw std::runtime_error(fmt::format("rdb: short read for '{}'", path));

    m_impl->fixUp(path);
}

Reader::Reader(std::vector<std::uint8_t> bytes, std::string const& ctx)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->data = std::move(bytes);
    m_impl->fixUp(ctx);
}

Reader::~Reader() = default;

std::vector<PropDecl> const&    Reader::props() const { return m_impl->props; }
std::vector<ConfDecl> const&    Reader::confs() const { return m_impl->confs; }

void*   Reader::ptrFirst() const
{
    return m_impl->data.data() + m_impl->hdr.states.fileOffs;
}

void*   Reader::ptrLast() const
{
    if (m_impl->hdr.states.itemNmbr == 0) return nullptr;
    return m_impl->data.data() + m_impl->hdr.states.fileOffs
         + (m_impl->hdr.states.itemNmbr - 1) * m_impl->hdr.rowBytes;
}

void*   Reader::confPtr() const
{
    return m_impl->data.data() + m_impl->hdr.conf.fileOffs;
}

std::size_t     Reader::numStates() const { return m_impl->hdr.states.itemNmbr; }
std::size_t     Reader::numProps()  const { return m_impl->hdr.schema.itemNmbr; }
std::size_t     Reader::rowBytes()  const { return m_impl->hdr.rowBytes; }
std::size_t     Reader::confSize()  const { return m_impl->hdr.conf.fileSize; }

std::int64_t    Reader::time(std::size_t stateIdx) const
{
    if (stateIdx >= m_impl->hdr.states.itemNmbr)
        throw std::runtime_error("rdb: state index out of range");
    auto*       row = m_impl->data.data() + m_impl->hdr.states.fileOffs
                    + stateIdx * m_impl->hdr.rowBytes;
    int64_t     t   = 0;
    std::memcpy(&t, row, sizeof(t));
    return t;
}

void const*     Reader::propBlob(std::size_t stateIdx, std::size_t propIdx) const
{
    if (stateIdx >= m_impl->hdr.states.itemNmbr || propIdx >= m_impl->hdr.schema.itemNmbr)
        throw std::runtime_error("rdb: index out of range");
    auto*       row  = m_impl->data.data() + m_impl->hdr.states.fileOffs
                     + stateIdx * m_impl->hdr.rowBytes;
    void*       host = nullptr;
    std::memcpy(&host,
                row + sizeof(int64_t) + propIdx * sizeof(int64_t),
                sizeof(host));
    return host;
}

struct DataYamlPrinter
    : Visitor<TypeBoolean, TypeByte, TypeInteger, TypeNumber, TypeString, TypeEnum, TypeStruct, TypeArray>
{
    std::ostream& os;
    Type*          type;
    uint8_t const* data;
    size_t         indent;

    DataYamlPrinter(std::ostream &os, Type *type, uint8_t const* d, size_t indent)
        : os(os), type(type), data(d), indent(indent)
    {
    }

    static std::ostream& render(std::ostream& os, Type *type, uint8_t const* data, size_t indent)
    {
        DataYamlPrinter v(os, type, data, indent);
        type->accept(v);

        return os;
    }

    static std::string ind(size_t n)
    {
        std::ostringstream ss;
        for (size_t i = 0; i < n; i++) ss << ' ';

        return ss.str();
    }

    void visit(TypeBoolean*) override
    {
        os << ((*reinterpret_cast<bool const*>(data)) ? "true" : "false");
    }

    void visit(TypeByte*) override
    {
        os << static_cast<unsigned>(*reinterpret_cast<std::uint8_t const*>(data));
    }

    void visit(TypeInteger*) override
    {
        //  An integer is stored 64-bit; reading `int` here truncated it and,
        //  on a big value, printed garbage.
        os << *reinterpret_cast<std::int64_t const*>(data);
    }

    void visit(TypeNumber*) override
    {
        os << *reinterpret_cast<double const*>(data);
    }

    void visit(TypeString*) override
    {
        auto*   s = *reinterpret_cast<char const* const*>(data);
        os << (s != nullptr ? s : "");
    }

    void visit(TypeEnum* e) override
    {
        //  One byte, 1-based, 0 meaning no member matched -- the same encoding
        //  the loader writes and `writeValue` reads. Reading a 4-byte 0-based
        //  index here named the *wrong member* (and off the end for the last),
        //  so a dumped enum was silently one off.
        auto    idx = std::size_t(*data);
        if (idx == 0 || idx > e->items.size())  os << "null";
        else                                    os << e->items[idx - 1];
    }

    void visit(TypeStruct* s) override
    {
        size_t curr = 0;
        for (auto const& member : s->members)
        {
            auto align = member.data->alignment();

            if (curr % align)
                curr += align- (curr % align);

            os << "\n" << ind(indent + 2) << "- " << member.name << ": ";

            render(os, member.data, data + curr, indent + 4);

            curr += member.data->size();
        }
    }

    void visit(TypeArray* a) override
    {
        auto elemSize = a->type->size();

        //  An array that carries its own length is a {count, pointer}
        //  descriptor; the elements live where the pointer says. Looping
        //  `a->count` -- which is 0 for these -- printed nothing, so a ragged
        //  .rdb dumped with every array empty and looked like data loss.
        if (a->count == 0)
        {
            std::int64_t    n = 0;
            uint8_t const*  p = nullptr;
            std::memcpy(&n, data,     sizeof(n));
            std::memcpy(&p, data + 8, sizeof(p));

            for (std::int64_t i = 0; i < n; i++)
            {
                os << "\n" << ind(indent + 2) << "- ";
                render(os, a->type, p + i * elemSize, indent + 4);
            }
            return;
        }

        for (size_t i = 0; i < a->count; i++)
        {
            os << "\n" << ind(indent + 2) << "- ";

            render(os,
                   a->type,
                   data + i * elemSize,
                   indent + 4);
        }
    }
};

struct TypeYamlPrinter
    : Visitor<TypeBoolean, TypeByte, TypeInteger, TypeNumber, TypeString, TypeEnum, TypeStruct, TypeArray>
{
    std::ostream&   os;
    Type*           type;
    size_t          indent;

    TypeYamlPrinter(std::ostream& o, Type *type, size_t indent)
        : os(o), type(type), indent(indent)
    {
    }

    static std::string ind(size_t n)
    {
        std::ostringstream ss;
        for (size_t i = 0; i < n; i++) ss << ' ';

        return ss.str();
    }

    static std::ostream& render(std::ostream& os, Type *type, size_t indent)
    {
        TypeYamlPrinter v(os, type, indent);
        type->accept(v);

        return os;
    }

    void visit(TypeBoolean*)  override { os << "boolean"; }
    void visit(TypeByte*)     override { os << "byte"; }
    void visit(TypeInteger*)  override { os << "integer"; }
    void visit(TypeNumber*)   override { os << "number"; }
    void visit(TypeString*)   override { os << "string"; }
    void visit(TypeEnum* e)   override
    {
        os << "enum: " << "\n";
        os << ind(indent + 2) << "items: ";
        for (auto const& item : e->items)
            os << "\n" << ind(indent + 4) << "- " << item;
    }
    void visit(TypeStruct* s) override
    {
        os << "struct" << "\n";

        os << ind(indent + 0) << "fields:";

        for (auto const& member : s->members)
        {
            os << "\n" << ind(indent + 2) << "- " << member.name << ": ";

            render(os, member.data, indent + 4);
        }
    }

    void visit(TypeArray* a) override
    {
        os << "array" << "\n";
        os << ind(indent + 2) << "items: " << "\n";
        os << ind(indent + 4) << "type:  "; render(os, a->type, indent + 4) << "\n";
        os << ind(indent + 4) << "count: " << a->count << "\n";
    }
};

//  Flatten one prop blob to `(column, value)` leaf pairs, in the order
//  `csvHeaders` names the columns, so a header built from the types and a row
//  built from a blob line up by position.
//
//  Structural traversal is DataYamlPrinter's -- align each struct member,
//  stride an array by its element size. Leaf decoding is `writeValue`'s, which
//  is the *current* one: an integer is 64-bit, an enum is 1-based with 0
//  meaning "no member", a string is the fixed-up pointer. (DataYamlPrinter's
//  own leaves read a 32-bit int and a 0-based enum, which is a latent dump bug,
//  not something to reproduce here.)
//  The capacity table a ragged trace needs to flatten: how many elements the
//  widest record carries, per array path and dimension, keyed the way the
//  loader keys its own capacities -- the dotted path with subscripts elided,
//  dimension counted since the last dot. `pkt` is ("pkt", 0); the inner array
//  of `rows[1]` is ("rows", 1); `msg[0].raw` is ("msg.raw", 0).
using FlatCaps = std::map<std::string, std::vector<unsigned>>;

static std::pair<std::string, unsigned> flatPathOf(std::string const& prefix)
{
    std::string path;
    bool        skip = false;
    for (char c : prefix)
    {
        if      (c == '[') skip = true;
        else if (c == ']') skip = false;
        else if (!skip)    path += c;
    }

    unsigned    dim = 0;
    auto        dot = prefix.rfind('.');
    for (std::size_t i = (dot == std::string::npos ? 0 : dot + 1); i < prefix.size(); i++)
        if (prefix[i] == '[')
            dim++;

    return {path, dim};
}

//  One recorded blob's contribution to the capacity table: every descriptor's
//  count, at every nesting depth.
static void flatScanCaps(Type* type, uint8_t const* data,
                         std::string const& prefix, FlatCaps& caps)
{
    if (auto* st = dynamic_cast<TypeStruct*>(type))
    {
        std::size_t cur = 0;
        for (auto const& m : st->members)
        {
            auto align = m.data->alignment();
            if (cur % align) cur += align - (cur % align);
            flatScanCaps(m.data, data + cur, prefix + "." + m.name, caps);
            cur += m.data->size();
        }
        return;
    }

    if (auto* a = dynamic_cast<TypeArray*>(type))
    {
        if (a->count == 0)
        {
            std::int64_t    n = 0;
            uint8_t const*  p = nullptr;
            std::memcpy(&n, data,     sizeof(n));
            std::memcpy(&p, data + 8, sizeof(p));

            auto [path, dim] = flatPathOf(prefix);
            auto& dims = caps[path];
            if (dims.size() <= dim) dims.resize(dim + 1, 0);
            dims[dim] = std::max<unsigned>(dims[dim], unsigned(n));

            auto stride = a->type->size();
            for (std::int64_t i = 0; i < n; i++)
                flatScanCaps(a->type, p + i * stride,
                             prefix + "[" + std::to_string(i) + "]", caps);
            return;
        }

        auto stride = a->type->size();
        for (unsigned i = 0; i < a->count; i++)
            flatScanCaps(a->type, data + i * stride,
                         prefix + "[" + std::to_string(i) + "]", caps);
        return;
    }
    //  leaves carry no capacity
}

struct FlatRow
    : Visitor<TypeBoolean, TypeByte, TypeInteger, TypeNumber, TypeString, TypeEnum, TypeStruct, TypeArray>
{
    std::vector<std::pair<std::string, std::string>>&   out;
    std::string                                         prefix;
    uint8_t const*                                      data;   //  null: absent -- emit `-`
    FlatCaps const*                                     caps;

    FlatRow(std::vector<std::pair<std::string, std::string>>& out,
            std::string prefix, uint8_t const* data, FlatCaps const* caps)
        : out(out), prefix(std::move(prefix)), data(data), caps(caps)
    {
    }

    static void walk(std::vector<std::pair<std::string, std::string>>& out,
                     std::string const& prefix, Type* type, uint8_t const* data,
                     FlatCaps const* caps = nullptr)
    {
        FlatRow v(out, prefix, data, caps);
        type->accept(v);
    }

    void    leaf(std::string s) { out.emplace_back(prefix, std::move(s)); }

    //  Null data is an absent ragged element (or a header walk, which ignores
    //  values): every leaf under it is `-`, the marker the loader reads as
    //  "not an element".
    void visit(TypeBoolean*) override { leaf(data ? (*reinterpret_cast<bool const*>(data) ? "true" : "false") : "-"); }
    void visit(TypeByte*)    override { leaf(data ? std::to_string(unsigned(*data)) : "-"); }
    void visit(TypeInteger*) override { leaf(data ? std::to_string(*reinterpret_cast<std::int64_t const*>(data)) : "-"); }
    void visit(TypeNumber*)  override { leaf(data ? fmt::format("{}", *reinterpret_cast<double const*>(data)) : "-"); }

    void visit(TypeString*)  override
    {
        if (data == nullptr) { leaf("-"); return; }
        auto*   s = *reinterpret_cast<char const* const*>(data);
        leaf(s != nullptr ? std::string(s) : std::string());
    }

    void visit(TypeEnum* e)  override
    {
        if (data == nullptr) { leaf("-"); return; }
        auto    idx = std::size_t(*data);       //  1-based; 0 means no member matched
        leaf(idx == 0 || idx > e->items.size() ? std::string() : e->items[idx - 1]);
    }

    void visit(TypeStruct* s) override
    {
        std::size_t cur = 0;
        for (auto const& m : s->members)
        {
            auto    align = m.data->alignment();
            if (cur % align) cur += align - (cur % align);

            walk(out, prefix + "." + m.name, m.data, data ? data + cur : nullptr, caps);
            cur += m.data->size();
        }
    }

    void visit(TypeArray* a) override
    {
        auto    stride = a->type->size();

        //  An array that carries its own length: real elements from the
        //  descriptor's pointer, then `-` padding up to the widest record so
        //  every row has the header's width -- exactly the ragged-CSV
        //  convention the loader parses back.
        if (a->count == 0)
        {
            std::int64_t    n = 0;
            uint8_t const*  p = nullptr;
            if (data != nullptr)
            {
                std::memcpy(&n, data,     sizeof(n));
                std::memcpy(&p, data + 8, sizeof(p));
            }

            unsigned    cap = unsigned(n);
            if (caps != nullptr)
            {
                auto [path, dim] = flatPathOf(prefix);
                auto it = caps->find(path);
                if (it != caps->end() && dim < it->second.size())
                    cap = std::max<unsigned>(cap, it->second[dim]);
            }

            for (unsigned i = 0; i < cap; i++)
                walk(out, prefix + "[" + std::to_string(i) + "]", a->type,
                     (data != nullptr && std::int64_t(i) < n) ? p + i * stride : nullptr,
                     caps);
            return;
        }

        for (unsigned i = 0; i < a->count; i++)
            walk(out, prefix + "[" + std::to_string(i) + "]", a->type,
                 data ? data + i * stride : nullptr, caps);
    }
};

bool    typesEqual(Type* a, Type* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    if (typeid(*a) != typeid(*b)) return false;

    if (auto* ea = dynamic_cast<TypeEnum*>(a))
    {
        auto* eb = dynamic_cast<TypeEnum*>(b);
        return ea->items == eb->items;
    }
    if (auto* sa = dynamic_cast<TypeStruct*>(a))
    {
        auto* sb = dynamic_cast<TypeStruct*>(b);
        if (sa->members.size() != sb->members.size()) return false;
        for (size_t i = 0; i < sa->members.size(); i++)
        {
            if (sa->members[i].name != sb->members[i].name) return false;
            if (!typesEqual(sa->members[i].data, sb->members[i].data)) return false;
        }
        return true;
    }
    if (auto* aa = dynamic_cast<TypeArray*>(a))
    {
        auto* ab = dynamic_cast<TypeArray*>(b);
        return aa->count == ab->count && typesEqual(aa->type, ab->type);
    }
    return true;        // primitives -- class identity already matched
}

void    toCsv(Reader const& rdb, std::ostream& os)
{
    auto const& props = rdb.props();
    std::size_t real  = rdb.numStates() >= 2 ? rdb.numStates() - 2 : 0;

    //  Pass 1: the capacity of every ragged array -- the widest record decides
    //  the header, exactly as a hand-written ragged CSV does. Records narrower
    //  than the header pad with `-`, which the loader reads back as "not an
    //  element", so the round trip preserves each record's own count. Without
    //  this pass a ragged array emitted zero columns and its values were
    //  silently lost.
    FlatCaps    caps;
    for (std::size_t r = 0; r < real; r++)
        for (std::size_t pi = 0; pi < props.size(); pi++)
            if (auto* blob = static_cast<uint8_t const*>(rdb.propBlob(r + 1, pi)))
                flatScanCaps(props[pi].type, blob, props[pi].name, caps);

    //  Header: a null-data walk emits every column name, ragged ones padded to
    //  their capacity. The per-prop widths are kept for null-slot rows.
    std::vector<std::string>    columns{"__time__"};
    std::vector<std::size_t>    width(props.size(), 0);
    for (std::size_t pi = 0; pi < props.size(); pi++)
    {
        std::vector<std::pair<std::string, std::string>>    names;
        FlatRow::walk(names, props[pi].name, props[pi].type, nullptr, &caps);
        width[pi] = names.size();
        for (auto const& [name, _] : names)
            columns.push_back(name);
    }

    for (std::size_t i = 0; i < columns.size(); i++)
        os << (i ? "," : "") << columns[i];
    os << "\n";

    //  One row per real state (sentinels at 0 and numStates-1 are dropped).
    for (std::size_t r = 0; r < real; r++)
    {
        std::size_t si = r + 1;
        os << rdb.time(si);

        for (std::size_t pi = 0; pi < props.size(); pi++)
        {
            auto*   blob = static_cast<uint8_t const*>(rdb.propBlob(si, pi));

            if (blob != nullptr)
            {
                std::vector<std::pair<std::string, std::string>>    leaves;
                FlatRow::walk(leaves, props[pi].name, props[pi].type, blob, &caps);
                for (auto const& [_, value] : leaves)
                    os << "," << csvQuote(value);
            }
            else
            {
                //  A null slot's cells stay empty: a value read as the type's
                //  zero would be an invention.
                for (std::size_t k = 0; k < width[pi]; k++)
                    os << ",";
            }
        }
        os << "\n";
    }
}

void dump(std::string const& path, std::ostream& os)
{
    Reader r(path);

    os << "rdb:\n";
    os << "  path: "      << path          << "\n";
    os << "  numStates: " << r.numStates() << "\n";
    os << "  numProps: "  << r.numProps()  << "\n";
    os << "  rowBytes: "  << r.rowBytes()  << "\n";

    os << "schema:\n";
    os << "  data:\n";
    for (auto const& p : r.props())
    {
        os << "    - name: " << p.name << "\n";
        os << "      type: ";
        TypeYamlPrinter::render(os, p.type, 8) << "\n";
    }
    os << "  conf:";
    for (auto const& c : r.confs())
    {
        os << "\n";
        os << "    - name: " << c.name << "\n";
        os << "      type: ";
        TypeYamlPrinter::render(os, c.type, 8);
    }
    os << "\n";

    os << "conf:\n";
    {
        auto* base = static_cast<uint8_t const*>(r.confPtr());
        size_t cur = 0;

        for (auto const& c : r.confs())
        {
            size_t a = c.type->alignment();
            if (cur % a) cur += a - (cur % a);

            os << "  " << c.name << ":";
            DataYamlPrinter::render(os, c.type, base + cur, 2) << "\n";

            cur += c.type->size();
        }
    }

    os << "states:\n";
    for (size_t si = 0; si < r.numStates(); si++)
    {
        os << "  - index: " << si << "\n";
        os << "    time: "  << r.time(si) << "\n";
        os << "    props:\n";

        for (size_t pi = 0; pi < r.numProps(); pi++)
        {
            auto* blob = static_cast<uint8_t const*>(r.propBlob(si, pi));

            os << "      " << r.props()[pi].name << ": ";

            if (!blob)
            {
                os << "null\n";
                continue;
            }

            DataYamlPrinter::render(os, r.props()[pi].type, blob, 8);

            os << "\n";
        }
    }
}

} // namespace referee::db
