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

#include "loader.hpp"

#include <cstring>
#include <deque>
#include "strings.hpp"

#include <stdexcept>

namespace {
static void alignBuffer(std::vector<uint8_t>& buf, size_t align)
{
    while (buf.size() % align) buf.push_back(0);
}
} // namespace

struct LoaderImpl
    : Visitor<TypeBoolean, TypeByte, TypeInteger, TypeNumber, TypeString, TypeEnum, TypeStruct, TypeArray>
{
    using GetCell = Loader::GetCell;

    //  An array with no written extent whose elements are still to be placed.
    //  The descriptor is sixteen bytes in the middle of the fixed layout; the
    //  elements go after everything else, so writing them where they are found
    //  would push every following member sideways.
    struct Pending
    {
        std::size_t     slot;       //  where the descriptor sits in m_buf
        std::string     prefix;     //  column prefix of the array itself
        TypeArray*      type;
    };

    std::vector<uint8_t>& m_buf;
    GetCell const&        m_getCell;
    Loader::Caps const&   m_caps;
    std::string           m_prefix;
    std::deque<Pending>   m_pending;

    LoaderImpl(std::vector<uint8_t>& buf, GetCell const& getCell, Loader::Caps const& caps)
        : m_buf(buf), m_getCell(getCell), m_caps(caps)
    {}

    void run(std::string const& prefix, Type* type)
    {
        m_prefix = prefix;
        type->accept(*this);
    }

    //  The column path an array's capacity is recorded under: the prefix with
    //  its subscripts elided, plus which of that path's dimensions this is.
    static std::pair<std::string, unsigned> pathOf(std::string const& prefix)
    {
        std::string     path;
        bool            skip = false;

        for(char c: prefix)
        {
            if      (c == '[')  skip = true;
            else if (c == ']')  skip = false;
            else if (!skip)     path += c;
        }

        //  `g[0]` is dimension 1 of path `g`; `msg[0].raw` is dimension 0 of
        //  `msg.raw`, since the subscripts that count are the ones on this
        //  name rather than on something it is reached through.
        unsigned        dim  = 0;
        auto            dot  = prefix.rfind('.');

        for(std::size_t i = (dot == std::string::npos ? 0 : dot + 1); i < prefix.size(); i++)
            if(prefix[i] == '[')
                dim++;

        return {path, dim};
    }

    //  How many elements this array's columns can hold. Absent from the table
    //  means the trace carries no columns for it, which is an empty array --
    //  not an error, since a specification may mention a signal a particular
    //  capture never recorded.
    unsigned capacity(std::string const& prefix) const
    {
        auto [path, dim] = pathOf(prefix);
        auto it          = m_caps.find(path);

        if(it == m_caps.end() || dim >= it->second.size())
            return 0;

        return it->second[dim];
    }

    //  Whether a cell holds an element. `-` says it does not, and so does an
    //  empty cell.
    bool absent(std::string const& col) const
    {
        auto    text = m_getCell(col);

        return  text.empty() || text == "-";
    }

    //  Whether an element is there at all, which is not a question about one
    //  cell once the elements are structs or arrays: `g[0]` is never a column,
    //  so asking for it directly said "absent" and every array of anything
    //  compound loaded as empty. An element is present if any leaf beneath it
    //  is.
    bool present(std::string const& prefix, Type* type)
    {
        if(auto* st = dynamic_cast<TypeStruct*>(type))
        {
            for(auto const& m: st->members)
                if(present(prefix + "." + m.name, m.data))
                    return true;

            return false;
        }

        if(auto* ar = dynamic_cast<TypeArray*>(type))
        {
            auto    n = ar->count != 0 ? ar->count : capacity(prefix);

            for(unsigned k = 0; k < n; k++)
                if(present(prefix + "[" + std::to_string(k) + "]", ar->type))
                    return true;

            return false;
        }

        return !absent(prefix);
    }

    //  Place the elements of every deferred array, after the fixed layout is
    //  complete. Elements may themselves defer -- an array of structs holding
    //  arrays -- so this drains a queue rather than looping over a snapshot.
    void settle()
    {
        while(!m_pending.empty())
        {
            auto    job = m_pending.front();
            m_pending.pop_front();

            auto    cap = capacity(job.prefix);
            auto    n   = 0u;
            auto    gap = false;

            for(unsigned k = 0; k < cap; k++)
            {
                //  An array has no holes, so a present element after an absent
                //  one is not a shorter array with a gap in it -- it is a file
                //  that means something the type cannot express, and guessing
                //  which end to believe is how a count and its cells come to
                //  disagree.
                if(!present(job.prefix + "[" + std::to_string(k) + "]", job.type->type))
                {
                    gap = true;
                    continue;
                }

                if(gap)
                    throw std::runtime_error(
                        "array '" + job.prefix + "' has a gap: element " + std::to_string(k)
                        + " is present but an earlier one is not");

                n = k + 1;
            }

            alignBuffer(m_buf, job.type->type->alignment());

            auto    data = m_buf.size();
            for(unsigned k = 0; k < n; k++)
                run(job.prefix + "[" + std::to_string(k) + "]", job.type->type);

            //  The offset is relative to the descriptor rather than to the
            //  start of the blob, so it survives the blob being placed
            //  anywhere -- which it is, twice: appended into the file's blob
            //  section, then mapped at whatever address the reader has.
            std::int64_t    count = n;
            std::int64_t    delta = static_cast<std::int64_t>(data)
                                  - static_cast<std::int64_t>(job.slot);

            std::memcpy(m_buf.data() + job.slot,     &count, sizeof(count));
            std::memcpy(m_buf.data() + job.slot + 8, &delta, sizeof(delta));
        }
    }

    //  Written like an integer, stored in one byte. Out-of-range is rejected
    //  rather than truncated, since a payload byte silently becoming a
    //  different value is exactly the kind of thing a checker exists to catch.
    void visit(TypeByte*) override
    {
        alignBuffer(m_buf, 1);
        auto        text = m_getCell(m_prefix);
        long long   v    = 0;
        try         { v = text.empty() ? 0 : std::stoll(text, nullptr, 0); }
        catch (...) { v = 0; }

        if (v < 0 || v > 255)
            throw std::runtime_error(
                "byte '" + m_prefix + "' out of range 0..255: '" + text + "'");

        m_buf.push_back(static_cast<std::uint8_t>(v));
    }

    void visit(TypeBoolean*) override
    {
        alignBuffer(m_buf, 1);
        auto s = m_getCell(m_prefix);
        m_buf.push_back((s == "true" || s == "yes" || s == "1") ? 1 : 0);
    }

    void visit(TypeInteger*) override
    {
        alignBuffer(m_buf, 8);
        int64_t v = 0;
        try { v = std::stoll(m_getCell(m_prefix)); } catch (...) {}
        m_buf.insert(m_buf.end(), reinterpret_cast<uint8_t*>(&v),
                                  reinterpret_cast<uint8_t*>(&v) + 8);
    }

    void visit(TypeNumber*) override
    {
        alignBuffer(m_buf, 8);
        double v = 0.0;
        try { v = std::stod(m_getCell(m_prefix)); } catch (...) {}
        m_buf.insert(m_buf.end(), reinterpret_cast<uint8_t*>(&v),
                                  reinterpret_cast<uint8_t*>(&v) + 8);
    }

    void visit(TypeString*) override
    {
        alignBuffer(m_buf, 8);
        const char* ptr = Strings::instance()->getString(m_getCell(m_prefix));
        m_buf.insert(m_buf.end(), reinterpret_cast<const uint8_t*>(&ptr),
                                  reinterpret_cast<const uint8_t*>(&ptr) + 8);
    }

    void visit(TypeEnum* type) override
    {
        alignBuffer(m_buf, 1);
        auto    name = m_getCell(m_prefix);
        uint8_t v    = 0;
        for (unsigned i = 0; i < type->items.size(); i++) {
            if (type->items[i] == name) { v = static_cast<uint8_t>(i + 1); break; }
        }
        m_buf.push_back(v);
    }

    void visit(TypeStruct* type) override
    {
        auto   prefix = m_prefix;
        size_t start  = m_buf.size();
        for (auto& m : type->members)
            run(prefix + "." + m.name, m.data);
        size_t sz = m_buf.size() - start, align = type->alignment();
        size_t rem = sz % align;
        for (size_t i = 0; i < (rem ? align - rem : 0); i++) m_buf.push_back(0);
    }

    void visit(TypeArray* type) override
    {
        auto prefix = m_prefix;
        if (type->count == 0) {
            //  `{size_t count; T const* data;}`, with the pointer written as a
            //  self-relative offset until the reader knows where the blob
            //  landed. Reserved here and filled by settle().
            alignBuffer(m_buf, 8);
            m_pending.push_back({m_buf.size(), prefix, type});
            m_buf.insert(m_buf.end(), 16, 0);
        } else {
            for (unsigned i = 0; i < type->count; i++)
                run(prefix + "[" + std::to_string(i) + "]", type->type);
        }
    }
};

void Loader::load(std::vector<uint8_t>& buf,
                      std::string const&    prefix,
                      Type*                 type,
                      GetCell const&        getCell,
                      Caps const&           caps)
{
    LoaderImpl  impl(buf, getCell, caps);

    impl.run(prefix, type);
    impl.settle();
}
