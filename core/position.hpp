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

#include <string>

struct Location
{
    Location() = default;
    Location(unsigned row, unsigned col): row(row), col(col) {}

    unsigned    row = 0;
    unsigned    col = 0;
};

struct Position
{
    Position() = default;
    Position(Location beg, Location end): beg(beg), end(end) {}
    Position(Location beg, Location end, char const* file)
        : file(file), beg(beg), end(end) {}

    //  Which source file this node came from, as a path relative to the root
    //  .ref.  Null/empty for the root file itself, so a single-file program
    //  keeps its original `row:col .. row:col` labels unchanged.
    //
    //  Interned by Strings, so copying a Position stays cheap even though
    //  every AST node carries one.
    char const* file    = nullptr;

    //  `row:col .. row:col`, prefixed with the file for anything that came in
    //  through an import.  This doubles as the requirement's label in the
    //  PASS/FAIL report and as the name of its generated LLVM function, so it
    //  has to distinguish two requirements that sit at the same line and
    //  column in different files.
    std::string text() const
    {
        std::string out;

        if (file != nullptr && *file != '\0')
        {
            out += file;
            out += ":";
        }

        out += std::to_string(beg.row) + ":" + std::to_string(beg.col)
            +  " .. "
            +  std::to_string(end.row) + ":" + std::to_string(end.col);

        return out;
    }

    Location    beg;
    Location    end;
};