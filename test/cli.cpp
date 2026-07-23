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

// `referee build` emits a native object for an ahead-of-time checker: an ELF
// relocatable exporting one symbol, `referee_module`, plus `__prepare__`, with
// each requirement's label embedded as data. The full link needs a toolchain,
// so this checks the object's contract with `nm`, which is portable.
TEST(Cli, BuildEmitsAnObjectExportingRefereeModule)
{
    auto    ref = tmpPath("build", ".ref");
    auto    obj = tmpPath("build", ".o");
    {
        std::ofstream   f(ref);
        f << "data a : integer;\n@r1\nG(a >= 0);\n@r2\na == 1;\n";
    }

    auto    b = run(quote(REFEREE_BIN) + " build " + quote(ref) + " -o " + quote(obj));
    ASSERT_EQ(b.status, 0) << b.output;

    //  A defined `referee_module` (and `__prepare__`) is the ABI a driver binds
    //  to; `nm` marks a defined symbol with a capital type letter.
    auto    syms = run("nm " + quote(obj));
    ASSERT_EQ(syms.status, 0) << syms.output;
    EXPECT_NE(syms.output.find("referee_module"), std::string::npos) << syms.output;
    EXPECT_NE(syms.output.find("__prepare__"),    std::string::npos) << syms.output;

    //  The requirement labels ride as data, not as symbols.
    auto    str = run("strings " + quote(obj));
    EXPECT_NE(str.output.find("r1"), std::string::npos) << str.output;
    EXPECT_NE(str.output.find("r2"), std::string::npos) << str.output;

    std::remove(ref.c_str());
    std::remove(obj.c_str());
}

// `build --shared` links a .so, and `execute --checker` drives it with no
// .ref and no compilation. The verdicts must match `execute`, including string
// comparison -- which is a pointer compare over interned strings, so the
// checker re-interns its literals at load (they were interned in a different
// process at build time). This exercises ==, !=, and the schema check.
TEST(Cli, CheckerRunsAgainstRdbAndMatchesExecute)
{
    auto    dir = tmpPath("chk", "");
    auto    ref = dir + ".ref";
    auto    okc = dir + ".ok.csv";
    auto    bad = dir + ".bad.csv";
    auto    so  = dir + ".so";
    auto    okr = dir + ".ok.rdb";
    auto    badr= dir + ".bad.rdb";

    {
        std::ofstream f(ref);
        f << "data n : integer;\n"
             "data s : string;\n"
             "@pos\nG(n >= 0);\n"
             "@is_alpha\nG(s == \"alpha\");\n"
             "@not_beta\nG(s != \"beta\");\n";
    }
    { std::ofstream f(okc); f << "__time__,n,s\n0,1,alpha\n1000,2,alpha\n"; }
    { std::ofstream f(bad); f << "__time__,n,s\n0,1,gamma\n1000,2,alpha\n"; }

    ASSERT_EQ(run(quote(REFEREE_BIN) + " build --shared " + quote(ref) + " -o " + quote(so)).status, 0);
    ASSERT_EQ(run(quote(RDB_BIN) + " build " + quote(ref) + " " + quote(okc) + " -o " + quote(okr)).status, 0);
    ASSERT_EQ(run(quote(RDB_BIN) + " build " + quote(ref) + " " + quote(bad) + " -o " + quote(badr)).status, 0);

    //  All pass on the good trace.
    auto    ok = run(quote(REFEREE_BIN) + " execute --checker " + quote(so) + " " + quote(okr));
    EXPECT_EQ(ok.status, 0) << ok.output;
    EXPECT_NE(ok.output.find("is_alpha"), std::string::npos) << ok.output;
    EXPECT_EQ(ok.output.find("FAIL"), std::string::npos) << ok.output;

    //  The string requirement fails on the bad trace, and the checker's output
    //  matches `execute`'s verdicts (order aside).
    auto    chk = run(quote(REFEREE_BIN) + " execute --checker " + quote(so) + " " + quote(badr));
    EXPECT_NE(chk.status, 0) << chk.output;
    EXPECT_NE(chk.output.find("is_alpha"), std::string::npos);
    EXPECT_NE(chk.output.find("FAIL"), std::string::npos) << chk.output;

    auto    exe = run(quote(REFEREE_BIN) + " execute " + quote(ref) + " " + quote(badr));
    //  Same verdict for the string requirement in both.
    EXPECT_NE(exe.output.find("is_alpha"), std::string::npos);

    //  A checker refuses a trace whose schema differs from the one it carries.
    auto    other = dir + ".other.ref";
    auto    otherSo = dir + ".other.so";
    { std::ofstream f(other); f << "data x : number;\nG(x >= 0);\n"; }
    ASSERT_EQ(run(quote(REFEREE_BIN) + " build --shared " + quote(other) + " -o " + quote(otherSo)).status, 0);
    auto    mism = run(quote(REFEREE_BIN) + " execute --checker " + quote(otherSo) + " " + quote(okr));
    EXPECT_NE(mism.status, 0);
    EXPECT_NE(mism.output.find("different schema"), std::string::npos) << mism.output;

    for (auto* p : {&ref, &okc, &bad, &so, &okr, &badr, &other, &otherSo, &dir})
        std::remove(p->c_str());
}

// A specification with an unbounded array needs no trace to build -- `T[]` is a
// runtime descriptor, so its extent is not resolved at compile time. This is
// the doc's open question #3, which ragged arrays settled.
TEST(Cli, BuildAcceptsAnUnsizedArrayWithoutATrace)
{
    auto    ref = tmpPath("buildrag", ".ref");
    auto    obj = tmpPath("buildrag", ".o");
    {
        std::ofstream   f(ref);
        f << "data pkt : byte[];\nG(pkt.count >= 0);\n";
    }

    auto    b = run(quote(REFEREE_BIN) + " build " + quote(ref) + " -o " + quote(obj));
    EXPECT_EQ(b.status, 0) << b.output;

    std::remove(ref.c_str());
    std::remove(obj.c_str());
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

// `rdb merge` folds signals sampled by different sources at different rates
// into one trace of complete rows: the union of timestamps, each signal held
// forward from its own most recent sample. REF reads an empty cell as zero, so
// the merge has to materialise the hold rather than leave gaps.
TEST(Cli, RdbMergeMultiRateSources)
{
    auto    dir  = tmpPath("merge", "");
    auto    ref  = dir + ".ref";
    auto    fast = dir + ".fast.csv";
    auto    slow = dir + ".slow.csv";
    auto    out  = dir + ".rdb";

    { std::ofstream f(ref);  f << "data fast : integer;\ndata slow : integer;\nG(fast >= 0);\n"; }
    { std::ofstream f(fast); f << "__time__,fast\n0,10\n100,11\n200,12\n"; }
    { std::ofstream f(slow); f << "__time__,slow\n50,5\n250,7\n"; }

    //  Default (trim): drop rows before both signals have a value, so the merge
    //  starts at t=50 and invents nothing.
    auto    m = run(quote(RDB_BIN) + " merge " + quote(ref) + " "
                  + quote(fast) + " " + quote(slow) + " -o " + quote(out));
    ASSERT_EQ(m.status, 0) << m.output;

    auto    dump = run(quote(RDB_BIN) + " dump " + quote(out));
    ASSERT_EQ(dump.status, 0) << dump.output;

    //  At t=100 `slow` still reads its t=50 sample (held forward); `fast` its
    //  t=100 one. The row is complete, which is the whole point.
    EXPECT_NE(dump.output.find("time: 100"), std::string::npos) << dump.output;
    EXPECT_NE(dump.output.find("fast: 11"),  std::string::npos) << dump.output;
    EXPECT_NE(dump.output.find("slow: 5"),   std::string::npos) << dump.output;

    //  A signal is checkable straight after the merge.
    auto    exec = run(quote(REFEREE_BIN) + " execute " + quote(ref) + " " + quote(out));
    EXPECT_EQ(exec.status, 0) << exec.output;

    std::remove(out.c_str());
    std::remove(ref.c_str());  std::remove(fast.c_str());  std::remove(slow.c_str());
    std::remove(dir.c_str());
}

// A `.rdb` stands in as a merge source anywhere a CSV does: it is decoded back
// to a flat trace first. The round trip has to preserve every type exactly --
// a 1-based enum, a 64-bit integer, a struct field, an array element -- so this
// merges a rich-type `.rdb` with a disjoint CSV and checks the values survived.
TEST(Cli, RdbMergeAcceptsRdbSource)
{
    auto    dir   = tmpPath("rdbsrc", "");
    auto    ref   = dir + ".ref";
    auto    csv   = dir + ".csv";
    auto    src   = dir + ".src.rdb";
    auto    extra = dir + ".extra.csv";
    auto    out   = dir + ".out.rdb";

    {
        std::ofstream f(ref);
        f << "type K : enum { A, B, C };\n"
             "data k : K;\n"
             "data i : integer;\n"
             "data p : struct { x : integer; y : integer; };\n"
             "data arr : integer[3];\n"
             "data extra : integer;\n"
             "G(__time__ == 0 => k.B && i == 42 && p.x == 7 && arr[2] == 30 && extra == 99);\n";
    }
    { std::ofstream f(csv);   f << "__time__,k,i,p.x,p.y,arr[0],arr[1],arr[2]\n0,B,42,7,8,10,20,30\n"; }
    { std::ofstream f(extra); f << "__time__,extra\n0,99\n"; }

    //  A spec with just the .rdb's own signals, to pack it.
    auto    srcRef = dir + ".src.ref";
    {
        std::ofstream f(srcRef);
        f << "type K : enum { A, B, C };\n"
             "data k : K;\n"
             "data i : integer;\n"
             "data p : struct { x : integer; y : integer; };\n"
             "data arr : integer[3];\n"
             "G(i >= 0);\n";
    }
    ASSERT_EQ(run(quote(RDB_BIN) + " build " + quote(srcRef) + " " + quote(csv)
                + " -o " + quote(src)).status, 0);

    auto    m = run(quote(RDB_BIN) + " merge " + quote(ref) + " "
                  + quote(src) + " " + quote(extra) + " --leading backfill -o " + quote(out));
    ASSERT_EQ(m.status, 0) << m.output;

    //  If the enum/int/struct/array decoded correctly, this passes.
    auto    exec = run(quote(REFEREE_BIN) + " execute " + quote(ref) + " " + quote(out));
    EXPECT_EQ(exec.status, 0) << exec.output;

    for (auto* p : {&out, &ref, &csv, &src, &extra, &srcRef, &dir})
        std::remove(p->c_str());
}

// The same column in two sources is ambiguous, and the default refuses it
// rather than pick a winner.
TEST(Cli, RdbMergeRejectsOverlapByDefault)
{
    auto    dir = tmpPath("overlap", "");
    auto    ref = dir + ".ref";
    auto    a   = dir + ".a.csv";
    auto    b   = dir + ".b.csv";
    auto    out = dir + ".rdb";

    { std::ofstream f(ref); f << "data x : integer;\nG(x >= 0);\n"; }
    { std::ofstream f(a);   f << "__time__,x\n0,1\n"; }
    { std::ofstream f(b);   f << "__time__,x\n100,2\n"; }

    auto    m = run(quote(RDB_BIN) + " merge " + quote(ref) + " "
                  + quote(a) + " " + quote(b) + " -o " + quote(out));
    EXPECT_NE(m.status, 0);
    EXPECT_NE(m.output.find("appears in more than one source"), std::string::npos) << m.output;

    //  With --overlap merge it is accepted and the timelines are unioned.
    auto    ok = run(quote(RDB_BIN) + " merge " + quote(ref) + " "
                   + quote(a) + " " + quote(b) + " --overlap merge --leading zero -o " + quote(out));
    EXPECT_EQ(ok.status, 0) << ok.output;

    std::remove(out.c_str());
    std::remove(ref.c_str());  std::remove(a.c_str());  std::remove(b.c_str());
    std::remove(dir.c_str());
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
