/*
 *  A streaming JSON writer.
 *
 *  Referee only ever *writes* JSON -- the readers are elsewhere, in Python --
 *  and that asymmetry is why there is no dependency here. A writer is objects,
 *  arrays, numbers and string escaping; a parser is the hard half, and nothing
 *  needs one.
 *
 *  Streaming, in the sense that matters: nothing is buffered and no document
 *  is built. Each value goes straight to the stream, so a run trace over a
 *  million-state capture costs no more memory than one over four. That is the
 *  whole reason the format is newline-delimited, and it only pays off if the
 *  producer honours it.
 *
 *  Scopes are RAII, which makes the two classic failures structural rather
 *  than a matter of care: a brace cannot be left unclosed, and a comma cannot
 *  be misplaced -- the writer knows whether anything has been emitted at this
 *  depth, so no caller has to.
 *
 *      json::Writer    out(os);
 *      {
 *          auto    doc = out.object();
 *          out.key("kind").value("signal");
 *          out.key("values");
 *          {
 *              auto    vals = out.array();
 *              for(auto v: values)
 *                  out.value(v);
 *          }
 *      }
 *      out.line();
 */
#pragma once

#include <cmath>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace json
{

class Writer
{
public:
    explicit Writer(std::ostream& os)
        : m_os(os)
    {
    }

    //  Closes its brace when it goes out of scope. Move-only: copying one
    //  would close the same scope twice.
    class Scope
    {
    public:
        Scope(Writer* writer, char close)
            : m_writer(writer)
            , m_close(close)
        {
        }

        Scope(Scope&& other) noexcept
            : m_writer(other.m_writer)
            , m_close(other.m_close)
        {
            other.m_writer = nullptr;
        }

        Scope(Scope const&)             = delete;
        Scope&  operator=(Scope const&) = delete;

        ~Scope()
        {
            if(m_writer != nullptr)
                m_writer->close(m_close);
        }

    private:
        Writer* m_writer;
        char    m_close;
    };

    [[nodiscard]] Scope     object()    { return open('{', '}'); }
    [[nodiscard]] Scope     array()     { return open('[', ']'); }

    Writer&     key(std::string_view name)
    {
        separate();
        quote(name);
        m_os << ':';
        m_afterKey = true;
        return *this;
    }

    Writer&     value(std::string_view v)    { separate(); quote(v);                    return *this; }
    Writer&     value(char const* v)         { return value(std::string_view(v)); }
    Writer&     value(bool v)                { separate(); m_os << (v ? "true" : "false"); return *this; }
    Writer&     value(std::int64_t v)        { separate(); m_os << v;                   return *this; }
    Writer&     value(std::uint64_t v)       { separate(); m_os << v;                   return *this; }
    Writer&     value(int v)                 { return value(static_cast<std::int64_t>(v)); }
    Writer&     value(unsigned v)            { return value(static_cast<std::int64_t>(v)); }

    //  JSON has no infinity or NaN, so there is nothing honest to write for
    //  them -- null at least says "no value here" rather than inventing one.
    Writer&     value(double v)
    {
        separate();

        if(v != v || v == HUGE_VAL || v == -HUGE_VAL)   m_os << "null";
        else                                            m_os << v;

        return *this;
    }

    Writer&     null()                       { separate(); m_os << "null";              return *this; }

    //  Ends a newline-delimited record.
    Writer&     line()                       { m_os << '\n';                            return *this; }

private:
    friend class Scope;

    Scope   open(char oc, char cc)
    {
        separate();
        m_os << oc;
        m_empty.push_back(true);
        return Scope(this, cc);
    }

    void    close(char cc)
    {
        m_empty.pop_back();
        m_os << cc;
    }

    //  A comma before everything except the first item at this depth, and
    //  never straight after a key -- the key already placed one.
    void    separate()
    {
        if(m_afterKey)
        {
            m_afterKey = false;
            return;
        }

        if(m_empty.empty())
            return;

        if(!m_empty.back())
            m_os << ',';

        m_empty.back() = false;
    }

    void    quote(std::string_view s)
    {
        m_os << '"';

        for(unsigned char c: s)
        {
            switch(c)
            {
            case '"':   m_os << "\\\"";     break;
            case '\\':  m_os << "\\\\";     break;
            case '\b':  m_os << "\\b";      break;
            case '\f':  m_os << "\\f";      break;
            case '\n':  m_os << "\\n";      break;
            case '\r':  m_os << "\\r";      break;
            case '\t':  m_os << "\\t";      break;
            default:
                //  Everything below 0x20 must be escaped; the rest goes
                //  through byte for byte, which keeps UTF-8 intact without
                //  this having to understand it.
                if(c < 0x20)
                {
                    static char const*  hex = "0123456789abcdef";
                    m_os << "\\u00" << hex[(c >> 4) & 0xF] << hex[c & 0xF];
                }
                else
                {
                    m_os << static_cast<char>(c);
                }
                break;
            }
        }

        m_os << '"';
    }

    std::ostream&       m_os;
    std::vector<bool>   m_empty;
    bool                m_afterKey = false;
};

} // namespace json
