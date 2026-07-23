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

#include "loaders/row.hpp"

#include <string>
#include <vector>

namespace referee::db
{

//  Signals sampled by different sources at different rates, folded into one
//  trace of complete rows.
//
//  REF's model is sample-and-hold *between* rows but an empty cell reads as the
//  type's zero *within* one -- so a merge cannot leave gaps and rely on the
//  reader to fill them. It has to materialise the hold itself: the union of
//  every source's timestamps, and at each one every signal takes the most
//  recent value its own source reported at or before that time.
enum class LeadingGap
{
    Trim,       //  drop rows before every signal has reported once
    Zero,       //  fill with the type's zero, matching REF's empty-cell rule
    Backfill,   //  use each signal's earliest real value
};

enum class Overlap
{
    Error,      //  a column in more than one source is a mistake
    Merge,      //  union the two sources' samples of the one signal
};

//  Merge `docs` into a single CSV document -- `__time__` plus every signal
//  column, one complete row per distinct timestamp. Returns the CSV text,
//  which `ingest` turns into a `.rdb`; kept separate from packing so the merge
//  itself is schema-agnostic and testable without a `.ref`.
std::string     mergeTraces(std::vector<loader::Row*> const&   docs,
                            LeadingGap                          leading,
                            Overlap                             overlap);

} // namespace referee::db
