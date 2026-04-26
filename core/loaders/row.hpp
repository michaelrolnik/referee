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

#include <istream>
#include <memory>
#include <string>
#include <vector>

namespace loader
{
// Abstract tabular data source.  Concrete implementations probe the filename
// extension via try_() and parse the stream via load().
class Row
{
public:
    virtual ~Row() = default;

    // Returns true if this loader handles files with the given name/extension.
    virtual bool try_(std::string const& filename) const = 0;

    // Parse the stream.  Called by open() after try_() succeeds.
    virtual void load(std::istream& stream) = 0;

    virtual size_t                   rowCount()    const = 0;
    virtual std::vector<std::string> columnNames() const = 0;

    // Returns the cell value for (col, row), or "" if the column/row is absent.
    virtual std::string cell(std::string const& col, size_t row) const = 0;

    // Factory: picks the first registered loader whose try_() accepts filename,
    // loads the stream, and returns the ready-to-query source.
    static std::unique_ptr<Row> open(std::istream& stream,
                                           std::string const& filename);
};

}
