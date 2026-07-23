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

#include "merge.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace referee::db
{

namespace
{

//  One recorded sample of a column: the time it was reported, and the value.
struct Event
{
    std::int64_t    time;
    std::string     value;
};

std::int64_t    parseTime(std::string const& text, std::string const& where)
{
    try         { return std::stoll(text); }
    catch (...) { throw std::runtime_error("merge: unparseable __time__ '" + text + "' " + where); }
}

} // namespace

std::string     mergeTraces(std::vector<loader::Row*> const&   docs,
                            LeadingGap                          leading,
                            Overlap                             overlap)
{
    //  1. The merged column set, in first-seen order, and which source(s) each
    //     column came from. A column in two sources is either an error or, with
    //     --overlap merge, one signal whose samples both sources contribute to.
    std::vector<std::string>                        columns;
    std::map<std::string, int>                      seenIn;     //  column -> a source it appeared in

    for (std::size_t si = 0; si < docs.size(); si++)
    {
        bool    hasTime = false;

        for (auto const& col : docs[si]->columnNames())
        {
            if (col == "__time__") { hasTime = true; continue; }

            auto    it = seenIn.find(col);
            if (it == seenIn.end())
            {
                columns.push_back(col);
                seenIn.emplace(col, static_cast<int>(si));
            }
            else if (overlap == Overlap::Error)
            {
                throw std::runtime_error(
                    "merge: column '" + col + "' appears in more than one source"
                    " -- pass --overlap merge to combine them, or --overlap error"
                    " (the default) if that is a mistake");
            }
        }

        if (!hasTime)
            throw std::runtime_error(
                "merge: source #" + std::to_string(si) + " has no __time__ column");
    }

    //  2. Every column's samples, gathered across whichever sources hold it,
    //     and the union of all timestamps. Sorting is stable, so among samples
    //     with the same timestamp the later source wins -- last write on a tie.
    std::map<std::string, std::vector<Event>>       events;
    std::set<std::int64_t>                          times;

    for (std::size_t si = 0; si < docs.size(); si++)
    {
        auto*   d = docs[si];

        for (std::size_t r = 0; r < d->rowCount(); r++)
        {
            auto    t = parseTime(d->cell("__time__", r),
                                  "in source #" + std::to_string(si) + " row " + std::to_string(r));
            times.insert(t);

            for (auto const& col : d->columnNames())
                if (col != "__time__")
                    events[col].push_back({t, d->cell(col, r)});
        }
    }

    for (auto& [col, ev] : events)
        std::stable_sort(ev.begin(), ev.end(),
                         [](Event const& a, Event const& b) { return a.time < b.time; });

    //  3. Where to start. Trim drops every row before the last of the columns
    //     has its first sample, so no column is ever invented; the other modes
    //     keep every row and fill the leading gap.
    std::int64_t    start = times.empty() ? 0 : *times.begin();
    if (leading == LeadingGap::Trim)
        for (auto const& col : columns)
        {
            auto const& ev = events[col];
            if (!ev.empty())
                start = std::max(start, ev.front().time);
        }

    //  4. Emit. A cursor per column walks its samples forward as the timestamp
    //     increases, so the whole thing is one linear pass rather than a search
    //     per cell.
    std::ostringstream          out;
    out << "__time__";
    for (auto const& col : columns)
        out << "," << col;
    out << "\n";

    std::map<std::string, std::size_t>  cursor;
    for (auto const& col : columns)
        cursor[col] = 0;

    for (std::int64_t t : times)
    {
        if (leading == LeadingGap::Trim && t < start)
            continue;

        out << t;
        for (auto const& col : columns)
        {
            auto const& ev  = events[col];
            auto&       cur = cursor[col];

            //  Advance to the most recent sample at or before t.
            while (cur + 1 < ev.size() && ev[cur + 1].time <= t)
                cur++;

            if (!ev.empty() && ev[cur].time <= t)
                out << "," << ev[cur].value;                //  forward-filled real value
            else if (leading == LeadingGap::Backfill && !ev.empty())
                out << "," << ev.front().value;             //  earliest real value
            else
                out << ",";                                 //  zero: REF reads the empty cell as 0
        }
        out << "\n";
    }

    return out.str();
}

} // namespace referee::db
