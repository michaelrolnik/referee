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
 *  The std::string builtins' host half. Compiled into both the main build
 *  (where the JIT registers them as absolute symbols) and libreferee_rt
 *  (where an ahead-of-time checker links them), so a spec using
 *  std::string::len and friends behaves identically under `execute` and
 *  under a compiled checker -- one implementation, two linkings.
 */

#include <cstdint>
#include <cstring>

extern "C" {

std::int64_t    __ref_str_len(char const* s)
{
    return s ? static_cast<std::int64_t>(std::strlen(s)) : 0;
}

//  Out of range yields -1 rather than reading past the end. There is no
//  bounds checking elsewhere in the language, but here the alternative is a
//  segfault in the checker rather than a wrong answer about the trace.
std::int64_t    __ref_str_at(char const* s, std::int64_t i)
{
    if (s == nullptr || i < 0 || static_cast<std::size_t>(i) >= std::strlen(s))
        return -1;

    return static_cast<unsigned char>(s[i]);
}

std::int64_t    __ref_str_cmp(char const* a, char const* b)
{
    return std::strcmp(a ? a : "", b ? b : "");
}

bool            __ref_str_starts(char const* s, char const* p)
{
    if (s == nullptr || p == nullptr) return false;
    auto n = std::strlen(p);
    return std::strlen(s) >= n && std::strncmp(s, p, n) == 0;
}

bool            __ref_str_ends(char const* s, char const* p)
{
    if (s == nullptr || p == nullptr) return false;
    auto ls = std::strlen(s), lp = std::strlen(p);
    return ls >= lp && std::strcmp(s + ls - lp, p) == 0;
}

//  -1 when absent, so a caller can test for it without a second call.
std::int64_t    __ref_str_find(char const* s, char const* p)
{
    if (s == nullptr || p == nullptr) return -1;
    auto at = std::strstr(s, p);
    return at ? static_cast<std::int64_t>(at - s) : -1;
}

} // extern "C"