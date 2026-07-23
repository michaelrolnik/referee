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

#include "strings.hpp"

#include <mutex>

#include <memory>
#include <string.h>

struct cstrless {
    bool operator()(const char* a, const char* b) const
    {
        return strcmp(a, b) < 0;
    }
};

class StringsImpl
    : public Strings
{
public:     
    char const* getString(char const* data) override;

private:
    std::set<const char*, cstrless> m_set;
};

char const* Strings::getString(std::string const& data)
{
    return getString(data.c_str());
}

char const* StringsImpl::getString(char const* data)
{
    //  Interning mutates the set; concurrent compilations (and a checker's
    //  string fixup) go through here, so it takes a lock like the factory.
    static std::mutex           m;
    std::lock_guard<std::mutex> lock(m);

    auto iter = m_set.find(data);

    if(iter == m_set.end())
    {
        auto cstr   = strdup(data);

        m_set.insert(cstr);

        return cstr;
    }

    return *iter;
}

Strings*    Strings::instance()
{
    static Strings* instance    = new StringsImpl();

    return instance;
}
