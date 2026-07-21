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

//  End-to-end tests for the two CLI binaries. These drive the real executables
//  as subprocesses: argument parsing, subcommand dispatch and exit codes live
//  in main(), which no in-process test can reach.

#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{

struct Result
{
    int         status;
    std::string output;     //  stdout and stderr, combined
};

std::string     quote(std::string const& s)
{
    std::string out = "'";
    for (char c : s)
    {
        if (c == '\'') out += "'\\''";
        else           out += c;
    }
    return out + "'";
}

Result  run(std::string const& cmd)
{
    Result  r{-1, {}};

    //  2>&1 so diagnostics are visible to the assertions; several of these
    //  cases are about what the tool says when it refuses.
    FILE*   pipe = ::popen((cmd + " 2>&1").c_str(), "r");
    if (pipe == nullptr)
        return r;

    std::array<char, 4096>  buf{};
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr)
        r.output += buf.data();

    int     rc = ::pclose(pipe);
    r.status = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
    return r;
}

std::string     data(std::string const& leaf)
{
    return std::string(REFEREE_TEST_DATA_DIR) + "/" + leaf;
}

std::string     tmpPath(std::string const& tag)
{
    std::string         pat = "/tmp/referee-cli-" + tag + "-XXXXXX";
    std::vector<char>   buf(pat.begin(), pat.end());
    buf.push_back('\0');
    int     fd = ::mkstemp(buf.data());
    EXPECT_GE(fd, 0);
    if (fd >= 0) ::close(fd);
    return std::string(buf.data());
}

} // namespace

// ── referee ──────────────────────────────────────────────────────────────────

TEST(Cli, NoArgumentsIsAnError)
{
    auto    r = run(quote(REFEREE_BIN));
    EXPECT_NE(r.status, 0);
    EXPECT_NE(r.output.find("subcommand is required"), std::string::npos) << r.output;
}

TEST(Cli, HelpListsSubcommands)
{
    auto    r = run(quote(REFEREE_BIN) + " --help");
    EXPECT_EQ(r.status, 0) << r.output;
    EXPECT_NE(r.output.find("compile"), std::string::npos) << r.output;
    EXPECT_NE(r.output.find("execute"), std::string::npos) << r.output;
}

TEST(Cli, CompileEmitsIR)
{
    auto    r = run(quote(REFEREE_BIN) + " compile " + quote(data("pass.ref")));
    EXPECT_EQ(r.status, 0) << r.output;
    EXPECT_NE(r.output.find("define"),      std::string::npos) << r.output;
    EXPECT_NE(r.output.find("__prop_t"),    std::string::npos) << r.output;
}

TEST(Cli, CompileRejectsMissingFile)
{
    auto    r = run(quote(REFEREE_BIN) + " compile /no/such/file.ref");
    EXPECT_NE(r.status, 0);
}

// A .ref whose requirements hold over the trace exits 0; one that does not
// exits non-zero. That exit code is the CLI's contract with a CI job.
TEST(Cli, ExecutePassExitsZero)
{
    auto    r = run(quote(REFEREE_BIN) + " execute "
                  + quote(data("pass.ref")) + " " + quote(data("data.csv"))
                  + " --conf " + quote(data("conf.csv")));
    EXPECT_EQ(r.status, 0) << r.output;
    EXPECT_NE(r.output.find("PASS"), std::string::npos) << r.output;
}

TEST(Cli, ExecuteFailExitsNonZero)
{
    auto    r = run(quote(REFEREE_BIN) + " execute "
                  + quote(data("fail.ref")) + " " + quote(data("data.csv"))
                  + " --conf " + quote(data("conf.csv")));
    EXPECT_NE(r.status, 0);
    EXPECT_NE(r.output.find("FAIL"), std::string::npos) << r.output;
}

TEST(Cli, ExecuteAcceptsYamlTrace)
{
    auto    r = run(quote(REFEREE_BIN) + " execute "
                  + quote(data("pass.ref")) + " " + quote(data("data.yaml"))
                  + " --conf " + quote(data("conf.yaml")));
    EXPECT_EQ(r.status, 0) << r.output;
}

// A malformed .ref must fail the run, not print a complaint and emit IR anyway.
TEST(Cli, ExecuteReportsBadSyntax)
{
    auto    bad = tmpPath("bad");
    {
        std::FILE* f = std::fopen(bad.c_str(), "w");
        ASSERT_NE(f, nullptr);
        std::fputs("data a : boolean;\nG(a;\n", f);   // unbalanced paren
        std::fclose(f);
    }

    auto    r = run(quote(REFEREE_BIN) + " compile " + quote(bad));
    EXPECT_NE(r.status, 0) << r.output;
    std::remove(bad.c_str());
}

TEST(Cli, DebugFlagIsAccepted)
{
    auto    r = run(quote(REFEREE_BIN) + " --debug compile " + quote(data("pass.ref")));
    EXPECT_EQ(r.status, 0) << r.output;
}

// -I has to work when given after the subcommand, which is where it gets typed.
TEST(Cli, IncludePathIsSearched)
{
    auto    ref  = data("import/via_path.ref");
    auto    csv  = data("import/data.csv");
    auto    defs = data("import/defs");

    auto    without = run(quote(REFEREE_BIN) + " execute " + quote(ref) + " " + quote(csv));
    EXPECT_NE(without.status, 0) << without.output;
    EXPECT_NE(without.output.find("cannot find imported file"), std::string::npos)
        << without.output;

    auto    with = run(quote(REFEREE_BIN) + " execute -I " + quote(defs)
                     + " " + quote(ref) + " " + quote(csv));
    EXPECT_EQ(with.status, 0) << with.output;
}

// Imported requirements are reported with a file-qualified label.
TEST(Cli, ImportedRequirementsAreLabelledByFile)
{
    auto    r = run(quote(REFEREE_BIN) + " execute "
                  + quote(data("import/main.ref")) + " " + quote(data("import/data.csv")));
    EXPECT_EQ(r.status, 0) << r.output;
    EXPECT_NE(r.output.find("reqs/one.ref:"), std::string::npos) << r.output;
    EXPECT_NE(r.output.find("reqs/two.ref:"), std::string::npos) << r.output;
}

// ── rdb ──────────────────────────────────────────────────────────────────────

TEST(Cli, RdbNoArgumentsIsAnError)
{
    auto    r = run(quote(RDB_BIN));
    EXPECT_NE(r.status, 0);
}

TEST(Cli, RdbHelpListsSubcommands)
{
    auto    r = run(quote(RDB_BIN) + " --help");
    EXPECT_EQ(r.status, 0) << r.output;
    EXPECT_NE(r.output.find("build"), std::string::npos) << r.output;
    EXPECT_NE(r.output.find("dump"),  std::string::npos) << r.output;
}

// build -> dump -> execute: the packed file has to survive the whole round trip
// through the CLI, not just through the in-process API.
TEST(Cli, RdbBuildDumpAndExecuteRoundTrip)
{
    auto    out = tmpPath("roundtrip") + ".rdb";

    auto    build = run(quote(RDB_BIN) + " build "
                      + quote(data("pass.ref")) + " " + quote(data("data.csv"))
                      + " --conf " + quote(data("conf.csv"))
                      + " -o " + quote(out));
    ASSERT_EQ(build.status, 0) << build.output;

    auto    dump = run(quote(RDB_BIN) + " dump " + quote(out));
    EXPECT_EQ(dump.status, 0) << dump.output;
    EXPECT_NE(dump.output.find("schema:"), std::string::npos) << dump.output;
    EXPECT_NE(dump.output.find("states:"), std::string::npos) << dump.output;

    auto    exec = run(quote(REFEREE_BIN) + " execute "
                     + quote(data("pass.ref")) + " " + quote(out));
    EXPECT_EQ(exec.status, 0) << exec.output;

    std::remove(out.c_str());
}

TEST(Cli, RdbBuildRejectsMissingInputs)
{
    auto    out = tmpPath("missing");
    auto    r   = run(quote(RDB_BIN) + " build /no/such.ref /no/such.csv -o " + quote(out));
    EXPECT_NE(r.status, 0);
    std::remove(out.c_str());
}

TEST(Cli, RdbDumpRejectsNonRdbFile)
{
    auto    r = run(quote(RDB_BIN) + " dump " + quote(data("data.csv")));
    EXPECT_NE(r.status, 0) << r.output;
}

// `rdb build` resolves imports the same way `referee` does, so a spec split
// across files packs to the same schema as an inline one.
TEST(Cli, RdbBuildResolvesImports)
{
    auto    out = tmpPath("imports");

    auto    build = run(quote(RDB_BIN) + " build "
                      + quote(data("import/main.ref")) + " " + quote(data("import/data.csv"))
                      + " -o " + quote(out));
    ASSERT_EQ(build.status, 0) << build.output;

    auto    dump = run(quote(RDB_BIN) + " dump " + quote(out));
    EXPECT_EQ(dump.status, 0) << dump.output;
    for (auto const* name : {"lock", "a", "b"})
        EXPECT_NE(dump.output.find(name), std::string::npos) << dump.output;

    std::remove(out.c_str());
}

TEST(Cli, RdbBuildWithIncludePath)
{
    auto    out = tmpPath("rdb-inc");

    auto    r = run(quote(RDB_BIN) + " build "
                  + quote(data("import/via_path.ref")) + " " + quote(data("import/data.csv"))
                  + " -I " + quote(data("import/defs"))
                  + " -o " + quote(out));
    EXPECT_EQ(r.status, 0) << r.output;

    std::remove(out.c_str());
}
