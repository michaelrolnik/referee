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

//  Tests for the visitors that neither the logic harness nor the RDB tests
//  reach: Printer (AST -> text) and CsvHeaders (type -> flat column names).

#include <gtest/gtest.h>

#include "referee.hpp"
#include "module.hpp"
#include "syntax.hpp"
#include "factory.hpp"
#include "visitors/printer.hpp"
#include "visitors/csvHeaders.hpp"
#include "visitors/canonic.hpp"
#include "visitors/negated.hpp"
#include "visitors/rewrite.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace
{

std::string     print(Base* node)
{
    std::ostringstream  os;
    Printer::output(os, node);
    return os.str();
}

//  Parse a snippet into a Module. Each call gets a distinct name so the
//  Factory-cached Module is not shared between tests.
Referee::Schema parse(std::string const& src, std::string const& tag)
{
    std::istringstream  is(src);
    return Referee::parseSchema(is, "<" + tag + ">");
}

} // namespace

// ── Printer ──────────────────────────────────────────────────────────────────

// Every expression in a file that exercises the whole operator set must print
// as something non-empty, and must not fall through to the `Expr*` catch-all
// that prints "???" -- that would mean an operator the printer does not know.
TEST(Printer, PrintsEveryOperatorInPassRef)
{
    auto            path = std::string(REFEREE_TEST_DATA_DIR) + "/pass.ref";
    std::ifstream   in(path);
    ASSERT_TRUE(in.is_open());

    auto            schema = Referee::parseSchema(in, path);
    auto*           mod    = schema.ast;
    ASSERT_NE(mod, nullptr);

    ASSERT_FALSE(mod->getExprs().empty());

    for (auto* expr : mod->getExprs())
    {
        auto    text = print(expr);
        EXPECT_FALSE(text.empty());
        EXPECT_EQ(text.find("???"), std::string::npos)
            << "printer has no case for a node in: " << text;
    }
}

// Specification patterns print too, and go through a different visitor family.
TEST(Printer, PrintsSpecificationPatterns)
{
    auto    schema = parse(
        "data a : boolean;\n"
        "data b : boolean;\n"
        "globally, it is always the case that a holds;\n"
        "before b, a eventually holds;\n",
        "specs");

    auto*   mod = schema.ast;
    ASSERT_NE(mod, nullptr);
    ASSERT_FALSE(mod->getSpecs().empty());

    for (auto* spec : mod->getSpecs())
        EXPECT_FALSE(print(spec).empty());
}

// The operators the printer renders through multichar() should come back as
// their source spelling rather than as a number.
TEST(Printer, RendersOperatorSpellings)
{
    auto    schema = parse(
        "data a : boolean;\n"
        "data b : boolean;\n"
        "G(a);\n"
        "F(b);\n"
        "Us(a, b);\n"
        "Xs(a);\n"
        "a && b;\n"
        "a || b;\n"
        "!a;\n"
        "a => b;\n",
        "ops");

    std::string all;
    for (auto* expr : schema.ast->getExprs())
        all += print(expr) + "\n";

    for (auto const* op : {"G", "F", "Us", "Xs", "&&", "||", "!"})
        EXPECT_NE(all.find(op), std::string::npos) << op << " missing from:\n" << all;
}

// Bounded operators print their time window; unbounded ones do not.
TEST(Printer, PrintsTimeBounds)
{
    auto    schema = parse(
        "data a : boolean;\n"
        "data b : boolean;\n"
        "G[0:1000](a);\n"
        "Us[100:200](a, b);\n"
        "F[500:](a);\n"
        "G[:900](b);\n",
        "bounds");

    std::string all;
    for (auto* expr : schema.ast->getExprs())
        all += print(expr) + "\n";

    for (auto const* frag : {"1000", "100", "200", "500", "900"})
        EXPECT_NE(all.find(frag), std::string::npos) << frag << " missing from:\n" << all;
}

// Non-boolean leaves: literals, member access, indexing, conf and enum refs.
TEST(Printer, PrintsValueExpressions)
{
    auto    schema = parse(
        "type Point : struct { x : number; y : number; };\n"
        "type State : enum { ON, OFF };\n"
        "data pos   : Point;\n"
        "data lock  : State;\n"
        "data arr   : integer[3];\n"
        "conf limit : integer;\n"
        "pos.x > 1.5;\n"
        "arr[2] == limit;\n"
        "lock.ON;\n"
        "\"abc\" == \"abc\";\n"
        "1 + 2 * 3 - 4 / 5 % 6 > 0;\n"
        "true != false;\n",
        "values");

    std::string all;
    for (auto* expr : schema.ast->getExprs())
        all += print(expr) + "\n";

    EXPECT_EQ(all.find("???"), std::string::npos) << all;
    for (auto const* frag : {"pos", "arr", "lock", "limit", "abc"})
        EXPECT_NE(all.find(frag), std::string::npos) << frag << " missing from:\n" << all;
}

// Printer also renders the rewritten / canonicalised forms, which are what the
// compiler actually lowers -- those go through the same visitor.
TEST(Printer, PrintsCanonicAndRewrittenForms)
{
    auto    schema = parse(
        "data a : boolean;\n"
        "data b : boolean;\n"
        "G(a => F(b));\n",
        "canonic");

    ASSERT_FALSE(schema.ast->getExprs().empty());
    auto*   expr = schema.ast->getExprs().front();

    auto    canonicText = print(Canonic::make(expr));
    auto    negatedText = print(Negated::make(expr));

    EXPECT_FALSE(canonicText.empty());
    EXPECT_FALSE(negatedText.empty());
    EXPECT_EQ(canonicText.find("???"), std::string::npos) << canonicText;
    EXPECT_EQ(negatedText.find("???"), std::string::npos) << negatedText;
}

// specs.ref carries every specification pattern under every scope, so this
// reaches the Spec visit() cases that a hand-written snippet would miss.
TEST(Printer, PrintsEverySpecificationPattern)
{
    auto            path = std::string(REFEREE_TEST_DATA_DIR) + "/specs.ref";
    std::ifstream   in(path);
    ASSERT_TRUE(in.is_open());

    auto            schema = Referee::parseSchema(in, path);
    auto*           mod    = schema.ast;
    ASSERT_NE(mod, nullptr);

    // 16 pattern bodies + 5 further scopes + 7 time-bound shapes + 1 nesting.
    ASSERT_GE(mod->getSpecs().size(), 29u);

    for (auto* spec : mod->getSpecs())
    {
        auto    text = print(spec);
        EXPECT_FALSE(text.empty());
        EXPECT_EQ(text.find("???"), std::string::npos)
            << "printer has no case for: " << text;
    }
}

// Rewrite lowers a pattern to the temporal formula the compiler emits; that
// form has to print as well, since it is what debugging actually looks at.
TEST(Printer, PrintsRewrittenSpecifications)
{
    auto            path = std::string(REFEREE_TEST_DATA_DIR) + "/specs.ref";
    std::ifstream   in(path);
    ASSERT_TRUE(in.is_open());

    auto            schema = Referee::parseSchema(in, path);

    for (auto* spec : schema.ast->getSpecs())
    {
        auto    text = print(Rewrite::make(spec));
        EXPECT_FALSE(text.empty());
    }
}

// Note: Printer covers expressions and specification patterns only -- it has no
// visit() cases for Type nodes, so types are deliberately not exercised here.

// ── Module diagnostics ───────────────────────────────────────────────────────

// Redeclaring a name is rejected, and the message names it. This matters more
// since `import` arrived: two definition files that both declare a signal is
// now an easy mistake to make.
TEST(Module, RejectsDuplicateDeclarations)
{
    struct Case { char const* src; char const* kind; };

    Case    cases[] = {
        {"type T : boolean;\ntype T : integer;\n",  "type"},
        {"data a : boolean;\ndata a : integer;\n",  "data"},
        {"conf c : boolean;\nconf c : integer;\n",  "conf"},
        {"data a : boolean;\ndata a = a;\n",        "data"},
    };

    int     i = 0;
    for (auto const& c : cases)
    {
        try
        {
            parse(c.src, "dup" + std::to_string(i++));
            ADD_FAILURE() << "expected a duplicate " << c.kind << " to be rejected";
        }
        catch (std::exception const& e)
        {
            std::string msg = e.what();
            EXPECT_NE(msg.find("duplicate"), std::string::npos) << msg;
            EXPECT_NE(msg.find(c.kind),      std::string::npos) << msg;
        }
    }
}

// A syntax error must abort the parse rather than yield a partial tree: before
// this was enforced, a malformed .ref still produced a module and exited 0.
TEST(Module, RejectsSyntaxErrors)
{
    for (auto const* bad : {"data a : boolean;\nG(a;\n",
                            "data a : ;\n",
                            "@@@;\n"})
    {
        EXPECT_THROW(parse(bad, "syntax"), std::exception) << bad;
    }
}

// ── CsvHeaders ───────────────────────────────────────────────────────────────

// A primitive is one column, named after the signal itself.
TEST(CsvHeaders, PrimitiveIsSingleColumn)
{
    TypeBoolean tBool;
    auto        cols = CsvHeaders::make("flag", &tBool);

    ASSERT_EQ(cols.size(), 1u);
    EXPECT_EQ(cols[0], "flag");
}

// Struct fields flatten to dotted names, nesting included.
TEST(CsvHeaders, StructFlattensToDottedNames)
{
    auto    schema = parse(
        "data pos : struct { x : number; y : number; };\n"
        "data deep : struct { inner : struct { a : integer; b : integer; }; };\n",
        "csv-struct");

    auto*   mod = schema.ast;

    auto    pos = CsvHeaders::make("pos", mod->getProp("pos"));
    ASSERT_EQ(pos.size(), 2u);
    EXPECT_EQ(pos[0], "pos.x");
    EXPECT_EQ(pos[1], "pos.y");

    auto    deep = CsvHeaders::make("deep", mod->getProp("deep"));
    ASSERT_EQ(deep.size(), 2u);
    EXPECT_EQ(deep[0], "deep.inner.a");
    EXPECT_EQ(deep[1], "deep.inner.b");
}

// Arrays expand to one column per element, and stack for multiple dimensions.
TEST(CsvHeaders, ArraysExpandPerElement)
{
    auto    schema = parse(
        "data limits : integer[3];\n"
        "data grid   : integer[2][2];\n",
        "csv-array");

    auto*   mod = schema.ast;

    auto    limits = CsvHeaders::make("limits", mod->getProp("limits"));
    ASSERT_EQ(limits.size(), 3u);
    EXPECT_EQ(limits[0], "limits[0]");
    EXPECT_EQ(limits[2], "limits[2]");

    auto    grid = CsvHeaders::make("grid", mod->getProp("grid"));
    EXPECT_EQ(grid.size(), 4u);
}

// Structs of arrays of structs -- the combination, since each visitor recurses
// through CsvHeaders::make rather than through itself.
TEST(CsvHeaders, NestedStructArrayCombination)
{
    auto    schema = parse(
        "type Point : struct { x : number; y : number; };\n"
        "data path  : struct { pts : Point[2]; tag : string; };\n",
        "csv-mixed");

    auto    cols = CsvHeaders::make("path", schema.ast->getProp("path"));

    ASSERT_EQ(cols.size(), 5u);
    EXPECT_EQ(cols[0], "path.pts[0].x");
    EXPECT_EQ(cols[1], "path.pts[0].y");
    EXPECT_EQ(cols[2], "path.pts[1].x");
    EXPECT_EQ(cols[3], "path.pts[1].y");
    EXPECT_EQ(cols[4], "path.tag");
}

// An enum is a single column: the member name is the cell value, not a column.
TEST(CsvHeaders, EnumIsSingleColumn)
{
    auto    schema = parse(
        "type State : enum { ON, OFF };\n"
        "data lock  : State;\n",
        "csv-enum");

    auto    cols = CsvHeaders::make("lock", schema.ast->getProp("lock"));
    ASSERT_EQ(cols.size(), 1u);
    EXPECT_EQ(cols[0], "lock");
}

// The headers derived from a .ref must be exactly the columns its trace file
// carries -- that agreement is the whole contract of this visitor.
TEST(CsvHeaders, MatchesShippedTraceColumns)
{
    auto            refPath = std::string(REFEREE_TEST_DATA_DIR) + "/pass.ref";
    std::ifstream   refIn(refPath);
    ASSERT_TRUE(refIn.is_open());

    auto            schema = Referee::parseSchema(refIn, refPath);

    std::vector<std::string>    expected;
    for (auto const& name : schema.ast->getPropNames())
        for (auto const& col : CsvHeaders::make(name, schema.ast->getProp(name)))
            expected.push_back(col);

    std::ifstream   csvIn(std::string(REFEREE_TEST_DATA_DIR) + "/data.csv");
    ASSERT_TRUE(csvIn.is_open());
    std::string     header;
    ASSERT_TRUE(std::getline(csvIn, header));

    std::vector<std::string>    actual;
    std::stringstream           hs(header);
    std::string                 cell;
    while (std::getline(hs, cell, ','))
        if (cell != "__time__")
            actual.push_back(cell);

    EXPECT_EQ(expected, actual);
}
