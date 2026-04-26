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

#include <gtest/gtest.h>

#include "rdb/database.hpp"
#include "rdb/ingest.hpp"
#include "referee.hpp"
#include "strings.hpp"
#include "syntax.hpp"
#include "visitors/loader.hpp"

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
