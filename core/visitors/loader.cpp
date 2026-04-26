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

#include "loader.hpp"
#include "strings.hpp"

#include <stdexcept>

namespace {
static void alignBuffer(std::vector<uint8_t>& buf, size_t align)
{
    while (buf.size() % align) buf.push_back(0);
}
} // namespace

struct LoaderImpl
    : Visitor<TypeBoolean, TypeInteger, TypeNumber, TypeString, TypeEnum, TypeStruct, TypeArray>
{
    using GetCell = Loader::GetCell;

    std::vector<uint8_t>& m_buf;
    GetCell const&        m_getCell;
    std::string           m_prefix;

    LoaderImpl(std::vector<uint8_t>& buf, GetCell const& getCell)
        : m_buf(buf), m_getCell(getCell)
    {}

    void run(std::string const& prefix, Type* type)
    {
        m_prefix = prefix;
        type->accept(*this);
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
            throw std::runtime_error(
                "dynamic arrays (count=0) are not supported in CSV/YAML loader: column '" +
                prefix + "'");
        } else {
            for (unsigned i = 0; i < type->count; i++)
                run(prefix + "[" + std::to_string(i) + "]", type->type);
        }
    }
};

void Loader::load(std::vector<uint8_t>& buf,
                      std::string const&    prefix,
                      Type*                 type,
                      GetCell const&        getCell)
{
    LoaderImpl(buf, getCell).run(prefix, type);
}
