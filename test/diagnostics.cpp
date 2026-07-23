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

//  Negative tests: the paths a well-formed input never reaches.
//
//  These are the type checker's rejections and the .rdb reader's validation of
//  a damaged file. Both are almost entirely error-handling code, so they stay
//  unexercised by any test built from valid inputs -- and error handling that
//  is never run is error handling that has never been shown to work.

#include <gtest/gtest.h>

#include "referee.hpp"
#include "module.hpp"
#include "rdb/database.hpp"
#include "rdb/ingest.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

//  Parse a snippet; each gets a unique module name so the Factory cache does
//  not carry declarations between cases.
void    parseSnippet(std::string const& src, std::string const& tag)
{
    std::istringstream  is(src);
    (void) Referee::parseSchema(is, "<" + tag + ">");
}

std::string     tmpFile(std::string const& tag)
{
    std::string         pat = "/tmp/referee-diag-" + tag + "-XXXXXX";
    std::vector<char>   buf(pat.begin(), pat.end());
    buf.push_back('\0');
    int     fd = ::mkstemp(buf.data());
    EXPECT_GE(fd, 0);
    if (fd >= 0) ::close(fd);
    return std::string(buf.data());
}

std::vector<std::uint8_t>   readFile(std::string const& path)
{
    std::ifstream               in(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), {});
}

void    writeFile(std::string const& path, std::vector<std::uint8_t> const& bytes)
{
    std::ofstream   out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<char const*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

//  A valid .rdb to damage in the tests below.
std::string     buildGoodRdb(std::string const& tag)
{
    auto    refPath  = std::string(REFEREE_TEST_DATA_DIR) + "/pass.ref";
    auto    csvPath  = std::string(REFEREE_TEST_DATA_DIR) + "/data.csv";
    auto    confPath = std::string(REFEREE_TEST_DATA_DIR) + "/conf.csv";
    auto    out      = tmpFile(tag);

    referee::db::ingest(refPath, csvPath, confPath, out);
    return out;
}

} // namespace

// ── Type checking ────────────────────────────────────────────────────────────

// Arithmetic and comparison are typed; mixing categories has to be rejected
// rather than silently reinterpreted.
TEST(Diagnostics, RejectsMistypedOperands)
{
    char const* cases[] = {
        "data a : boolean;\ndata i : integer;\na + i;\n",        // arith on boolean
        "data a : boolean;\n-a;\n",                              // negate a boolean
        "data i : integer;\n!i;\n",                              // not an integer
        "data i : integer;\ndata s : string;\ni < s;\n",         // ordered compare on string
        "data i : integer;\ndata s : string;\ni + s;\n",         // add a string
        "data a : boolean;\ndata i : integer;\na && i;\n",       // conjoin an integer
        "data i : integer;\ni && i;\n",
        "data i : integer;\ni => i;\n",
    };

    int     n = 0;
    for (auto const* src : cases)
    {
        EXPECT_THROW(parseSnippet(src, "mistyped" + std::to_string(n++)), std::exception)
            << src;
    }
}

// Temporal operators quantify over boolean-valued expressions.
TEST(Diagnostics, RejectsNonBooleanTemporalOperands)
{
    char const* cases[] = {
        "data i : integer;\nG(i);\n",
        "data i : integer;\nF(i);\n",
        "data i : integer;\nXs(i);\n",
        "data i : integer;\nYs(i);\n",
        "data i : integer;\nH(i);\n",
        "data i : integer;\nO(i);\n",
        "data i : integer;\ndata a : boolean;\nUs(i, a);\n",
        "data i : integer;\ndata a : boolean;\nSs(a, i);\n",
        "data i : integer;\ndata a : boolean;\nRs(i, a);\n",
        "data i : integer;\ndata a : boolean;\nTs(a, i);\n",
    };

    int     n = 0;
    for (auto const* src : cases)
    {
        EXPECT_THROW(parseSnippet(src, "temporal" + std::to_string(n++)), std::exception)
            << src;
    }
}

// A time bound is a count of time units, so it has to be an integer.
TEST(Diagnostics, RejectsNonIntegerTimeBounds)
{
    char const* cases[] = {
        "data a : boolean;\ndata b : boolean;\nG[a:1000](b);\n",
        "data a : boolean;\ndata b : boolean;\nUs[0:a](a, b);\n",
    };

    int     n = 0;
    for (auto const* src : cases)
        EXPECT_THROW(parseSnippet(src, "bound" + std::to_string(n++)), std::exception) << src;
}

// Names have to resolve, and member/index access has to make sense for the
// type it is applied to.
TEST(Diagnostics, RejectsBadAccess)
{
    char const* cases[] = {
        "data a : boolean;\nnosuch;\n",                                  // unknown name
        "data a : boolean;\na.field;\n",                                 // member of a scalar
        "data a : boolean;\na[0];\n",                                    // index a scalar
        "type S : struct { x : integer; };\ndata s : S;\ns.nope;\n",     // unknown member
        "type E : enum { ON };\ndata e : E;\ne.OFF;\n",                  // unknown enum member
        "data i : integer[2];\ndata a : boolean;\ni[a];\n",              // non-integer index
    };

    int     n = 0;
    for (auto const* src : cases)
        EXPECT_THROW(parseSnippet(src, "access" + std::to_string(n++)), std::exception) << src;
}

// The ternary's arms must agree, and its condition must be boolean.
TEST(Diagnostics, RejectsIllTypedTernary)
{
    char const* cases[] = {
        "data a : boolean;\ndata i : integer;\ndata s : string;\na ? i : s;\n",
        "data i : integer;\ni ? i : i;\n",
    };

    int     n = 0;
    for (auto const* src : cases)
        EXPECT_THROW(parseSnippet(src, "ternary" + std::to_string(n++)), std::exception) << src;
}

// A computed signal's defining expression is type-checked like any other.
TEST(Diagnostics, RejectsIllTypedComputedSignal)
{
    EXPECT_THROW(
        parseSnippet("data i : integer;\ndata bad = G(i);\n", "computed"),
        std::exception);
}

// ── .rdb file validation ─────────────────────────────────────────────────────

TEST(Diagnostics, RejectsRdbTooSmall)
{
    auto    path = tmpFile("tiny");
    writeFile(path, {1, 2, 3, 4});

    EXPECT_THROW(referee::db::Reader r(path), std::exception);
    std::remove(path.c_str());
}

TEST(Diagnostics, RejectsRdbBadMagic)
{
    auto    path  = buildGoodRdb("magic");
    auto    bytes = readFile(path);
    ASSERT_GE(bytes.size(), 8u);

    bytes[0] ^= 0xFF;                       // corrupt the magic only
    writeFile(path, bytes);

    EXPECT_THROW(referee::db::Reader r(path), std::exception);
    std::remove(path.c_str());
}

TEST(Diagnostics, RejectsRdbTruncated)
{
    auto    path  = buildGoodRdb("trunc");
    auto    bytes = readFile(path);
    ASSERT_GT(bytes.size(), 64u);

    bytes.resize(bytes.size() / 2);         // lop off the back half
    writeFile(path, bytes);

    EXPECT_THROW(referee::db::Reader r(path), std::exception);
    std::remove(path.c_str());
}

TEST(Diagnostics, RejectsMissingRdbFile)
{
    EXPECT_THROW(referee::db::Reader r("/no/such/trace.rdb"), std::exception);
}

// The reader cross-checks its embedded schema against the .ref before running,
// so a trace packed from one specification must be refused by another.
TEST(Diagnostics, RejectsSchemaMismatch)
{
    auto    rdbPath = buildGoodRdb("mismatch");

    // A .ref whose declarations do not match what the file was packed from.
    std::istringstream  other("data completely : boolean;\nG(completely);\n");
    std::ostringstream  out;

    EXPECT_THROW(
        Referee::executeRdb(other, "<other>", rdbPath, out),
        std::exception);

    std::remove(rdbPath.c_str());
}

// ── Writer contract ──────────────────────────────────────────────────────────

// The Writer requires its calls in a particular order and shape. These are the
// checks that catch a caller getting it wrong, and they document the contract
// as much as they test it.
TEST(Diagnostics, WriterRejectsMisuse)
{
    TypeBoolean                         tBool;
    std::vector<referee::db::PropDecl>  props = {{"b", &tBool}};

    auto    oneBlob  = [] { return std::vector<std::vector<std::uint8_t>>{{1}}; };
    auto    twoBlobs = [] { return std::vector<std::vector<std::uint8_t>>{{1}, {0}}; };

    // writeState before setNumStates.
    {
        std::ostringstream      os;
        referee::db::Writer     w(os);
        w.setSchema(props, {});
        EXPECT_THROW(w.writeState(0, 0, oneBlob()), std::exception);
    }

    // state index past the end.
    {
        std::ostringstream      os;
        referee::db::Writer     w(os);
        w.setSchema(props, {});
        w.setNumStates(2);
        EXPECT_THROW(w.writeState(5, 0, oneBlob()), std::exception);
    }

    // wrong number of prop blobs for the schema.
    {
        std::ostringstream      os;
        referee::db::Writer     w(os);
        w.setSchema(props, {});
        w.setNumStates(2);
        EXPECT_THROW(w.writeState(0, 0, twoBlobs()), std::exception);
    }

    // finish() without setNumStates.
    {
        std::ostringstream      os;
        referee::db::Writer     w(os);
        w.setSchema(props, {});
        EXPECT_THROW(w.finish(), std::exception);
    }

    // finish() without setConfBlob.
    {
        std::ostringstream      os;
        referee::db::Writer     w(os);
        w.setSchema(props, {});
        w.setNumStates(1);
        w.writeState(0, 0, oneBlob());
        EXPECT_THROW(w.finish(), std::exception);
    }

    // finish() with a state that was never written.
    {
        std::ostringstream      os;
        referee::db::Writer     w(os);
        w.setSchema(props, {});
        w.setNumStates(2);
        w.setConfBlob({});
        w.writeState(0, 0, oneBlob());          // index 1 left unwritten
        EXPECT_THROW(w.finish(), std::exception);
    }
}

// ── Trace ingestion ──────────────────────────────────────────────────────────

// Known gaps in trace loading, deliberately not asserted here because pinning
// the current behaviour would entrench it:
//
//   * a column the .ref declares but the trace omits is tolerated rather than
//     reported, so the signal silently reads as whatever the blob happens to
//     hold;
//   * a cell that does not parse as its declared type is coerced rather than
//     refused ("notanumber" in an integer column yields a value).
//
// Both are behavioural changes rather than test fixes, so they are left for a
// separate pass.

// Built-ins are checked like any other call, and their names are namespaced
// so they cannot collide with anything a specification declares.
TEST(Diagnostics, RejectsBadBuiltinCalls)
{
    char const* cases[] = {
        //  arity
        "data bi : integer;\nG(std::math::abs() == 0);\n",
        "data bj : integer;\nG(std::math::abs(bj, bj) == 0);\n",
        "data bk : integer;\nG(std::math::min(bk) == 0);\n",
        //  `abs` is overloaded and takes either, but a name with only one
        //  overload still refuses the other kind
        "data bm : integer;\nG(std::math::sqrt(bm) == 0.0);\n",
        "data bn : string;\nG(std::math::sqrt(bn) == 0.0);\n",
        //  no such built-in
        "data bo : integer;\nG(std::math::nosuch(bo) == 0);\n",
        "data bp : integer;\nG(std::nosuch::abs(bp) == 0);\n",
    };

    int     n = 0;
    for (auto const* src : cases)
        EXPECT_THROW(parseSnippet(src, "bi" + std::to_string(n++)), std::exception) << src;
}

// The string built-ins take a string first, and an index or a second string
// after it.
TEST(Diagnostics, RejectsBadStringBuiltins)
{
    char const* cases[] = {
        "data sa : integer;\nG(std::string::len(sa) > 0);\n",
        "data sb : string;\nG(std::string::nth(sb, sb) == 0);\n",
        "data sc : string;\ndata sd : integer;\nG(std::string::starts(sc, sd));\n",
        "data se : string;\nG(std::string::len(se, se) > 0);\n",
        "data sf : string;\nG(std::string::nth(sf) == 0);\n",
    };

    int     n = 0;
    for (auto const* src : cases)
        EXPECT_THROW(parseSnippet(src, "sb" + std::to_string(n++)), std::exception) << src;
}

// Slicing: what may be sliced, and by what.
TEST(Diagnostics, RejectsBadSlices)
{
    char const* cases[] = {
        //  only an array can be sliced
        "data i : integer;\nG(i[0:1] == 0);\n",
        "data s : string;\nG(s[0:1] == 0);\n",
        "type P : struct { x : integer; };\ndata p : P;\nG(p[0:1] == 0);\n",
        //  bounds are integers, not numbers or booleans
        "data v : integer[4];\ndata r : number;\nG(v[r:2][0] == 0);\n",
        "data v : integer[4];\ndata b : boolean;\nG(v[0:b][0] == 0);\n",
    };

    int     n = 0;
    for (auto const* src : cases)
        EXPECT_THROW(parseSnippet(src, "slice" + std::to_string(n++)), std::exception) << src;
}

// A `func` is checked against its declaration and nothing else: the
// implementation may not exist at compile time, so the declaration is the only
// contract there is.
TEST(Diagnostics, RejectsBadCalls)
{
    //  Every snippet uses names of its own. AST signal nodes are interned
    //  process-globally, so a name declared with one type in another test
    //  leaks its type into this one -- the open bug noted in
    //  test/logic/nested_extents.ref.
    char const* cases[] = {
        //  not declared
        "data ca : integer;\nG(ca_nosuch(ca) == 0);\n",
        //  wrong arity, both directions
        "func cb_f : (integer) -> integer;\ndata cb : integer;\nG(cb_f() == 0);\n",
        "func cc_f : (integer) -> integer;\ndata cc : integer;\nG(cc_f(cc, cc) == 0);\n",
        //  wrong argument type -- no silent widening, since a C function has
        //  one signature and reinterpreting the bits would be worse than a
        //  rejection
        "func cd_f : (integer) -> integer;\ndata cd : number;\nG(cd_f(cd) == 0);\n",
        "func ce_f : (number) -> number;\ndata ce : string;\nG(ce_f(ce) == 0.0);\n",
        //  an array parameter takes an array, of exactly the right element
        //  type: `byte` reads as an integer everywhere else, but an array's
        //  layout is its element width
        "func cf_f : (byte[]) -> integer;\ndata cf : integer;\nG(cf_f(cf) == 0);\n",
        "func cg_f : (byte[]) -> integer;\ndata cg : integer[4];\nG(cg_f(cg) == 0);\n",
        //  two declarations of one name are fine; two with the same argument
        //  types are not, since no call could tell them apart
        "func ch_f : (integer) -> integer;\nfunc ch_f : (integer) -> number;\n"
        "data ch : integer;\nG(ch_f(ch) == 0);\n",
        //  no overload takes this shape
        "func cj_f : (integer) -> integer;\nfunc cj_f : (number) -> number;\n"
        "data cj : string;\nG(cj_f(cj) == 0);\n",
        //  the result is used as its declared type
        "func ci_f : (integer) -> boolean;\ndata ci : integer;\nG(ci_f(ci) + 1 == 2);\n",
    };

    int     n = 0;
    for (auto const* src : cases)
        EXPECT_THROW(parseSnippet(src, "call" + std::to_string(n++)), std::exception) << src;
}

// A diagnostic must describe what is wrong with the specification, not name
// the C++ function that noticed. Twenty-nine sites threw bare
// __PRETTY_FUNCTION__, which surfaced to users as, for example,
// "virtual void TypeCalcImpl::visit(ExprXw*)".
//
// The check is deliberately on the shape of the message rather than its exact
// wording, so it keeps working as the wording improves.
TEST(Diagnostics, MessagesAreNotCppSignatures)
{
    char const* cases[] = {
        //  a name that resolves to nothing
        "data i : integer;\nG(nosuchsignal > 0);\n",
        //  next/yesterday: the formula must be boolean, the repeat count integral
        "data i : integer;\nG(Xw(i));\n",
        "data i : integer;\nG(Xs(i));\n",
        "data b : boolean;\nG(Ys(b, b));\n",
        "data b : boolean;\nG(Yw(b, b));\n",
        //  a time bound is a number of time units
        "data b : boolean;\nG[b:2](b);\n",
        //  a member that the type does not have
        "type P : struct { x : integer; };\ndata p : P;\nG(p.nosuch == 1);\n",
        //  negation of something with no sign
        "data s : string;\nG(-s == 0);\n",
    };

    int     n = 0;
    for (auto const* src : cases)
    {
        try
        {
            parseSnippet(src, "sig" + std::to_string(n++));
            ADD_FAILURE() << "expected a rejection for: " << src;
        }
        catch (std::exception const& e)
        {
            std::string     what = e.what();

            EXPECT_EQ(what.find("::visit("), std::string::npos)  << what;
            EXPECT_EQ(what.find("Impl::"), std::string::npos)    << what;
            EXPECT_EQ(what.find("virtual "), std::string::npos)  << what;
            EXPECT_FALSE(what.empty())                           << src;
        }
    }
}

// Enums are nominal, so equality is only defined between two values of the
// same enum type, and ordering is not defined at all -- `<` would compare
// declaration positions, which is not a meaning the language gives them.
TEST(Diagnostics, RejectsBadEnumComparison)
{
    char const* cases[] = {
        //  ordering on enums
        "type E : enum { A, B };\ndata e : E;\nG(e < e);\n",
        "type E : enum { A, B };\ndata e : E;\nG(e >= e);\n",
        //  two different enum types
        "type E : enum { A, B };\ntype F : enum { C, D };\n"
        "data e : E;\ndata f : F;\nG(e == f);\n",
        //  an enum against a number
        "type E : enum { A, B };\ndata e : E;\nG(e == 0);\n",
        "type E : enum { A, B };\ndata e : E;\nG(e != 1);\n",
    };

    int     n = 0;
    for (auto const* src : cases)
    {
        EXPECT_THROW(parseSnippet(src, "enumcmp" + std::to_string(n++)), std::exception) << src;
    }
}

// Bitwise operators read the bit pattern of an integer. A number has no such
// reading, and booleans already have `&&` / `||`, so both are refused rather
// than silently converted -- and `^` alone is polymorphic, because there is no
// `^^` for booleans to use instead.
TEST(Diagnostics, RejectsNonIntegerBitwise)
{
    char const* cases[] = {
        "data r : number;\nG((r & 1) == 0);\n",
        "data r : number;\nG((1 | r) == 0);\n",
        "data r : number;\nG((r << 1) == 0);\n",
        "data r : number;\nG((r >> 1) == 0);\n",
        "data r : number;\nG(~r == 0);\n",
        "data a : boolean;\nG((a & 1) == 0);\n",
        "data a : boolean;\nG(~a);\n",
        //  `&` binds looser than `==`, as in C, so this reads as
        //  `n & (1 == 1)` -- which C computes and REF refuses.
        "data n : integer;\nG(n & 1 == 1);\n",
    };

    int     n = 0;
    for (auto const* src : cases)
    {
        EXPECT_THROW(parseSnippet(src, "bitwise" + std::to_string(n++)), std::exception) << src;
    }
}

// A byte holds 0..255. A cell outside that range is refused rather than
// truncated: a payload octet silently becoming a different value is exactly
// what a checker exists to catch.
TEST(Diagnostics, RejectsOutOfRangeByte)
{
    auto    refPath = tmpFile("byterange") + ".ref";
    {
        std::ofstream   f(refPath);
        f << "data b : byte;\nG(b >= 0);\n";
    }

    for (auto const* cell : {"256", "-1", "0x100"})
    {
        auto    csv = tmpFile("byterange") + ".csv";
        {
            std::ofstream   f(csv);
            f << "__time__,b\n0," << cell << "\n";
        }
        auto    out = tmpFile("byterange") + ".rdb";

        EXPECT_THROW(referee::db::ingest(refPath, csv, "", out), std::exception) << cell;

        std::remove(csv.c_str());
        std::remove(out.c_str());
    }

    std::remove(refPath.c_str());
}

// An unrecognised trace extension has no loader.
TEST(Diagnostics, RejectsUnknownTraceFormat)
{
    auto    refPath = std::string(REFEREE_TEST_DATA_DIR) + "/pass.ref";
    auto    weird   = tmpFile("weird") + ".xyz";
    {
        std::ofstream   f(weird);
        f << "__time__\n0\n";
    }
    auto    out = tmpFile("out2") + ".rdb";

    EXPECT_THROW(referee::db::ingest(refPath, weird, "", out), std::exception);

    std::remove(weird.c_str());
    std::remove(out.c_str());
}

// ── Member completion ────────────────────────────────────────────────────────
//
// The LSP's other half: after a `.` the server offers the members of the signal
// to its left. These drive the front-end entry the server calls (Referee::
// complete), so they pin the behaviour a user sees as the member popup.
//
// Signal names are interned process-globally (see RejectsBadCalls), so each
// case uses names of its own to avoid a type leaking in from another test.

namespace
{

//  Labels Referee::complete offers at (line, character) — both 0-based (LSP).
std::vector<std::string>    completeLabels(
    std::string const& src, unsigned line, unsigned character, std::string const& tag)
{
    std::istringstream          is(src);
    auto                        cands = Referee::complete(is, "<" + tag + ">", {}, line, character);
    std::vector<std::string>    labels;
    for (auto const& c : cands) labels.push_back(c.label);
    return labels;
}

bool    has(std::vector<std::string> const& v, std::string const& s)
{
    return std::find(v.begin(), v.end(), s) != v.end();
}

} // namespace

// `sig.` on a struct offers its fields.
TEST(Completion, ListsStructFields)
{
    auto    src = std::string(
        "type CompPoint : struct { cx : integer; cy : integer; };\n"
        "data comp_pt : CompPoint;\n"
        "comp_pt.\n");                                  // caret at end, after the dot
    auto    got = completeLabels(src, 2, 8, "structfields");

    EXPECT_EQ(got.size(), 2u);
    EXPECT_TRUE(has(got, "cx")) << "expected field cx";
    EXPECT_TRUE(has(got, "cy")) << "expected field cy";
}

// `sig.` on an enum offers its cases.
TEST(Completion, ListsEnumCases)
{
    auto    src = std::string(
        "type CompEnum : enum { CA, CB, CC };\n"
        "data comp_e : CompEnum;\n"
        "comp_e.\n");
    auto    got = completeLabels(src, 2, 7, "enumcases");

    EXPECT_EQ(got.size(), 3u);
    EXPECT_TRUE(has(got, "CA"));
    EXPECT_TRUE(has(got, "CB"));
    EXPECT_TRUE(has(got, "CC"));
}

// A dotted chain walks through nested struct members.
TEST(Completion, WalksNestedStruct)
{
    auto    src = std::string(
        "type CompInner : struct { na : integer; nb : integer; };\n"
        "type CompOuter : struct { op : CompInner; oq : integer; };\n"
        "data comp_o : CompOuter;\n"
        "comp_o.op.\n");                                // members of the inner struct
    auto    got = completeLabels(src, 3, 10, "nested");

    EXPECT_EQ(got.size(), 2u);
    EXPECT_TRUE(has(got, "na"));
    EXPECT_TRUE(has(got, "nb"));
}

// A scalar has no members, so `.` after one offers nothing.
TEST(Completion, EmptyForScalar)
{
    auto    src = std::string(
        "data comp_i : integer;\n"
        "comp_i.\n");
    EXPECT_TRUE(completeLabels(src, 1, 7, "scalar").empty());
}

// The head must resolve to a signal; an unknown name offers nothing (and does
// not crash the parse, which is the point of blanking the caret's line).
TEST(Completion, EmptyForUnknownSignal)
{
    auto    src = std::string(
        "data comp_z : integer;\n"
        "comp_unknown.\n");
    EXPECT_TRUE(completeLabels(src, 1, 13, "unknown").empty());
}

// Away from a `.`, completion offers the names in scope and the keywords.
TEST(Completion, BareOffersNamesAndKeywords)
{
    auto    src = std::string(
        "type CbPoint : struct { cbx : integer; };\n"
        "data cb_pt : CbPoint;\n"
        "conf cb_lim : number;\n"
        "func cb_f : (integer) -> integer;\n"
        "G(cb);\n");                                 // caret inside the bare word 'cb'
    auto    got = completeLabels(src, 4, 4, "bare");

    EXPECT_TRUE(has(got, "cb_pt"))   << "a data signal";
    EXPECT_TRUE(has(got, "cb_lim"))  << "a conf";
    EXPECT_TRUE(has(got, "CbPoint")) << "a type";
    EXPECT_TRUE(has(got, "cb_f"))    << "a function";
    EXPECT_TRUE(has(got, "G"))       << "a temporal keyword";
    EXPECT_TRUE(has(got, "data"))    << "a declaration keyword";
}

// After a `.`, only the type's members are offered — no keywords or names bleed in.
TEST(Completion, MemberOffersOnlyMembers)
{
    auto    src = std::string(
        "type CmPoint : struct { cmx : integer; cmy : integer; };\n"
        "data cm_pt : CmPoint;\n"
        "G(cm_pt.);\n");                             // caret right after the dot
    auto    got = completeLabels(src, 2, 8, "memberonly");

    EXPECT_EQ(got.size(), 2u);
    EXPECT_TRUE(has(got, "cmx"));
    EXPECT_TRUE(has(got, "cmy"));
    EXPECT_FALSE(has(got, "G"))     << "keywords must not appear after '.'";
    EXPECT_FALSE(has(got, "cm_pt")) << "signal names must not appear after '.'";
}

// ── Hover ────────────────────────────────────────────────────────────────────
//
// Hover shows the declaration of the name under the caret. Same resolution as
// completion, so the same interning caveat: each case uses its own names.

namespace
{

std::string     hoverAt(std::string const& src, unsigned line, unsigned character, std::string const& tag)
{
    std::istringstream  is(src);
    return Referee::hover(is, "<" + tag + ">", {}, line, character);
}

bool    contains(std::string const& hay, std::string const& needle)
{
    return hay.find(needle) != std::string::npos;
}

} // namespace

// A data signal shows `data name : Type`, and a named struct also shows its body.
TEST(Hover, SignalOfNamedStruct)
{
    auto    src = std::string(
        "type HovPoint : struct { hx : integer; hy : integer; };\n"
        "data hov_pt : HovPoint;\n"
        "G(hov_pt.hx > 0);\n");
    auto    got = hoverAt(src, 1, 7, "hovstruct");           // caret on 'hov_pt'

    EXPECT_TRUE(contains(got, "data hov_pt : HovPoint"))            << got;
    EXPECT_TRUE(contains(got, "struct { hx : integer; hy : integer }")) << got;
}

// A computed signal is flagged as such.
TEST(Hover, ComputedSignalIsMarked)
{
    auto    src = std::string(
        "data hov_i : integer;\n"
        "data hov_avg = hov_i + 1;\n"
        "G(hov_avg > 0);\n");
    auto    got = hoverAt(src, 1, 8, "hovcomputed");          // caret on 'hov_avg'

    EXPECT_TRUE(contains(got, "hov_avg : integer")) << got;
    EXPECT_TRUE(contains(got, "computed"))          << got;
}

// A conf shows the `conf` keyword.
TEST(Hover, ConfSignal)
{
    auto    src = std::string(
        "conf hov_lim : number;\n"
        "data hov_x : number;\n"
        "G(hov_x < hov_lim);\n");
    EXPECT_TRUE(contains(hoverAt(src, 0, 7, "hovconf"), "conf hov_lim : number"));
}

// A type name shows its definition.
TEST(Hover, TypeName)
{
    auto    src = std::string(
        "type HovMode : enum { HA, HB };\n"
        "data hov_m : HovMode;\n"
        "G(hov_m.HA);\n");
    EXPECT_TRUE(contains(hoverAt(src, 0, 6, "hovtype"), "type HovMode : enum { HA, HB }"));
}

// The caret on a member shows that field's type.
TEST(Hover, MemberField)
{
    auto    src = std::string(
        "type HovS : struct { fa : integer; fb : integer; };\n"
        "data hov_s : HovS;\n"
        "G(hov_s.fb > 0);\n");
    EXPECT_TRUE(contains(hoverAt(src, 2, 8, "hovmember"), "fb : integer"));
}

// Off any name — on punctuation — there is nothing to show.
TEST(Hover, EmptyOffToken)
{
    auto    src = std::string(
        "data hov_q : integer;\n"
        "G(hov_q > 0);\n");
    // "G(hov_q > 0);" — column 8 is the '>' operator, not part of any name.
    EXPECT_TRUE(hoverAt(src, 1, 8, "hovempty").empty());
}

// ── Go-to-definition ─────────────────────────────────────────────────────────
//
// Jump from a use of a name to its declaration. Click columns are computed with
// find() so the tests do not depend on hand-counted offsets.

namespace
{

std::string     joinLines(std::vector<std::string> const& L)
{
    std::string s;
    for (auto const& l : L) { s += l; s.push_back('\n'); }
    return s;
}

Referee::Definition     defineAt(
    std::vector<std::string> const& L, unsigned line, unsigned col, std::string const& tag)
{
    std::istringstream  is(joinLines(L));
    return Referee::define(is, "<" + tag + ">", {}, line, col);
}

//  The definition was found, on `line`, with its column landing on `word`.
bool    pointsAt(std::vector<std::string> const& L, Referee::Definition const& d,
                 unsigned line, std::string const& word)
{
    return d.found && d.line == line
        && d.startCol < L[line].size()
        && L[line].compare(d.startCol, word.size(), word) == 0;
}

} // namespace

// A signal use jumps to its `data` declaration.
TEST(Definition, DataSignal)
{
    std::vector<std::string>    L = {
        "type DefPoint : struct { dfx : integer; dfy : integer; };",
        "data def_pt : DefPoint;",
        "G(def_pt.dfx > 0);",
    };
    unsigned    col = static_cast<unsigned>(L[2].find("def_pt")) + 1;
    EXPECT_TRUE(pointsAt(L, defineAt(L, 2, col, "gotodata"), 1, "def_pt"));
}

// A type reference jumps to its `type` declaration.
TEST(Definition, TypeReference)
{
    std::vector<std::string>    L = {
        "type DefPoint : struct { dfx : integer; dfy : integer; };",
        "data def_pt : DefPoint;",
        "G(def_pt.dfx > 0);",
    };
    unsigned    col = static_cast<unsigned>(L[1].find("DefPoint")) + 1;
    EXPECT_TRUE(pointsAt(L, defineAt(L, 1, col, "gototype"), 0, "DefPoint"));
}

// A member jumps to its field inside the owning struct.
TEST(Definition, MemberField)
{
    std::vector<std::string>    L = {
        "type DefPoint : struct { dfx : integer; dfy : integer; };",
        "data def_pt : DefPoint;",
        "G(def_pt.dfx > 0);",
    };
    unsigned    col = static_cast<unsigned>(L[2].find("dfx")) + 1;
    EXPECT_TRUE(pointsAt(L, defineAt(L, 2, col, "gotofield"), 0, "dfx"));
}

// A conf use jumps to its `conf` declaration.
TEST(Definition, ConfSignal)
{
    std::vector<std::string>    L = {
        "conf def_lim : number;",
        "data def_x : number;",
        "G(def_x < def_lim);",
    };
    unsigned    col = static_cast<unsigned>(L[2].find("def_lim")) + 1;
    EXPECT_TRUE(pointsAt(L, defineAt(L, 2, col, "gotoconf"), 0, "def_lim"));
}

// A function use jumps to its `func` declaration.
TEST(Definition, Function)
{
    std::vector<std::string>    L = {
        "func def_f : (integer) -> integer;",
        "data def_a : integer;",
        "G(def_f(def_a) == 0);",
    };
    unsigned    col = static_cast<unsigned>(L[2].find("def_f")) + 1;
    EXPECT_TRUE(pointsAt(L, defineAt(L, 2, col, "gotofunc"), 0, "def_f"));
}

// On punctuation there is nothing to resolve.
TEST(Definition, NotFoundOffToken)
{
    std::vector<std::string>    L = { "data def_q : integer;", "G(def_q > 0);" };
    unsigned    col = static_cast<unsigned>(L[1].find('>'));
    EXPECT_FALSE(defineAt(L, 1, col, "gotonone").found);
}

// A name declared in an imported file resolves into that file, following the
// `import` relative to the importing file's directory.
TEST(Definition, CrossFileImport)
{
    char    dir[] = "/tmp/referee-xfile-XXXXXX";
    ASSERT_NE(::mkdtemp(dir), nullptr);
    std::string typesPath = std::string(dir) + "/xtypes.ref";
    std::string mainPath  = std::string(dir) + "/xmain.ref";

    { std::ofstream f(typesPath); f << "type XPoint : struct { xfx : integer; xfy : integer; };\n"; }

    std::string mainSrc =
        "import \"xtypes.ref\";\n"       // line 0
        "data x_pt : XPoint;\n"          // line 1
        "G(x_pt.xfx > 0);\n";            // line 2
    { std::ofstream f(mainPath); f << mainSrc; }

    // The type reference `XPoint` on line 1 resolves into the imported file.
    {
        unsigned            col = static_cast<unsigned>(std::string("data x_pt : XPoint;").find("XPoint")) + 1;
        std::istringstream  is(mainSrc);
        auto                d = Referee::define(is, mainPath, {}, 1, col);
        EXPECT_TRUE(d.found);
        EXPECT_NE(d.file.find("xtypes.ref"), std::string::npos) << d.file;
        EXPECT_EQ(d.line, 0u);
    }
    // The member `xfx` (owned by the imported struct) resolves to the field there.
    {
        unsigned            col = static_cast<unsigned>(std::string("G(x_pt.xfx > 0);").find("xfx")) + 1;
        std::istringstream  is(mainSrc);
        auto                d = Referee::define(is, mainPath, {}, 2, col);
        EXPECT_TRUE(d.found);
        EXPECT_NE(d.file.find("xtypes.ref"), std::string::npos) << d.file;
    }
    // A locally-declared name stays in the current file (empty `file`).
    {
        unsigned            col = static_cast<unsigned>(std::string("data x_pt : XPoint;").find("x_pt")) + 1;
        std::istringstream  is(mainSrc);
        auto                d = Referee::define(is, mainPath, {}, 1, col);
        EXPECT_TRUE(d.found);
        EXPECT_NE(d.file.find("xmain.ref"), std::string::npos) << d.file;
        EXPECT_EQ(d.line, 1u);
    }

    std::remove(typesPath.c_str());
    std::remove(mainPath.c_str());
    ::rmdir(dir);
}

// ── Document symbols (outline) ───────────────────────────────────────────────

namespace
{

std::string const   symFixture =
    "type SymPoint : struct { spx : integer; spy : integer; };\n"
    "type SymMode : enum { SA, SB };\n"
    "data sym_pt : SymPoint;\n"
    "data sym_avg = sym_pt.spx + 1;\n"
    "conf sym_lim : number;\n"
    "func sym_area : (SymPoint) -> number;\n"
    "G(sym_pt.spx < sym_lim);\n";

std::vector<Referee::Symbol>    symbolsOf(std::string const& src, std::string const& tag)
{
    std::istringstream  is(src);
    return Referee::symbols(is, "<" + tag + ">", {});
}

Referee::Symbol const*  bySym(std::vector<Referee::Symbol> const& v, std::string const& n)
{
    for (auto const& s : v) if (s.name == n) return &s;
    return nullptr;
}

} // namespace

// Every top-level declaration is listed, in source order, with its LSP kind.
TEST(Symbols, ListsDeclarationsWithKinds)
{
    auto    syms = symbolsOf(symFixture, "symkinds");
    ASSERT_EQ(syms.size(), 6u);
    EXPECT_EQ(syms[0].name, "SymPoint"); EXPECT_EQ(syms[0].kind, 23);   // Struct
    EXPECT_EQ(syms[1].name, "SymMode");  EXPECT_EQ(syms[1].kind, 10);   // Enum
    EXPECT_EQ(syms[2].name, "sym_pt");   EXPECT_EQ(syms[2].kind, 13);   // Variable
    EXPECT_EQ(syms[3].name, "sym_avg");  EXPECT_EQ(syms[3].kind, 13);
    EXPECT_EQ(syms[4].name, "sym_lim");  EXPECT_EQ(syms[4].kind, 14);   // Constant
    EXPECT_EQ(syms[5].name, "sym_area"); EXPECT_EQ(syms[5].kind, 12);   // Function
}

// A struct's fields nest under it.
TEST(Symbols, StructFieldsAsChildren)
{
    auto        syms = symbolsOf(symFixture, "symstruct");
    auto const* p    = bySym(syms, "SymPoint");
    ASSERT_TRUE(p);
    ASSERT_EQ(p->children.size(), 2u);
    EXPECT_EQ(p->children[0].name, "spx"); EXPECT_EQ(p->children[0].kind, 8);   // Field
    EXPECT_EQ(p->children[1].name, "spy");
}

// An enum's cases nest under it.
TEST(Symbols, EnumCasesAsChildren)
{
    auto        syms = symbolsOf(symFixture, "symenum");
    auto const* m    = bySym(syms, "SymMode");
    ASSERT_TRUE(m);
    ASSERT_EQ(m->children.size(), 2u);
    EXPECT_EQ(m->children[0].name, "SA"); EXPECT_EQ(m->children[0].kind, 22);   // EnumMember
    EXPECT_EQ(m->children[1].name, "SB");
}

// Signals carry their type as detail; a computed one is marked.
TEST(Symbols, DetailShowsTypeAndComputed)
{
    auto        syms = symbolsOf(symFixture, "symdetail");
    auto const* pt   = bySym(syms, "sym_pt");
    auto const* avg  = bySym(syms, "sym_avg");
    ASSERT_TRUE(pt);
    ASSERT_TRUE(avg);
    EXPECT_EQ(pt->detail, "SymPoint");
    EXPECT_TRUE(contains(avg->detail, "computed")) << avg->detail;
}

// ── Find references ──────────────────────────────────────────────────────────

// The declaration and every use are found; a `#` comment mention is not.
TEST(References, IncludesUsesAndDeclarationButNotComments)
{
    std::vector<std::string>    L = {
        "data ref_pt : integer;",           // 0: declaration
        "# ref_pt in a comment",            // 1: comment — must be skipped
        "G(ref_pt > 0 && ref_pt < 9);",     // 2: two uses
    };
    unsigned            col = static_cast<unsigned>(L[0].find("ref_pt")) + 1;
    std::istringstream  is(joinLines(L));
    auto                r = Referee::references(is, "<refs1>", {}, 0, col, /*includeDeclaration*/ true);

    EXPECT_EQ(r.size(), 3u);                                     // decl + 2 uses
    for (auto const& x : r) EXPECT_NE(x.line, 1u) << "comment line must be skipped";
}

// includeDeclaration=false drops the declaration occurrence.
TEST(References, ExcludesDeclarationWhenNotRequested)
{
    std::vector<std::string>    L = {
        "data ref_q : integer;",
        "G(ref_q > 0 && ref_q < 9);",
    };
    unsigned            col = static_cast<unsigned>(L[0].find("ref_q")) + 1;
    std::istringstream  is(joinLines(L));
    auto                r = Referee::references(is, "<refs2>", {}, 0, col, /*includeDeclaration*/ false);

    EXPECT_EQ(r.size(), 2u);                                     // both uses, no decl
    for (auto const& x : r) EXPECT_EQ(x.line, 1u);
}

// References span imported files: a type used here, declared there.
TEST(References, CrossFileImport)
{
    char    dir[] = "/tmp/referee-refs-XXXXXX";
    ASSERT_NE(::mkdtemp(dir), nullptr);
    std::string typesPath = std::string(dir) + "/rtypes.ref";
    std::string mainPath  = std::string(dir) + "/rmain.ref";

    { std::ofstream f(typesPath); f << "type RPoint : struct { rx : integer; };\n"; }
    std::string mainSrc =
        "import \"rtypes.ref\";\n"
        "data r_pt : RPoint;\n"
        "G(r_pt.rx > 0);\n";
    { std::ofstream f(mainPath); f << mainSrc; }

    unsigned            col = static_cast<unsigned>(std::string("data r_pt : RPoint;").find("RPoint")) + 1;
    std::istringstream  is(mainSrc);
    auto                r = Referee::references(is, mainPath, {}, 1, col, /*includeDeclaration*/ true);

    ASSERT_EQ(r.size(), 2u);                                     // use in main + decl in types
    bool inTypes = false, inMain = false;
    for (auto const& x : r)
    {
        if (x.file.find("rtypes.ref") != std::string::npos) inTypes = true;
        if (x.file.find("rmain.ref")  != std::string::npos) inMain  = true;
    }
    EXPECT_TRUE(inTypes);
    EXPECT_TRUE(inMain);

    std::remove(typesPath.c_str());
    std::remove(mainPath.c_str());
    ::rmdir(dir);
}
