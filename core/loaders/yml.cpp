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

#include "yml.hpp"

using namespace loader;

bool Yml::try_(std::string const& filename) const
{
    auto pos = filename.rfind('.');
    if (pos == std::string::npos) return false;
    auto ext = filename.substr(pos + 1);
    return ext == "yml" || ext == "yaml";
}

void Yml::load(std::istream& stream)
{
    m_root       = YAML::Load(stream);
    m_isSequence = m_root.IsSequence();

    // Build column list from the first row (sequence) or the map itself.
    auto const& first = m_isSequence ? m_root[0] : m_root;
    for (auto it = first.begin(); it != first.end(); ++it)
        m_cols.push_back(it->first.as<std::string>());
}

size_t Yml::rowCount() const
{
    return m_isSequence ? m_root.size() : 1;
}

std::vector<std::string> Yml::columnNames() const
{
    return m_cols;
}

std::string Yml::cell(std::string const& col, size_t row) const
{
    try {
        auto const& node = m_isSequence ? m_root[row][col] : m_root[col];
        if (!node.IsDefined()) return "";
        return node.as<std::string>();
    }
    catch (...) { return ""; }
}
