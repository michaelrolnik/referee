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
#include <fstream>
#include <vector>
#include <unistd.h>
#include <sys/stat.h>
#include <sstream>
#include <cstring>
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

namespace {

std::string     tmpPath(std::string const& tag, std::string const& ext)
{
    std::string         pat = "/tmp/referee-cli-" + tag + "-XXXXXX";
    std::vector<char>   buf(pat.begin(), pat.end());
    buf.push_back('\0');
    int     fd = ::mkstemp(buf.data());
    EXPECT_GE(fd, 0);
    if (fd >= 0) ::close(fd);
    std::string         out(buf.data());
    std::remove(out.c_str());
    return out + ext;
}

} // namespace

// A mistyped option should say what was meant. CLI11 reports "not expected",
// which is no help when the cause is a typo -- and for a subcommand with a
// multi-value positional it does not even name the offending token, so the
// scan is over argv rather than over the message.
TEST(Cli, SuggestsMistypedOptions)
{
    struct Case { char const* typo; char const* meant; };

    Case    cases[] = {
        { "--sucess",  "--success" },
        { "--verbse",  "--verbose" },
        { "--includ",  "--include" },
        { "--librari", "--library" },
        { "--failur",  "--failure" },
    };

    auto    ref = std::string(REFEREE_TEST_DATA_DIR) + "/pass.ref";
    auto    csv = std::string(REFEREE_TEST_DATA_DIR) + "/pass.csv";

    for (auto const& c : cases)
    {
        auto    r = run(quote(REFEREE_BIN) + " execute " + quote(ref) + " " + quote(csv)
                        + " " + c.typo + " x 2>&1");
        EXPECT_NE(r.output.find(std::string("did you mean ") + c.meant), std::string::npos)
            << c.typo << " -> " << r.output;
    }

    //  Nothing close: stay quiet rather than guess.
    auto    r = run(quote(REFEREE_BIN) + " execute " + quote(ref) + " " + quote(csv)
                    + " --zzzzzzzzzz 2>&1");
    EXPECT_EQ(r.output.find("did you mean"), std::string::npos) << r.output;
}

// Layout drift. A .so built against one version of a type keeps working
// against a changed specification, silently, unless the symbol says which
// layout it was built for. Before the structural hash this test's second run
// reported a confident wrong verdict; now it refuses to run.
TEST(Cli, StaleObjectIsRefusedRatherThanCalled)
{
    auto    dir = tmpPath("drift", "");
    ASSERT_EQ(::mkdir(dir.c_str(), 0755), 0);

    auto    ref = dir + "/a.ref";
    auto    hdr = dir + "/a.h";
    auto    src = dir + "/a.c";
    auto    csv = dir + "/a.csv";

    auto    write = [](std::string const& path, std::string const& text) {
        std::ofstream   f(path);
        f << text;
    };

    //  Built against Dir { M2S, S2M }, where M2S is 1.
    write(ref, "type Dir : enum { M2S, S2M };\n"
               "func classify : (integer) -> Dir;\n"
               "data i : integer;\ndata d = classify(i);\nG(d.M2S);\n");
    write(csv, "__time__,i\n0,5\n");

    ASSERT_EQ(run(quote(REFEREE_BIN) + " header " + quote(ref) + " -o " + quote(hdr)).status, 0);
    //  Generated stub, so the manifest comes with it.
    auto    stub = run(quote(REFEREE_BIN) + " header " + quote(ref) + " --stub --header-name "
                       + quote(hdr) + " -o " + quote(src));
    ASSERT_EQ(stub.status, 0) << stub.output;
    {
        std::ifstream       in(src);
        std::stringstream   buf;  buf << in.rdbuf();
        auto                text = buf.str();
        auto                at   = text.find("/*  TODO  */\n    return 0;\n");
        ASSERT_NE(at, std::string::npos);
        text.replace(at, std::strlen("/*  TODO  */\n    return 0;\n"), "return arg0 > 0 ? 1 : 2;\n");
        write(src, text);
    }

    auto    so = dir + "/liba.so";
    ASSERT_EQ(run("cc -shared -fPIC " + quote(src) + " -o " + quote(so) + " -I " + quote(dir) + " 2>&1").status, 0);

    auto    ok = run(quote(REFEREE_BIN) + " execute " + quote(ref) + " " + quote(csv)
                     + " -L " + quote(dir) + " 2>&1");
    EXPECT_EQ(ok.status, 0) << ok.output;

    //  A member is inserted, so 1 now means UNKNOWN. Same .so, untouched.
    write(ref, "type Dir : enum { UNKNOWN, M2S, S2M };\n"
               "func classify : (integer) -> Dir;\n"
               "data i : integer;\ndata d = classify(i);\nG(d.M2S);\n");

    auto    bad = run(quote(REFEREE_BIN) + " execute " + quote(ref) + " " + quote(csv)
                      + " -L " + quote(dir) + " 2>&1");
    EXPECT_NE(bad.status, 0) << bad.output;

    //  The object *does* implement classify, against the old layout. Saying
    //  "not found" would send someone to write a function that already
    //  exists; the manifest lets referee say which problem this is.
    EXPECT_NE(bad.output.find("different layout"), std::string::npos) << bad.output;
    EXPECT_NE(bad.output.find("expected"), std::string::npos)         << bad.output;
    EXPECT_NE(bad.output.find("found"), std::string::npos)            << bad.output;

    for (auto const* f : {"a.ref", "a.h", "a.c", "a.csv", "liba.so"})
        std::remove((dir + "/" + f).c_str());
    ::rmdir(dir.c_str());
}

// `--explain` writes a run trace. The check that matters is not that a file
// appears but that it satisfies the schema -- the schema is the contract
// between referee and every viewer, and prose cannot enforce it.
TEST(Cli, ExplainWritesASchemaValidRunTrace)
{
    auto    ref = std::string(REFEREE_TEST_DATA_DIR) + "/accumulate.ref";
    auto    csv = std::string(REFEREE_TEST_DATA_DIR) + "/accumulate.csv";
    auto    out = tmpPath("explain", ".ndjson");

    auto    r = run(quote(REFEREE_BIN) + " execute " + quote(ref) + " " + quote(csv)
                    + " --explain " + quote(out) + " 2>&1");
    EXPECT_EQ(r.status, 0) << r.output;

    std::ifstream       f(out);
    std::string         line;
    int                 header = 0, signals = 0, requirements = 0, withRows = 0;

    while (std::getline(f, line))
    {
        if (line.find("\"kind\":\"header\"")  != std::string::npos) header++;
        if (line.find("\"kind\":\"signal\"")  != std::string::npos) signals++;
        if (line.find("\"kind\":\"requirement\"") != std::string::npos)
        {
            requirements++;
            if (line.find("\"rows\":") != std::string::npos)
                withRows++;
        }

        //  One record per line, which is what makes the file streamable.
        EXPECT_NE(line.find('{'), std::string::npos) << line;
    }

    EXPECT_EQ(header, 1);
    EXPECT_GT(signals, 0);

    //  Bare requirements now carry their own per-state column, not just a
    //  verdict. accumulate.ref is all bare expressions, so all but a shadowed
    //  duplicate do -- two textually identical requirements at different lines
    //  intern to one node, and only the first keeps the column's name.
    //  Rdb.ExplainCarriesRequirementColumns checks the column's contents on a
    //  fixture without that quirk.
    EXPECT_GT(requirements, 0);
    EXPECT_GE(withRows, requirements - 1) << "bare requirements should carry a column";

    //  Sentinels bracket the real states internally and must not appear: the
    //  trace has eight rows, and a picture showing ten would show two states
    //  that were never captured.
    std::ifstream       g(out);
    std::getline(g, line);
    EXPECT_NE(line.find("\"states\":8"), std::string::npos) << line;
    EXPECT_EQ(line.find("-1"), std::string::npos) << line;

    std::remove(out.c_str());
}

// `--stub` emits a C skeleton implementing the declared functions. The point
// is that the first build is a copy rather than a transcription -- C cannot
// diagnose a signature mismatch -- so the test is that the generated pair
// compiles together, untouched, at -Wall -Wextra -Werror.
TEST(Cli, StubCompilesAgainstItsOwnHeader)
{
    auto    ref = tmpPath("stub", ".ref");
    {
        std::ofstream   f(ref);
        f << "type P : struct { x : integer; y : integer; };\n"
             "type K : enum { A, B };\n"
             //  a namespaced name, a struct by pointer, a slice, an enum
             "func std::math::hypot2 : (P) -> integer;\n"
             "func crc8              : (byte[], integer) -> byte;\n"
             "func kind              : (K) -> boolean;\n"
             "func nothing           : () -> boolean;\n";
    }

    auto    hdr = tmpPath("stub", ".h");
    auto    src = tmpPath("stub", ".c");

    EXPECT_EQ(run(quote(REFEREE_BIN) + " header " + quote(ref) + " -o " + quote(hdr)).status, 0);

    auto    r = run(quote(REFEREE_BIN) + " header " + quote(ref) + " --stub --header-name "
                    + quote(hdr) + " -o " + quote(src));
    EXPECT_EQ(r.status, 0) << r.output;

    auto    obj = tmpPath("stub", ".o");
    auto    c   = run("cc -c -std=c11 -Wall -Wextra -Werror " + quote(src)
                      + " -o " + quote(obj) + " 2>&1");
    EXPECT_EQ(c.status, 0) << c.output;

    std::remove(ref.c_str()); std::remove(hdr.c_str());
    std::remove(src.c_str()); std::remove(obj.c_str());
}

// The generated header is the only mechanism that makes an implementation and
// a specification agree -- C cannot diagnose a signature mismatch. So the test
// is not that the header looks right, but that a C compiler accepts it and
// that the layout it describes is the one referee computes.
TEST(Cli, HeaderCompilesAndMatchesTheLayout)
{
    auto    ref = tmpPath("hdr", ".ref");
    {
        std::ofstream   f(ref);
        f << "type Dir   : enum { M2S, S2M };\n"
             "type Pkt   : struct { flags : byte; len : byte; raw : byte[8]; kind : Dir; };\n"
             "type Point : struct { x : number; y : number; };\n"
             "func classify : (byte) -> Dir;\n"
             "func crc8     : (byte[], integer) -> byte;\n"
             "data d : Dir;\n";
    }

    auto    hdr = tmpPath("hdr", ".h");
    auto    r   = run(quote(REFEREE_BIN) + " header " + quote(ref) + " -o " + quote(hdr));
    EXPECT_EQ(r.status, 0) << r.output;

    //  Enums are one byte and 1-based, with 0 reserved for "matched nothing".
    //  A C enum written the obvious way would start at 0, putting every member
    //  off by one and colliding the first with the sentinel.
    auto    src = tmpPath("hdr", ".c");
    {
        std::ofstream   f(src);
        f << "#include \"" << hdr << "\"\n"
             "_Static_assert(sizeof(referee_Dir) == 1, \"one byte\");\n"
             "_Static_assert(referee_Dir_unknown == 0, \"0 is the sentinel\");\n"
             "_Static_assert(referee_Dir_M2S == 1, \"1-based\");\n"
             "_Static_assert(referee_Dir_S2M == 2, \"1-based\");\n"
             //  all members align 1: 1 + 1 + 8 + 1, no padding anywhere
             "_Static_assert(sizeof(referee_Pkt) == 11, \"packed by alignment\");\n"
             "_Static_assert(offsetof(referee_Pkt, raw) == 2, \"array offset\");\n"
             "_Static_assert(offsetof(referee_Pkt, kind) == 10, \"enum offset\");\n"
             "_Static_assert(sizeof(referee_Point) == 16, \"two doubles\");\n"
             //  the prototype must be satisfiable exactly as generated
             //  the descriptor is two words, count first
             "_Static_assert(sizeof(referee_slice_byte) == 16, \"two words\");\n"
             "_Static_assert(offsetof(referee_slice_byte, count) == 0, \"count first\");\n"
             "_Static_assert(offsetof(referee_slice_byte, data) == 8, \"data second\");\n"
             "referee_Dir referee_classify(uint8_t b) { return b ? referee_Dir_M2S : referee_Dir_S2M; }\n"
             "uint8_t referee_crc8(referee_slice_byte s, int64_t n) { return (uint8_t)(s.count + (size_t)n); }\n";
    }

    auto    obj = tmpPath("hdr", ".o");
    auto    c   = run("cc -c -std=c11 -Wall -Wextra -Werror " + quote(src) + " -o " + quote(obj) + " 2>&1");
    EXPECT_EQ(c.status, 0) << c.output;

    std::remove(ref.c_str()); std::remove(hdr.c_str());
    std::remove(src.c_str()); std::remove(obj.c_str());
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
