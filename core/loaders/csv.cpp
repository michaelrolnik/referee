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

#include "csv.hpp"
#include "rapidcsv.h"

using namespace loader;

bool Csv::try_(std::string const& filename) const
{
    auto pos = filename.rfind('.');
    if (pos == std::string::npos) return false;
    return filename.substr(pos + 1) == "csv";
}

void Csv::load(std::istream& stream)
{
    m_doc = std::make_unique<rapidcsv::Document>(stream);
    m_cols = m_doc->GetColumnNames();
    for (size_t i = 0; i < m_cols.size(); i++)
        m_colIdx[m_cols[i]] = i;
}

size_t Csv::rowCount() const
{
    return m_doc->GetRowCount();
}

std::vector<std::string> Csv::columnNames() const
{
    return m_cols;
}

std::string Csv::cell(std::string const& col, size_t row) const
{
    auto it = m_colIdx.find(col);
    if (it == m_colIdx.end()) return "";
    try { return m_doc->GetCell<std::string>(it->second, row); }
    catch (...) { return ""; }
}
