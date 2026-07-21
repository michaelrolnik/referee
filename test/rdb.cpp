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

#include <gtest/gtest.h>

#include "rdb/database.hpp"
#include "rdb/ingest.hpp"
#include "referee.hpp"
#include "strings.hpp"
#include <llvm/Support/TargetSelect.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include "antlr2ast.hpp"
#include "syntax.hpp"
#include "visitors/loader.hpp"
#include "visitors/csvHeaders.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace
{

std::string     tmpFile(std::string const& tag)
{
    std::string         pat = "/tmp/referee-rdb-" + tag + "-XXXXXX";
    std::vector<char>   buf(pat.begin(), pat.end());
    buf.push_back('\0');
    int     fd  = ::mkstemp(buf.data());
    EXPECT_GE(fd, 0);
    if (fd >= 0) ::close(fd);
    return std::string(buf.data());
}

std::vector<std::uint8_t>   readWholeFile(std::string const& path)
{
    std::ifstream   in(path, std::ios::binary);
    EXPECT_TRUE(in.good());
    std::vector<std::uint8_t>   v((std::istreambuf_iterator<char>(in)), {});
    return v;
}

} // namespace

// Phase 1 round-trip — handcraft a schema + blobs, write, read, check.
TEST(Rdb, RoundTripPrimitives)
{
    TypeBoolean tBool;
    TypeInteger tInt;
    TypeNumber  tNum;
    TypeString  tStr;
    TypeEnum    tEnum({"A", "B", "C"});

    std::vector<referee::db::PropDecl>  props = {
        {"b", &tBool},
        {"i", &tInt},
        {"n", &tNum},
        {"s", &tStr},
        {"e", &tEnum},
    };

    auto    intern = [](std::string const& s) {
        return Strings::instance()->getString(s);
    };

    auto    makeBoolBlob = [](bool v) {
        std::vector<std::uint8_t>   b(1);
        b[0] = v ? 1 : 0;
        return b;
    };
    auto    makeIntBlob = [](std::int64_t v) {
        std::vector<std::uint8_t>   b(8);
        std::memcpy(b.data(), &v, sizeof(v));
        return b;
    };
    auto    makeNumBlob = [](double v) {
        std::vector<std::uint8_t>   b(8);
        std::memcpy(b.data(), &v, sizeof(v));
        return b;
    };
    auto    makeStrBlob = [&](std::string const& v) {
        std::vector<std::uint8_t>   b(8);
        char const* p = intern(v);
        std::memcpy(b.data(), &p, sizeof(p));
        return b;
    };
    auto    makeEnumBlob = [](std::uint8_t idx) {
        std::vector<std::uint8_t>   b(1);
        b[0] = idx;
        return b;
    };

    auto    path = tmpFile("primitives");
    {
        std::ofstream   os(path, std::ios::binary);
        referee::db::Writer w(os);
        w.setSchema(props, /*confs*/ {});
        w.setNumStates(3);
        w.setConfBlob({});

        w.writeState(0, 0, {makeBoolBlob(false), makeIntBlob(0),    makeNumBlob(0.0), makeStrBlob(""),       makeEnumBlob(0)});
        w.writeState(1, 1, {makeBoolBlob(true),  makeIntBlob(42),   makeNumBlob(3.5), makeStrBlob("hello"),  makeEnumBlob(2)});
        w.writeState(2, 2, {makeBoolBlob(false), makeIntBlob(7),    makeNumBlob(2.5), makeStrBlob("world"),  makeEnumBlob(3)});
        w.finish();
    }

    referee::db::Reader r(path);
    EXPECT_EQ(r.numStates(), 3u);
    EXPECT_EQ(r.numProps(),  5u);
    EXPECT_EQ(r.props().size(), 5u);
    EXPECT_EQ(r.props()[0].name, "b");
    EXPECT_EQ(r.props()[3].name, "s");

    // State 1: pull each prop blob and decode.
    {
        auto*   bb = static_cast<std::uint8_t const*>(r.propBlob(1, 0));
        EXPECT_EQ(*bb, 1);
        std::int64_t    iv;
        std::memcpy(&iv, r.propBlob(1, 1), sizeof(iv));
        EXPECT_EQ(iv, 42);
        double          nv;
        std::memcpy(&nv, r.propBlob(1, 2), sizeof(nv));
        EXPECT_DOUBLE_EQ(nv, 3.5);
        char const*     sv;
        std::memcpy(&sv, r.propBlob(1, 3), sizeof(sv));
        ASSERT_NE(sv, nullptr);
        EXPECT_STREQ(sv, "hello");
        auto*           ev = static_cast<std::uint8_t const*>(r.propBlob(1, 4));
        EXPECT_EQ(*ev, 2);
    }

    // ptrFirst / ptrLast match expected row addresses.
    EXPECT_EQ(static_cast<std::uint8_t*>(r.ptrFirst()) +
                  (r.numStates() - 1) * r.rowBytes(),
              static_cast<std::uint8_t*>(r.ptrLast()));

    std::remove(path.c_str());
}

// Phase 3 — feed a `.rdb` built from the same fixtures as LogicTest into the
// JIT and confirm pass.ref passes / fail.ref fails.
TEST(Rdb, ExecuteRdbPass)
{
    auto    refPath  = std::string(REFEREE_TEST_DATA_DIR) + "/pass.ref";
    auto    csvPath  = std::string(REFEREE_TEST_DATA_DIR) + "/data.csv";
    auto    confPath = std::string(REFEREE_TEST_DATA_DIR) + "/conf.csv";
    auto    rdbPath  = tmpFile("pass");

    referee::db::ingest(refPath, csvPath, confPath, rdbPath);

    std::ifstream       refIn(refPath);
    std::ostringstream  out;
    bool                allPass = Referee::executeRdb(refIn, refPath, rdbPath, out);
    EXPECT_TRUE(allPass) << out.str();
    std::remove(rdbPath.c_str());
}

TEST(Rdb, ExecuteRdbFail)
{
    auto    refPath  = std::string(REFEREE_TEST_DATA_DIR) + "/fail.ref";
    auto    csvPath  = std::string(REFEREE_TEST_DATA_DIR) + "/data.csv";
    auto    confPath = std::string(REFEREE_TEST_DATA_DIR) + "/conf.csv";
    auto    rdbPath  = tmpFile("fail");

    referee::db::ingest(refPath, csvPath, confPath, rdbPath);

    std::ifstream       refIn(refPath);
    std::ostringstream  out;
    bool                allPass = Referee::executeRdb(refIn, refPath, rdbPath, out);
    EXPECT_FALSE(allPass);
    std::remove(rdbPath.c_str());
}

// Phase 4 — CSV-derived and YAML-derived .rdb files must be byte-identical.
// Both pipelines walk props in the same declaration order, so string-pool
// insertion order is identical and the output bytes match exactly.
TEST(Rdb, CsvAndYamlAgree)
{
    auto    refPath   = std::string(REFEREE_TEST_DATA_DIR) + "/pass.ref";
    auto    csvData   = std::string(REFEREE_TEST_DATA_DIR) + "/data.csv";
    auto    csvConf   = std::string(REFEREE_TEST_DATA_DIR) + "/conf.csv";
    auto    ymlData   = std::string(REFEREE_TEST_DATA_DIR) + "/data.yaml";
    auto    ymlConf   = std::string(REFEREE_TEST_DATA_DIR) + "/conf.yaml";

    auto    rdbCsv = tmpFile("from-csv");
    auto    rdbYml = tmpFile("from-yml");

    referee::db::ingest(refPath, csvData, csvConf, rdbCsv);
    referee::db::ingest(refPath, ymlData, ymlConf, rdbYml);

    auto    a = readWholeFile(rdbCsv);
    auto    b = readWholeFile(rdbYml);
    EXPECT_EQ(a, b);

    std::remove(rdbCsv.c_str());
    std::remove(rdbYml.c_str());
}

// Phase 5 — dump should produce the schema, conf, and per-state rows.
TEST(Rdb, DumpHasSchemaAndStates)
{
    auto    refPath = std::string(REFEREE_TEST_DATA_DIR) + "/pass.ref";
    auto    csvPath = std::string(REFEREE_TEST_DATA_DIR) + "/data.csv";
    auto    cnfPath = std::string(REFEREE_TEST_DATA_DIR) + "/conf.csv";
    auto    rdbPath = tmpFile("dump");

    referee::db::ingest(refPath, csvPath, cnfPath, rdbPath);

    std::ostringstream  out;
    referee::db::dump(rdbPath, out);
    auto                s = out.str();
    EXPECT_NE(s.find("schema:"), std::string::npos);
    EXPECT_NE(s.find("conf:"),   std::string::npos);
    EXPECT_NE(s.find("states:"), std::string::npos);
    std::remove(rdbPath.c_str());
}

// Phase 6 — dump schema section lists data and conf declarations by name.
TEST(Rdb, DumpSchemaContentsDataAndConf)
{
    auto    refPath = std::string(REFEREE_TEST_DATA_DIR) + "/pass.ref";
    auto    csvPath = std::string(REFEREE_TEST_DATA_DIR) + "/data.csv";
    auto    cnfPath = std::string(REFEREE_TEST_DATA_DIR) + "/conf.csv";
    auto    rdbPath = tmpFile("dump-schema");

    referee::db::ingest(refPath, csvPath, cnfPath, rdbPath);

    std::ostringstream  out;
    referee::db::dump(rdbPath, out);
    auto s = out.str();

    EXPECT_NE(s.find("  data:\n"), std::string::npos) << "schema.data block missing";
    EXPECT_NE(s.find("  conf:"),   std::string::npos) << "schema.conf block missing";
    EXPECT_NE(s.find("- name: "),  std::string::npos) << "no named entries found";

    std::remove(rdbPath.c_str());
}

// Phase 7 — YAML trace produces same execute result as CSV trace.
TEST(Rdb, ExecuteRdbPassYaml)
{
    auto    refPath  = std::string(REFEREE_TEST_DATA_DIR) + "/pass.ref";
    auto    ymlData  = std::string(REFEREE_TEST_DATA_DIR) + "/data.yaml";
    auto    ymlConf  = std::string(REFEREE_TEST_DATA_DIR) + "/conf.yaml";
    auto    rdbPath  = tmpFile("pass-yaml");

    referee::db::ingest(refPath, ymlData, ymlConf, rdbPath);

    std::ifstream       refIn(refPath);
    std::ostringstream  out;
    bool                allPass = Referee::executeRdb(refIn, refPath, rdbPath, out);
    EXPECT_TRUE(allPass) << out.str();
    std::remove(rdbPath.c_str());
}

// Phase 8 — single data row: sentinels bracket it, total numStates == 3.
TEST(Rdb, SingleStateHasThreeRows)
{
    TypeBoolean tBool;
    std::vector<referee::db::PropDecl> props = {{"flag", &tBool}};

    auto makeBoolBlob = [](bool v) {
        return std::vector<std::uint8_t>{v ? std::uint8_t{1} : std::uint8_t{0}};
    };

    auto path = tmpFile("single");
    {
        std::ofstream os(path, std::ios::binary);
        referee::db::Writer w(os);
        w.setSchema(props, {});
        w.setNumStates(3);
        w.setConfBlob({});
        w.writeState(0, -1,  {makeBoolBlob(false)});
        w.writeState(1, 42,  {makeBoolBlob(true)});
        w.writeState(2, 43,  {makeBoolBlob(false)});
        w.finish();
    }

    referee::db::Reader r(path);
    EXPECT_EQ(r.numStates(), 3u);
    EXPECT_EQ(r.time(0), -1);
    EXPECT_EQ(r.time(1), 42);
    EXPECT_EQ(r.time(2), 43);

    auto* b = static_cast<std::uint8_t const*>(r.propBlob(1, 0));
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(*b, 1);

    std::remove(path.c_str());
}

// Phase 9 — Loader throws for dynamic (count=0) array fields.
TEST(Rdb, LoaderThrowsOnDynamicArray)
{
    TypeArray dynArr(nullptr, 0);

    std::vector<std::uint8_t> buf;
    EXPECT_THROW(
        Loader::load(buf, "field", &dynArr,
                     [](std::string const&) { return ""; }),
        std::runtime_error);
}

// Phase 10 — no conf file: conf blob is zero-initialised, reader opens fine.
TEST(Rdb, IngestNoConfFile)
{
    auto    refPath = std::string(REFEREE_TEST_DATA_DIR) + "/pass.ref";
    auto    csvPath = std::string(REFEREE_TEST_DATA_DIR) + "/data.csv";
    auto    rdbPath = tmpFile("no-conf");

    referee::db::ingest(refPath, csvPath, /*confPath=*/"", rdbPath);

    referee::db::Reader r(rdbPath);
    EXPECT_NE(r.confPtr(), nullptr);

    std::remove(rdbPath.c_str());
}

// Phase 11 — data=expr computed props: ingest stores only the CSV-backed props,
// and executing the spec file materialises the computed ones and must pass.
TEST(Rdb, ExprDataIngestAndExecute)
{
    auto    refPath = std::string(REFEREE_TEST_DATA_DIR) + "/expr_data.ref";
    auto    csvPath = std::string(REFEREE_TEST_DATA_DIR) + "/expr_data.csv";
    auto    rdbPath = tmpFile("expr-data");

    referee::db::ingest(refPath, csvPath, /*confPath=*/"", rdbPath);

    std::ifstream       refIn(refPath);
    std::ostringstream  out;
    bool                allPass = Referee::executeRdb(refIn, refPath, rdbPath, out);
    EXPECT_TRUE(allPass) << out.str();

    std::remove(rdbPath.c_str());
}

// Phase 11b — a computed prop may be defined in terms of an earlier computed
// prop, including across states (`data y = Xs(x);`). __prepare__ must therefore
// materialise each prop over the whole trace before evaluating anything that
// depends on it; evaluating all props state-by-state would read x[i+1] before
// it was written.
TEST(Rdb, ExprDataChainedDependencies)
{
    auto    refPath = std::string(REFEREE_TEST_DATA_DIR) + "/expr_chain.ref";
    auto    csvPath = std::string(REFEREE_TEST_DATA_DIR) + "/expr_data.csv";
    auto    rdbPath = tmpFile("expr-chain");

    referee::db::ingest(refPath, csvPath, /*confPath=*/"", rdbPath);

    // Only a and b are CSV-backed; x, y, z, w are computed.
    referee::db::Reader r(rdbPath);
    EXPECT_EQ(r.numProps(), 2u);

    std::ifstream       refIn(refPath);
    std::ostringstream  out;
    bool                allPass = Referee::executeRdb(refIn, refPath, rdbPath, out);
    EXPECT_TRUE(allPass) << out.str();

    std::remove(rdbPath.c_str());
}

// Phase 11c — freeze (`@`) vs. the buffered O(N) temporal lowering. A temporal
// operator nested under a freeze that names the frozen state is not a function
// of the state index and must stay on the nested-scan path; one that merely
// *contains* a freeze is still eligible. expr_freeze.ref pins both directions,
// including a negative that only holds if the inner operator was never
// precomputed.
// `import` folds another .ref into the same program. main.ref reaches its
// definitions file five ways -- directly, through each of two requirement
// files, through a path with a '..' round-trip, and through a symlink -- so
// this only passes if import-once is keyed on the real path. The two imported
// requirements also sit at the same line and column, which collide unless
// requirement labels are qualified by file.
TEST(Rdb, ImportDiamondAndLabels)
{
    auto    refPath = std::string(REFEREE_TEST_DATA_DIR) + "/import/main.ref";
    auto    csvPath = std::string(REFEREE_TEST_DATA_DIR) + "/import/data.csv";
    auto    rdbPath = tmpFile("import");

    referee::db::ingest(refPath, csvPath, /*confPath=*/"", rdbPath);

    // The schema is the union of what the imports declare.
    referee::db::Reader r(rdbPath);
    EXPECT_EQ(r.numProps(), 3u);        // lock, a, b -- each exactly once

    std::ifstream       refIn(refPath);
    std::ostringstream  out;
    bool                allPass = Referee::executeRdb(refIn, refPath, rdbPath, out);
    EXPECT_TRUE(allPass) << out.str();

    auto    report = out.str();
    // Imported requirements are qualified; the root file's own are not.
    EXPECT_NE(report.find("reqs/one.ref:5:0 .. 5:10"), std::string::npos) << report;
    EXPECT_NE(report.find("reqs/two.ref:5:0 .. 5:10"), std::string::npos) << report;
    EXPECT_NE(report.find("13:0 .. 13:21"),            std::string::npos) << report;

    std::remove(rdbPath.c_str());
}

// A cycle is reported as a cycle, rather than silently no-op'ing and then
// failing later on a name the importer could not see yet.
TEST(Rdb, ImportCycleIsReported)
{
    auto    refPath = std::string(REFEREE_TEST_DATA_DIR) + "/import/cycle_a.ref";

    // parseSchema, not executeRdb: the failure has to come from the parse, and
    // executeRdb would open the trace first and fail on that instead.
    std::ifstream       refIn(refPath);
    try
    {
        Referee::parseSchema(refIn, refPath);
        FAIL() << "expected an import cycle to be rejected";
    }
    catch (std::exception const& e)
    {
        EXPECT_NE(std::string(e.what()).find("import cycle"), std::string::npos)
            << e.what();
    }
}

// An unresolvable import names what it looked for and where it looked.
TEST(Rdb, ImportMissingFileIsReported)
{
    auto    refPath = std::string(REFEREE_TEST_DATA_DIR) + "/import/missing.ref";

    std::ifstream       refIn(refPath);
    try
    {
        Referee::parseSchema(refIn, refPath);
        FAIL() << "expected a missing import to be rejected";
    }
    catch (std::exception const& e)
    {
        EXPECT_NE(std::string(e.what()).find("cannot find imported file"), std::string::npos)
            << e.what();
    }
}

// An import unresolvable next to the importing file is found on a search path.
TEST(Rdb, ImportResolvedViaSearchPath)
{
    auto    refPath = std::string(REFEREE_TEST_DATA_DIR) + "/import/via_path.ref";
    auto    csvPath = std::string(REFEREE_TEST_DATA_DIR) + "/import/data.csv";
    auto    searchDir = std::string(REFEREE_TEST_DATA_DIR) + "/import/defs";
    auto    rdbPath = tmpFile("import-path");

    // Without the search path the import cannot be resolved at all.
    EXPECT_THROW(
        referee::db::ingest(refPath, csvPath, "", rdbPath),
        std::exception);

    referee::db::ingest(refPath, csvPath, "", rdbPath, {searchDir});

    std::ifstream       refIn(refPath);
    std::ostringstream  out;
    bool                allPass = Referee::executeRdb(refIn, refPath, rdbPath, out,
                                                      {searchDir});
    EXPECT_TRUE(allPass) << out.str();

    std::remove(rdbPath.c_str());
}

// A requirement written with `@name` is labelled by that name instead of its
// source position, which is what lets a corpus refer to it across edits.
TEST(Rdb, NamedRequirements)
{
    auto    ref  = std::string(REFEREE_TEST_DATA_DIR) + "/suite/spec.ref";
    auto    csv  = std::string(REFEREE_TEST_DATA_DIR) + "/suite/good.csv";

    std::ifstream       in(ref);
    std::ostringstream  out;
    EXPECT_TRUE(Referee::executeAll(in, ref, {{csv, false}}, "", out)) << out.str();

    auto    report = out.str();
    EXPECT_NE(report.find("a_always_holds"), std::string::npos) << report;
    EXPECT_NE(report.find("b-always-holds"), std::string::npos) << report;
    // An unnamed requirement keeps its position.
    EXPECT_NE(report.find(" .. "),           std::string::npos) << report;
}

// A name identifies one requirement across the whole program, imports
// included; two sharing one would collide into a single generated function.
TEST(Rdb, DuplicateRequirementNamesRejected)
{
    std::istringstream  src("data a : boolean;\n@dup\nG(a);\n@dup\nG(!a);\n");
    try
    {
        Referee::parseSchema(src, "<dupname>");
        ADD_FAILURE() << "expected duplicate requirement names to be rejected";
    }
    catch (std::exception const& e)
    {
        EXPECT_NE(std::string(e.what()).find("duplicate requirement name"),
                  std::string::npos) << e.what();
    }
}

// A corpus read from a manifest, each trace saying which requirement it is
// meant to violate.
TEST(Rdb, SuiteManifest)
{
    auto    ref   = std::string(REFEREE_TEST_DATA_DIR) + "/suite/spec.ref";
    auto    good  = std::string(REFEREE_TEST_DATA_DIR) + "/suite/suite.txt";
    auto    wrong = std::string(REFEREE_TEST_DATA_DIR) + "/suite/wrong.txt";

    {
        auto    traces = Referee::readSuite(good);
        ASSERT_EQ(traces.size(), 3u);
        EXPECT_FALSE(traces[0].expectFailure);
        EXPECT_TRUE (traces[1].expectFailure);
        ASSERT_EQ(traces[1].violates.size(), 1u);
        EXPECT_EQ(traces[1].violates[0], "a_always_holds");

        std::ifstream       in(ref);
        std::ostringstream  out;
        EXPECT_TRUE(Referee::executeAll(in, ref, traces, "", out,
                                        Referee::Detail::Traces)) << out.str();
    }

    // The point of naming requirements: a trace that fails for the wrong
    // reason is a failure, where a bare "fails" would have called it fine.
    {
        auto                traces = Referee::readSuite(wrong);
        std::ifstream       in(ref);
        std::ostringstream  out;
        EXPECT_FALSE(Referee::executeAll(in, ref, traces, "", out,
                                         Referee::Detail::Traces)) << out.str();
        EXPECT_NE(out.str().find("WRONG REQUIREMENT"), std::string::npos) << out.str();
        EXPECT_NE(out.str().find("but it held"),       std::string::npos) << out.str();
    }
}

// A specification is compiled once and checked against several traces, each
// declared to pass or to fail. The exit-code contract is that every trace must
// behave as declared -- including the case that earns the feature, where a
// trace expected to be rejected is not, which means the specification has
// stopped catching what that trace demonstrates.
TEST(Rdb, TraceExpectations)
{
    auto    pass    = std::string(REFEREE_TEST_DATA_DIR) + "/pass.ref";
    auto    fail    = std::string(REFEREE_TEST_DATA_DIR) + "/fail.ref";
    auto    csv     = std::string(REFEREE_TEST_DATA_DIR) + "/data.csv";
    auto    yaml    = std::string(REFEREE_TEST_DATA_DIR) + "/data.yaml";
    auto    conf    = std::string(REFEREE_TEST_DATA_DIR) + "/conf.csv";

    auto    run = [&](std::string const& ref,
                      std::vector<Referee::Trace> const& traces,
                      std::string& report)
    {
        std::ifstream       in(ref);
        std::ostringstream  out;
        bool                ok = Referee::executeAll(in, ref, traces, conf, out,
                                                     Referee::Detail::Traces);
        report = out.str();
        return ok;
    };

    std::string report;

    // The four outcomes.
    EXPECT_TRUE (run(pass, {{csv, false}}, report)) << report;   // passes, as declared
    EXPECT_TRUE (run(fail, {{csv, true }}, report)) << report;   // fails,  as declared
    EXPECT_FALSE(run(pass, {{csv, true }}, report)) << report;   // unexpected pass
    EXPECT_FALSE(run(fail, {{csv, false}}, report)) << report;   // unexpected failure

    // An unexpected pass has to be distinguishable from an ordinary failure:
    // it means the specification stopped noticing, not that the system
    // misbehaved.
    run(pass, {{csv, true}}, report);
    EXPECT_NE(report.find("UNEXPECTED PASS"), std::string::npos) << report;

    // A mixed corpus in one invocation: one compile, a verdict per trace, and
    // the run fails because one trace did not behave as declared.
    EXPECT_FALSE(run(pass, {{csv, false}, {yaml, false}, {csv, true}}, report)) << report;
    EXPECT_NE(report.find("3 traces"), std::string::npos) << report;
    EXPECT_NE(report.find("2 ok"),     std::string::npos) << report;
}

// Several traces of different formats in one invocation, all satisfying the
// specification. CSV and YAML carry the same data, so both must agree.
TEST(Rdb, MultipleTracesOneCompile)
{
    auto    ref  = std::string(REFEREE_TEST_DATA_DIR) + "/pass.ref";
    auto    conf = std::string(REFEREE_TEST_DATA_DIR) + "/conf.csv";
    auto    rdb  = tmpFile("multi") + ".rdb";

    referee::db::ingest(ref,
                        std::string(REFEREE_TEST_DATA_DIR) + "/data.csv",
                        conf, rdb);

    std::vector<Referee::Trace> traces = {
        {std::string(REFEREE_TEST_DATA_DIR) + "/data.csv",  false},
        {std::string(REFEREE_TEST_DATA_DIR) + "/data.yaml", false},
        {rdb,                                               false},
    };

    std::ifstream       in(ref);
    std::ostringstream  out;
    EXPECT_TRUE(Referee::executeAll(in, ref, traces, conf, out,
                                    Referee::Detail::Traces)) << out.str();
    EXPECT_NE(out.str().find("3 traces: 3 ok"), std::string::npos) << out.str();

    std::remove(rdb.c_str());
}

// Bounded quantifiers over array elements: all / some / one and the counted
// forms, element-and-index binding, nesting over a 2-D array, composition with
// temporal operators in both orders, and use inside a computed signal.
TEST(Rdb, Quantifiers)
{
    auto    refPath = std::string(REFEREE_TEST_DATA_DIR) + "/quantifiers.ref";
    auto    csvPath = std::string(REFEREE_TEST_DATA_DIR) + "/quantifiers.csv";
    auto    rdbPath = tmpFile("quantifiers");

    referee::db::ingest(refPath, csvPath, /*confPath=*/"", rdbPath);

    std::ifstream       refIn(refPath);
    std::ostringstream  out;
    EXPECT_TRUE(Referee::executeRdb(refIn, refPath, rdbPath, out)) << out.str();

    std::remove(rdbPath.c_str());
}

// Discrete accumulation over records, bounded by a condition: Sum totals a
// value and Cnt counts records, where the existing Itg integrates over time.
// The distinction is what a protocol needs -- "bytes in this message" is a
// count of records, not a duration-weighted quantity.
TEST(Rdb, AccumulateOverRecords)
{
    auto    ref = std::string(REFEREE_TEST_DATA_DIR) + "/accumulate.ref";
    auto    csv = std::string(REFEREE_TEST_DATA_DIR) + "/accumulate.csv";

    std::ifstream       in(ref);
    std::ostringstream  out;
    EXPECT_TRUE(Referee::executeAll(in, ref, {{csv, false}}, "", out)) << out.str();
}

TEST(Rdb, AccumulateDiagnostics)
{
    struct Case { char const* src; char const* want; };

    Case    cases[] = {
        // what selects the states must be boolean
        {"data i : integer;\nSum(i, i) == 0;\n",            "selects states with a condition"},
        // what accumulates must be numeric
        {"data a : boolean;\nSum(a, a) == 0;\n",            "accumulates a number"},
        // Cnt selects too, and takes nothing to accumulate
        {"data i : integer;\nCnt(i) == 0;\n",               "selects states with a condition"},
    };

    int     n = 0;
    for (auto const& c : cases)
    {
        std::istringstream  src(c.src);
        try
        {
            Referee::parseSchema(src, "<acc" + std::to_string(n++) + ">");
            ADD_FAILURE() << "expected a rejection for: " << c.src;
        }
        catch (std::exception const& e)
        {
            EXPECT_NE(std::string(e.what()).find(c.want), std::string::npos) << e.what();
        }
    }
}

// An array declared `T[]` takes its extent from the trace. The same
// specification must therefore hold against traces of different sizes, which
// is the point of leaving the extent out.
TEST(Rdb, LoadSizedArrays)
{
    auto    ref = std::string(REFEREE_TEST_DATA_DIR) + "/loadsized.ref";

    for (auto const* trace : {"/loadsized.csv", "/loadsized_small.csv"})
    {
        auto                csv = std::string(REFEREE_TEST_DATA_DIR) + trace;
        std::ifstream       in(ref);
        std::ostringstream  out;
        EXPECT_TRUE(Referee::executeAll(in, ref, {{csv, false}}, "", out))
            << trace << ": " << out.str();
    }

    // And through a packed .rdb, whose schema records the extent that was
    // fixed when it was built.
    auto    rdb = tmpFile("loadsized") + ".rdb";
    referee::db::ingest(ref, std::string(REFEREE_TEST_DATA_DIR) + "/loadsized.csv",
                        "", rdb);
    {
        std::ifstream       in(ref);
        std::ostringstream  out;
        EXPECT_TRUE(Referee::executeAll(in, ref, {{rdb, false}}, "", out)) << out.str();
    }
    std::remove(rdb.c_str());
}

// Compiling an unsized specification with nothing to take the extent from is
// an error that says so, rather than a crash or a silent zero.
TEST(Rdb, UnsizedArrayNeedsATrace)
{
    std::istringstream  src("data v : integer[];\nv.count > 0;\n");
    try
    {
        Referee::parseSchema(src, "<unsized>");
        ADD_FAILURE() << "expected an unsized array with no trace to be rejected";
    }
    catch (std::exception const& e)
    {
        EXPECT_NE(std::string(e.what()).find("could not be taken from the trace"),
                  std::string::npos) << e.what();
    }
}

// A corpus is compiled once, against the extents of its first trace, so traces
// that disagree are reported rather than read into the wrong shape.
TEST(Rdb, CorpusMustAgreeOnExtents)
{
    auto    ref   = std::string(REFEREE_TEST_DATA_DIR) + "/loadsized.ref";
    auto    big   = std::string(REFEREE_TEST_DATA_DIR) + "/loadsized.csv";
    auto    small = std::string(REFEREE_TEST_DATA_DIR) + "/loadsized_small.csv";

    std::ifstream       in(ref);
    std::ostringstream  out;
    EXPECT_THROW(
        Referee::executeAll(in, ref, {{big, false}, {small, false}}, "", out),
        std::exception);
}

// `xs.count` is the element count, and an array has no other member.
TEST(Rdb, ArrayCountDiagnostics)
{
    struct Case { char const* src; char const* want; };

    Case    cases[] = {
        {"data v : integer[4];\nv.size == 4;\n",   "did you mean 'count'"},
        {"data v : integer[4];\nv.length == 4;\n", "no member 'length'"},
    };

    int     n = 0;
    for (auto const& c : cases)
    {
        std::istringstream  src(c.src);
        try
        {
            Referee::parseSchema(src, "<count" + std::to_string(n++) + ">");
            ADD_FAILURE() << "expected a rejection for: " << c.src;
        }
        catch (std::exception const& e)
        {
            EXPECT_NE(std::string(e.what()).find(c.want), std::string::npos) << e.what();
        }
    }

    // A struct field called `count` is unaffected -- the member is resolved by
    // the type of what it is applied to.
    std::istringstream  ok("data s : struct { count : integer; };\ns.count == 7;\n");
    EXPECT_NO_THROW(Referee::parseSchema(ok, "<countfield>"));
}

// A quantifier expands during AST construction, so nothing downstream ever
// sees one. Rejections therefore have to name the quantifier itself.
TEST(Rdb, QuantifierDiagnostics)
{
    struct Case { char const* src; char const* want; };

    Case    cases[] = {
        {"data b : boolean;\nall x in b: x;\n",
         "array to range over"},
        {"data v : integer[4];\nall x, x in v: x > 0;\n",
         "twice"},
    };

    int     n = 0;
    for (auto const& c : cases)
    {
        std::istringstream  src(c.src);
        try
        {
            Referee::parseSchema(src, "<quant" + std::to_string(n++) + ">");
            ADD_FAILURE() << "expected a rejection for: " << c.src;
        }
        catch (std::exception const& e)
        {
            EXPECT_NE(std::string(e.what()).find(c.want), std::string::npos) << e.what();
        }
    }
}

// Multi-dimensional arrays: the dimension order, and the agreement between the
// column names csvHeaders derives and the ones Loader actually reads. No
// fixture had a 2-D array before, which is how the two came to disagree
// unnoticed -- csvHeaders emitted g[0..2][0..1] while Loader read
// g[0..1][0..2], so a trace built from the documented headers loaded into the
// wrong slots.
TEST(Rdb, MultiDimensionalArrayLayout)
{
    auto    refPath = std::string(REFEREE_TEST_DATA_DIR) + "/arrays2d.ref";
    auto    csvPath = std::string(REFEREE_TEST_DATA_DIR) + "/arrays2d.csv";
    auto    rdbPath = tmpFile("arrays2d");

    referee::db::ingest(refPath, csvPath, /*confPath=*/"", rdbPath);

    std::ifstream       refIn(refPath);
    std::ostringstream  out;
    EXPECT_TRUE(Referee::executeRdb(refIn, refPath, rdbPath, out)) << out.str();

    std::remove(rdbPath.c_str());
}

// The headers csvHeaders derives for a 2-D array must be exactly the ones the
// trace carries, in order. This is the check that would have caught the
// mismatch directly.
TEST(CsvHeaders, MultiDimensionalOrder)
{
    std::istringstream  src("data g : integer[3][2];\n");
    auto                schema = Referee::parseSchema(src, "<md>");

    auto    cols = CsvHeaders::make("g", schema.ast->getProp("g"));

    std::vector<std::string>    want = {
        "g[0][0]", "g[0][1]",
        "g[1][0]", "g[1][1]",
        "g[2][0]", "g[2][1]",
    };
    EXPECT_EQ(cols, want);
}

// Operator precedence follows C++ / Kotlin. This pins the groupings, so the
// grammar and the precedence table in the README cannot drift apart.
TEST(Rdb, OperatorPrecedenceMatchesDocumentation)
{
    auto    refPath = std::string(REFEREE_TEST_DATA_DIR) + "/precedence.ref";
    auto    csvPath = std::string(REFEREE_TEST_DATA_DIR) + "/precedence.csv";
    auto    rdbPath = tmpFile("precedence");

    referee::db::ingest(refPath, csvPath, /*confPath=*/"", rdbPath);

    std::ifstream       refIn(refPath);
    std::ostringstream  out;
    EXPECT_TRUE(Referee::executeRdb(refIn, refPath, rdbPath, out)) << out.str();

    std::remove(rdbPath.c_str());
}

// The README's worked examples have to keep compiling. These are the two that
// carry real syntax rather than illustrative fragments.
TEST(Rdb, ReadmeExamplesCompile)
{
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    for (auto const* leaf : {"/readme_elevator.ref", "/readme_freeze.ref"})
    {
        auto            path = std::string(REFEREE_TEST_DATA_DIR) + leaf;
        std::ifstream   in(path);
        ASSERT_TRUE(in.is_open()) << path;

        EXPECT_NO_THROW({
            auto built = Referee::compile(in, path);
            (void) built.ast;
        }) << leaf;
    }
}

// Sample-and-hold: a sample's values hold from its own timestamp up to, but
// not including, the next one. A bounded window that closes before the next
// sample sees only the held value; one that reaches it sees the change. These
// pin the semantics the README documents, so the two cannot drift apart.
TEST(Rdb, SampleAndHoldBetweenSamples)
{
    auto    refPath = std::string(REFEREE_TEST_DATA_DIR) + "/hold.ref";
    auto    csvPath = std::string(REFEREE_TEST_DATA_DIR) + "/hold.csv";
    auto    rdbPath = tmpFile("hold");

    referee::db::ingest(refPath, csvPath, /*confPath=*/"", rdbPath);

    std::ifstream       refIn(refPath);
    std::ostringstream  out;
    EXPECT_TRUE(Referee::executeRdb(refIn, refPath, rdbPath, out)) << out.str();

    std::remove(rdbPath.c_str());
}

// Unbounded operators step from sample to sample and never consult the clock:
// the same sample sequence must give the same answer whether the gap between
// two rows is one time unit or a million.
TEST(Rdb, UnboundedOperatorsIgnoreSampleSpacing)
{
    auto    refPath = std::string(REFEREE_TEST_DATA_DIR) + "/hold_step.ref";

    for (auto const* trace : {"/hold_tight.csv", "/hold_loose.csv"})
    {
        auto    csvPath = std::string(REFEREE_TEST_DATA_DIR) + trace;
        auto    rdbPath = tmpFile("step");

        referee::db::ingest(refPath, csvPath, /*confPath=*/"", rdbPath);

        std::ifstream       refIn(refPath);
        std::ostringstream  out;
        EXPECT_TRUE(Referee::executeRdb(refIn, refPath, rdbPath, out))
            << trace << ": " << out.str();

        std::remove(rdbPath.c_str());
    }
}

// The hold applies between rows, not within them: an empty cell reads as the
// type's zero rather than being carried forward from the row above.
TEST(Rdb, EmptyCellsAreNotCarriedForward)
{
    auto    refPath = std::string(REFEREE_TEST_DATA_DIR) + "/hold_sparse.ref";
    auto    csvPath = std::string(REFEREE_TEST_DATA_DIR) + "/hold_sparse.csv";
    auto    rdbPath = tmpFile("sparse");

    referee::db::ingest(refPath, csvPath, /*confPath=*/"", rdbPath);

    std::ifstream       refIn(refPath);
    std::ostringstream  out;
    EXPECT_TRUE(Referee::executeRdb(refIn, refPath, rdbPath, out)) << out.str();

    std::remove(rdbPath.c_str());
}

// The nested-scan fallback: bounds that read a `data` signal are not
// loop-invariant, so the window is not monotone and the buffered lowering does
// not apply. Nothing else in the suite reaches that code, including the
// one-sided past-operator forms. Also covers integer -> floating promotion in
// mixed arithmetic.
TEST(Rdb, NestedScanFallbackAndMixedArithmetic)
{
    auto    refPath = std::string(REFEREE_TEST_DATA_DIR) + "/fallback.ref";
    auto    csvPath = std::string(REFEREE_TEST_DATA_DIR) + "/fallback.csv";
    auto    rdbPath = tmpFile("fallback");

    referee::db::ingest(refPath, csvPath, /*confPath=*/"", rdbPath);

    std::ifstream       refIn(refPath);
    std::ostringstream  out;
    bool                allPass = Referee::executeRdb(refIn, refPath, rdbPath, out);
    EXPECT_TRUE(allPass) << out.str();

    std::remove(rdbPath.c_str());
}

// Time-bounded operators on the linear lowering, over an irregularly spaced
// trace. Covers conf-valued bounds against their literal equivalents, one-sided
// bounds on the past operators (which used to crash the compiler), degenerate
// windows bottoming out at the strong/weak base value, and bounded operators
// nested inside other temporal operators.
TEST(Rdb, BoundedTemporalOperators)
{
    auto    refPath  = std::string(REFEREE_TEST_DATA_DIR) + "/bounded.ref";
    auto    csvPath  = std::string(REFEREE_TEST_DATA_DIR) + "/bounded.csv";
    auto    confPath = std::string(REFEREE_TEST_DATA_DIR) + "/bounded_conf.csv";
    auto    rdbPath  = tmpFile("bounded");

    referee::db::ingest(refPath, csvPath, confPath, rdbPath);

    std::ifstream       refIn(refPath);
    std::ostringstream  out;
    bool                allPass = Referee::executeRdb(refIn, refPath, rdbPath, out);
    EXPECT_TRUE(allPass) << out.str();

    std::remove(rdbPath.c_str());
}

// A `conf` field is read through the pointer computed for it, not through
// whatever pointer happened to be current. Reading the second and third fields
// is the part that matters: an offset mistake is invisible with a single conf.
TEST(Rdb, ConfFieldsReadAtCorrectOffsets)
{
    auto    refPath  = std::string(REFEREE_TEST_DATA_DIR) + "/conf_fields.ref";
    auto    csvPath  = std::string(REFEREE_TEST_DATA_DIR) + "/conf_fields.csv";
    auto    confPath = std::string(REFEREE_TEST_DATA_DIR) + "/conf_fields_conf.csv";
    auto    rdbPath  = tmpFile("conf-fields");

    referee::db::ingest(refPath, csvPath, confPath, rdbPath);

    std::ifstream       refIn(refPath);
    std::ostringstream  out;
    bool                allPass = Referee::executeRdb(refIn, refPath, rdbPath, out);
    EXPECT_TRUE(allPass) << out.str();

    std::remove(rdbPath.c_str());
}

TEST(Rdb, ExprFreezeNotBuffered)
{
    auto    refPath = std::string(REFEREE_TEST_DATA_DIR) + "/expr_freeze.ref";
    auto    csvPath = std::string(REFEREE_TEST_DATA_DIR) + "/expr_freeze.csv";
    auto    rdbPath = tmpFile("expr-freeze");

    referee::db::ingest(refPath, csvPath, /*confPath=*/"", rdbPath);

    std::ifstream       refIn(refPath);
    std::ostringstream  out;
    bool                allPass = Referee::executeRdb(refIn, refPath, rdbPath, out);
    EXPECT_TRUE(allPass) << out.str();

    std::remove(rdbPath.c_str());
}

// Phase 12 — data=expr: the RDB schema must contain only the CSV-backed props,
// and __prepare__ must fill every real state's computed slot with the correctly
// evaluated boolean.
TEST(Rdb, ExprDataBlobValues)
{
    auto    refPath = std::string(REFEREE_TEST_DATA_DIR) + "/expr_data.ref";
    auto    csvPath = std::string(REFEREE_TEST_DATA_DIR) + "/expr_data.csv";
    auto    rdbPath = tmpFile("expr-data-blobs");

    referee::db::ingest(refPath, csvPath, /*confPath=*/"", rdbPath);

    std::ifstream       refStream(refPath);
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    auto JITOrErr = llvm::orc::LLJITBuilder().create();
    ASSERT_TRUE(!!JITOrErr);
    auto jit = std::move(*JITOrErr);

    auto built = Referee::compile(refStream, refPath, &jit->getDataLayout());
    auto* astModule = built.ast;

    if (auto Err = jit->addIRModule(
            llvm::orc::ThreadSafeModule(std::move(built.mod), std::move(built.ctx))))
        throw std::runtime_error("Failed to add IR module to JIT");

    referee::db::Reader rdb(rdbPath);
    ASSERT_EQ(rdb.numProps(), 2u);
    ASSERT_EQ(rdb.numStates(), 6u);

    std::size_t numStates = rdb.numStates();
    std::size_t totalProps = astModule->getPropNames().size();
    std::size_t stateStride = sizeof(int64_t) + totalProps * sizeof(void*);
    std::vector<std::uint8_t> runStates(numStates * stateStride, 0);

    std::vector<std::string> csvPropNames;
    std::map<std::string, std::size_t> csvPropIndices;
    for (auto const& n : astModule->getPropNames())
    {
        if (!astModule->isExprData(n))
        {
            csvPropIndices[n] = csvPropNames.size();
            csvPropNames.push_back(n);
        }
    }

    std::vector<std::vector<std::uint8_t>> computedBuffers(totalProps);
    for (std::size_t pi = 0; pi < totalProps; pi++)
    {
        auto const& name = astModule->getPropNames()[pi];
        if (astModule->isExprData(name))
        {
            std::size_t typeSize = astModule->getProp(name)->size();
            computedBuffers[pi].resize(numStates * typeSize, 0);
        }
    }

    for (std::size_t si = 0; si < numStates; si++)
    {
        uint8_t* statePtr = runStates.data() + si * stateStride;
        int64_t t = rdb.time(si);
        std::memcpy(statePtr, &t, sizeof(t));

        for (std::size_t pi = 0; pi < totalProps; pi++)
        {
            auto const& name = astModule->getPropNames()[pi];
            void* valPtr = nullptr;
            if (astModule->isExprData(name))
            {
                std::size_t typeSize = astModule->getProp(name)->size();
                valPtr = computedBuffers[pi].data() + si * typeSize;
            }
            else
            {
                valPtr = const_cast<void*>(rdb.propBlob(si, csvPropIndices[name]));
            }
            std::memcpy(statePtr + sizeof(int64_t) + pi * sizeof(void*), &valPtr, sizeof(valPtr));
        }
    }

    auto prepSymOrErr = jit->lookup("__prepare__");
    ASSERT_TRUE(!!prepSymOrErr);

    using PrepFn = void(*)(void*, void*, void*);
    auto prepFn = (*prepSymOrErr).toPtr<PrepFn>();
    prepFn(runStates.data(), runStates.data() + (numStates - 1) * stateStride, rdb.confPtr());

    auto boolAt = [&](std::size_t si, std::size_t pi) -> bool {
        auto const& name = astModule->getPropNames()[pi];
        if (astModule->isExprData(name))
        {
            return computedBuffers[pi][si] != 0;
        }
        else
        {
            auto* p = static_cast<std::uint8_t const*>(rdb.propBlob(si, csvPropIndices[name]));
            return p && (*p != 0);
        }
    };

    // Trace (real states si=1..4, sentinels at si=0 and si=5):
    //   si=1  t=0     a=T b=F
    //   si=2  t=1000  a=F b=T
    //   si=3  t=2000  a=T b=T
    //   si=4  t=3000  a=F b=F

    // anb = a && b
    EXPECT_FALSE(boolAt(1, 2));   // T&&F = F
    EXPECT_FALSE(boolAt(2, 2));   // F&&T = F
    EXPECT_TRUE (boolAt(3, 2));   // T&&T = T
    EXPECT_FALSE(boolAt(4, 2));   // F&&F = F

    // aob = a || b
    EXPECT_TRUE (boolAt(1, 3));   // T||F = T
    EXPECT_TRUE (boolAt(2, 3));   // F||T = T
    EXPECT_TRUE (boolAt(3, 3));   // T||T = T
    EXPECT_FALSE(boolAt(4, 3));   // F||F = F

    // nota = !a
    EXPECT_FALSE(boolAt(1, 4));   // !T = F
    EXPECT_TRUE (boolAt(2, 4));   // !F = T
    EXPECT_FALSE(boolAt(3, 4));   // !T = F
    EXPECT_TRUE (boolAt(4, 4));   // !F = T

    // once_a = O(a): true from si=1 onward (a is true at si=1)
    EXPECT_TRUE(boolAt(1, 5));
    EXPECT_TRUE(boolAt(2, 5));
    EXPECT_TRUE(boolAt(3, 5));
    EXPECT_TRUE(boolAt(4, 5));

    std::remove(rdbPath.c_str());
}

// ─── Temporal operator tests ──────────────────────────────────────────────────
//
// Phase 13 — data=expr with temporal operators: execute all G(prop==expr) specs.
// This is the self-checking pattern: every computed prop is stored at ingest
// time and then verified against the live expression re-evaluation at execute
// time.  All specs in expr_temporal.ref must pass.
TEST(Rdb, ExprTemporalIngestAndExecute)
{
    auto    refPath = std::string(REFEREE_TEST_DATA_DIR) + "/expr_temporal.ref";
    auto    csvPath = std::string(REFEREE_TEST_DATA_DIR) + "/expr_data.csv";
    auto    rdbPath = tmpFile("expr-temporal");

    referee::db::ingest(refPath, csvPath, /*confPath=*/"", rdbPath);

    std::ifstream       refIn(refPath);
    std::ostringstream  out;
    bool                allPass = Referee::executeRdb(refIn, refPath, rdbPath, out);
    EXPECT_TRUE(allPass) << out.str();

    std::remove(rdbPath.c_str());
}

// Phase 14 — blob-level verification of every temporal operator at every real
// state.  Props are in declaration order from expr_temporal.ref:
//
//  pi  name          operator
//   0  a             CSV
//   1  b             CSV
//   2  xs_a          Xs(a)
//   3  xw_a          Xw(a)
//   4  xs_b          Xs(b)
//   5  xw_b          Xw(b)
//   6  ys_a          Ys(a)
//   7  yw_a          Yw(a)
//   8  ys_b          Ys(b)
//   9  ss_ab         Ss(a, b)
//  10  sw_ab         Sw(a, b)
//  11  ts_ab         Ts(a, b)
//  12  tw_ab         Tw(a, b)
//  13  us_ab         Us(a, b)
//  14  uw_ab         Uw(a, b)
//  15  rs_ab         Rs(a, b)
//  16  rw_ab         Rw(a, b)
//  17  xs_ys_a       Xs(Ys(a))
//  18  ys_xs_a       Ys(Xs(a))
//  19  us_a_xsb      Us(a, Xs(b))
//  20  ss_xsa_b      Ss(Xs(a), b)
//  21  ts_ysb_a      Ts(Ys(b), a)
//  22  complex1      O(a) && F(!b)
//  23  hist_b_ysb    H(b => Ys(b))
//  24  sw_notb_a     Sw(!b, a)
//  25  rs_b_nota     Rs(b, !a)
//
// Trace: 4 real rows => numStates = 6.
//   si=0 left sentinel (all zeroed by writer)
//   si=1 t=0     a=T b=F
//   si=2 t=1000  a=F b=T
//   si=3 t=2000  a=T b=T
//   si=4 t=3000  a=F b=F
//   si=5 right sentinel (all zeroed by writer)
//
// Computed values are worked out step-by-step in expr_temporal.ref comments.
TEST(Rdb, ExprTemporalBlobValues)
{
    auto    refPath = std::string(REFEREE_TEST_DATA_DIR) + "/expr_temporal.ref";
    auto    csvPath = std::string(REFEREE_TEST_DATA_DIR) + "/expr_data.csv";
    auto    rdbPath = tmpFile("expr-temporal-blobs");

    referee::db::ingest(refPath, csvPath, /*confPath=*/"", rdbPath);

    std::ifstream       refStream(refPath);
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    auto JITOrErr = llvm::orc::LLJITBuilder().create();
    ASSERT_TRUE(!!JITOrErr);
    auto jit = std::move(*JITOrErr);

    auto built = Referee::compile(refStream, refPath, &jit->getDataLayout());
    auto* astModule = built.ast;

    if (auto Err = jit->addIRModule(
            llvm::orc::ThreadSafeModule(std::move(built.mod), std::move(built.ctx))))
        throw std::runtime_error("Failed to add IR module to JIT");

    referee::db::Reader rdb(rdbPath);
    ASSERT_EQ(rdb.numProps(), 2u);
    ASSERT_EQ(rdb.numStates(), 6u);

    std::size_t numStates = rdb.numStates();
    std::size_t totalProps = astModule->getPropNames().size();
    std::size_t stateStride = sizeof(int64_t) + totalProps * sizeof(void*);
    std::vector<std::uint8_t> runStates(numStates * stateStride, 0);

    std::vector<std::string> csvPropNames;
    std::map<std::string, std::size_t> csvPropIndices;
    for (auto const& n : astModule->getPropNames())
    {
        if (!astModule->isExprData(n))
        {
            csvPropIndices[n] = csvPropNames.size();
            csvPropNames.push_back(n);
        }
    }

    std::vector<std::vector<std::uint8_t>> computedBuffers(totalProps);
    for (std::size_t pi = 0; pi < totalProps; pi++)
    {
        auto const& name = astModule->getPropNames()[pi];
        if (astModule->isExprData(name))
        {
            std::size_t typeSize = astModule->getProp(name)->size();
            computedBuffers[pi].resize(numStates * typeSize, 0);
        }
    }

    for (std::size_t si = 0; si < numStates; si++)
    {
        uint8_t* statePtr = runStates.data() + si * stateStride;
        int64_t t = rdb.time(si);
        std::memcpy(statePtr, &t, sizeof(t));

        for (std::size_t pi = 0; pi < totalProps; pi++)
        {
            auto const& name = astModule->getPropNames()[pi];
            void* valPtr = nullptr;
            if (astModule->isExprData(name))
            {
                std::size_t typeSize = astModule->getProp(name)->size();
                valPtr = computedBuffers[pi].data() + si * typeSize;
            }
            else
            {
                valPtr = const_cast<void*>(rdb.propBlob(si, csvPropIndices[name]));
            }
            std::memcpy(statePtr + sizeof(int64_t) + pi * sizeof(void*), &valPtr, sizeof(valPtr));
        }
    }

    auto prepSymOrErr = jit->lookup("__prepare__");
    ASSERT_TRUE(!!prepSymOrErr);

    using PrepFn = void(*)(void*, void*, void*);
    auto prepFn = (*prepSymOrErr).toPtr<PrepFn>();
    prepFn(runStates.data(), runStates.data() + (numStates - 1) * stateStride, rdb.confPtr());

    auto B = [&](std::size_t si, std::size_t pi) -> bool {
        auto const& name = astModule->getPropNames()[pi];
        if (astModule->isExprData(name))
        {
            return computedBuffers[pi][si] != 0;
        }
        else
        {
            auto* p = static_cast<std::uint8_t const*>(rdb.propBlob(si, csvPropIndices[name]));
            return p && (*p != 0);
        }
    };

    // Verify CSV props are as expected (sanity).
    //   a: T F T F  (si 1-4)
    EXPECT_TRUE (B(1,0)); EXPECT_FALSE(B(2,0)); EXPECT_TRUE (B(3,0)); EXPECT_FALSE(B(4,0));
    //   b: F T T F
    EXPECT_FALSE(B(1,1)); EXPECT_TRUE (B(2,1)); EXPECT_TRUE (B(3,1)); EXPECT_FALSE(B(4,1));
    // xs_a = Xs(a):  a[si+1]  →  a[2]=F, a[3]=T, a[4]=F, a[5]=F
    EXPECT_FALSE(B(1,2)); EXPECT_TRUE (B(2,2)); EXPECT_FALSE(B(3,2)); EXPECT_FALSE(B(4,2));

    // xw_a = Xw(a): same for real states except si=4 is true due to weak sentinel base
    EXPECT_FALSE(B(1,3)); EXPECT_TRUE (B(2,3)); EXPECT_FALSE(B(3,3)); EXPECT_TRUE (B(4,3));

    // xs_b = Xs(b):  b[si+1]  →  b[2]=T, b[3]=T, b[4]=F, b[5]=F
    EXPECT_TRUE (B(1,4)); EXPECT_TRUE (B(2,4)); EXPECT_FALSE(B(3,4)); EXPECT_FALSE(B(4,4));

    // xw_b = Xw(b): same for real states except si=4 is true
    EXPECT_TRUE (B(1,5)); EXPECT_TRUE (B(2,5)); EXPECT_FALSE(B(3,5)); EXPECT_TRUE (B(4,5));

    // ── Y operators ──────────────────────────────────────────────────────────
    // ys_a = Ys(a):  a[si-1]  →  a[0]=F, a[1]=T, a[2]=F, a[3]=T
    EXPECT_FALSE(B(1,6)); EXPECT_TRUE (B(2,6)); EXPECT_FALSE(B(3,6)); EXPECT_TRUE (B(4,6));

    // yw_a = Yw(a): same except si=1 gets the weak sentinel base true
    EXPECT_TRUE (B(1,7)); EXPECT_TRUE (B(2,7)); EXPECT_FALSE(B(3,7)); EXPECT_TRUE (B(4,7));

    // ys_b = Ys(b):  b[si-1]  →  b[0]=F, b[1]=F, b[2]=T, b[3]=T
    EXPECT_FALSE(B(1,8)); EXPECT_FALSE(B(2,8)); EXPECT_TRUE (B(3,8)); EXPECT_TRUE (B(4,8));

    // ── S (since) operators ──────────────────────────────────────────────────
    // ss_ab = Ss(a,b):  F, T, T, F
    EXPECT_FALSE(B(1,9));  EXPECT_TRUE (B(2,9));  EXPECT_TRUE (B(3,9));  EXPECT_FALSE(B(4,9));

    // sw_ab = Sw(a,b):  T, T, T, F
    EXPECT_TRUE (B(1,10)); EXPECT_TRUE (B(2,10)); EXPECT_TRUE (B(3,10)); EXPECT_FALSE(B(4,10));

    // ── T (trigger) operators ─────────────────────────────────────────────────
    // ts_ab = Ts(a,b):  b && (a || prev)
    //   si=1: b[1]=F => F
    //   si=2: b[2]=T && (a[2]=F || res[1]=F) => F
    //   si=3: b[3]=T && (a[3]=T || res[2]=F) => T
    //   si=4: b[4]=F => F
    //   real: F, F, T, F
    EXPECT_FALSE(B(1,11)); EXPECT_FALSE(B(2,11)); EXPECT_TRUE (B(3,11)); EXPECT_FALSE(B(4,11));

    // tw_ab = Tw(a,b): same for real states since b is false at si=1 and si=2
    EXPECT_FALSE(B(1,12)); EXPECT_FALSE(B(2,12)); EXPECT_TRUE (B(3,12)); EXPECT_FALSE(B(4,12));

    // ── U (until) operators ───────────────────────────────────────────────────
    // us_ab = Us(a,b):  T, T, T, F
    EXPECT_TRUE (B(1,13)); EXPECT_TRUE (B(2,13)); EXPECT_TRUE (B(3,13)); EXPECT_FALSE(B(4,13));

    // uw_ab = Uw(a,b): same for real states
    EXPECT_TRUE (B(1,14)); EXPECT_TRUE (B(2,14)); EXPECT_TRUE (B(3,14)); EXPECT_FALSE(B(4,14));

    // ── R (release) operators ─────────────────────────────────────────────────
    // rs_ab = Rs(a,b):  b && (a || next)
    //   si=4: b[4]=F => F
    //   si=3: b[3]=T && (a[3]=T || res[4]=F) => T
    //   si=2: b[2]=T && (a[2]=F || res[3]=T) => T
    //   si=1: b[1]=F => F
    //   real: F, T, T, F
    EXPECT_FALSE(B(1,15)); EXPECT_TRUE (B(2,15)); EXPECT_TRUE (B(3,15)); EXPECT_FALSE(B(4,15));

    // rw_ab = Rw(a,b): same for real states
    EXPECT_FALSE(B(1,16)); EXPECT_TRUE (B(2,16)); EXPECT_TRUE (B(3,16)); EXPECT_FALSE(B(4,16));

    // ── Complex composed operators ─────────────────────────────────────────────
    // xs_ys_a = Xs(Ys(a)):
    //   Ys(a): [F,F,T,F,T,F]  →  Xs of that: [F,T,F,T,F,false]
    //   real: T, F, T, F
    EXPECT_TRUE (B(1,17)); EXPECT_FALSE(B(2,17)); EXPECT_TRUE (B(3,17)); EXPECT_FALSE(B(4,17));

    // ys_xs_a = Ys(Xs(a)):
    //   real: F, F, T, F
    EXPECT_FALSE(B(1,18)); EXPECT_FALSE(B(2,18)); EXPECT_TRUE (B(3,18)); EXPECT_FALSE(B(4,18));

    // us_a_xsb = Us(a, Xs(b)):
    //   Xs(b): [F,T,T,F,F,false]
    //   Us: si5=F,si4=F,si3=F||(T&&F)=F,si2=T||(F&&F)=T,si1=T||(T&&T)=T,si0=F
    //   real: T, T, F, F
    EXPECT_TRUE (B(1,19)); EXPECT_TRUE (B(2,19)); EXPECT_FALSE(B(3,19)); EXPECT_FALSE(B(4,19));

    // ss_xsa_b = Ss(Xs(a), b):
    //   Xs(a): [T,F,T,F,F,false]
    //   Ss: si0=F,si1=F||(F&&F)=F,si2=T||(T&&F)=T,si3=T||(F&&T)=T,si4=F||(F&&T)=F
    //   real: F, T, T, F
    EXPECT_FALSE(B(1,20)); EXPECT_TRUE (B(2,20)); EXPECT_TRUE (B(3,20)); EXPECT_FALSE(B(4,20));

    // ts_ysb_a = Ts(Ys(b), a):
    //   Ys(b): [F,F,F,T,T,F]
    //   Ts: res[si] = a[si] && (Ys(b)[si] || res[si-1])
    //   real: F, F, T, F
    EXPECT_FALSE(B(1,21)); EXPECT_FALSE(B(2,21)); EXPECT_TRUE (B(3,21)); EXPECT_FALSE(B(4,21));

    // complex1 = O(a) && F(!b):
    //   O(a): [F,T,T,T,T,T];  F(!b): [T,T,T,T,T,F]
    //   AND:  [F,T,T,T,T,F]   real: T, T, T, T
    EXPECT_TRUE (B(1,22)); EXPECT_TRUE (B(2,22)); EXPECT_TRUE (B(3,22)); EXPECT_TRUE (B(4,22));

    // hist_b_ysb = H(b => Ys(b)):
    //   Ys(b): [F,F,F,T,T,F];  b=>Ys(b)=!b||Ys(b): [T,T,F,T,T,T]
    //   H: si0=T,si1=T,si2=F,si3=F,si4=F,si5=F   real: T, F, F, F
    EXPECT_TRUE (B(1,23)); EXPECT_FALSE(B(2,23)); EXPECT_FALSE(B(3,23)); EXPECT_FALSE(B(4,23));

    // sw_notb_a = Sw(!b, a):
    //   !b: [T,T,F,F,T,T]
    //   real: T, F, T, T
    EXPECT_TRUE (B(1,24)); EXPECT_FALSE(B(2,24)); EXPECT_TRUE (B(3,24)); EXPECT_TRUE (B(4,24));

    // rs_b_nota = Rs(b, !a):
    //   real: F, T, F, F
    EXPECT_FALSE(B(1,25)); EXPECT_TRUE (B(2,25)); EXPECT_FALSE(B(3,25)); EXPECT_FALSE(B(4,25));

    std::remove(rdbPath.c_str());
}

// Phase 15 — data=expr with time-bounded temporal operators over a longer trace.
// Bounded operators are not eligible for the O(N) buffered lowering, so this
// pins the computed-prop path against the O(N^2) fallback for every bounded
// operator, plus point-wise boolean operations and implications.
TEST(Rdb, ExprBoundedIngestAndExecute)
{
    auto    refPath = std::string(REFEREE_TEST_DATA_DIR) + "/expr_data_long.ref";
    auto    csvPath = std::string(REFEREE_TEST_DATA_DIR) + "/expr_data_long.csv";
    auto    rdbPath = tmpFile("expr-bounded");

    referee::db::ingest(refPath, csvPath, /*confPath=*/"", rdbPath);

    std::ifstream       refIn(refPath);
    std::ostringstream  out;
    bool                allPass = Referee::executeRdb(refIn, refPath, rdbPath, out);
    EXPECT_TRUE(allPass) << out.str();

    std::remove(rdbPath.c_str());
}
