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

#include "syntax.hpp"
#include <functional>
#include <map>
#include <vector>
#include <cstdint>
#include <string>

class Loader
{
public:
    using GetCell = std::function<std::string(std::string const&)>;

    //  How many columns a trace happens to carry for each array path -- the
    //  widest row in the file, keyed the way `inferSizes` keys it.
    //
    //  This is where capacity belongs and the only place it exists. Nothing
    //  above the loader can observe it: the descriptor carries a count, the
    //  count is the number of cells that held an element, and how many columns
    //  the header happened to have is an allocation figure. It is needed here
    //  only to know where to stop probing.
    using Caps = std::map<std::string, std::vector<unsigned>>;

    // Append the binary representation of 'type' rooted at CSV column 'prefix'
    // into 'buf'.  getCell maps a fully-qualified column name to its cell value.
    //
    // TypeString slots store a raw char const* from Strings::instance(). The
    // resulting buffer is only valid while that singleton is alive — do not
    // persist it across process lifetime or share it across processes.
    static void load(std::vector<uint8_t>& buf,
                     std::string const&    prefix,
                     Type*                 type,
                     GetCell const&        getCell,
                     Caps const&           caps = {});
};