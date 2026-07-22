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
