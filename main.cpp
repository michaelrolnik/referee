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
#include "referee.hpp"

#include <spdlog/spdlog.h>
#include "spdlog/fmt/fmt.h"
#include <spdlog/fmt/ostr.h>

#include <vector>
#include <algorithm>
#include <sstream>
#include <utility>

#include <CLI/App.hpp>
#include "CLI/Formatter.hpp"
#include "CLI/Config.hpp"


namespace {

//  CLI11 reports an unrecognised option as
//
//      The following arguments were not expected: --link
//
//  which is no help at all when the cause is a typo. Collect every long option
//  the program knows and suggest the nearest one.
std::size_t     editDistance(std::string const& a, std::string const& b)
{
    std::vector<std::size_t>    prev(b.size() + 1), curr(b.size() + 1);

    for (std::size_t j = 0; j <= b.size(); j++) prev[j] = j;

    for (std::size_t i = 1; i <= a.size(); i++)
    {
        curr[0] = i;
        for (std::size_t j = 1; j <= b.size(); j++)
            curr[j] = std::min({ prev[j] + 1, curr[j - 1] + 1,
                                 prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1) });
        prev = curr;
    }

    return prev[b.size()];
}

//  Scanning argv rather than CLI11's message, because the message does not
//  always contain the offending token: a subcommand with a multi-value
//  positional absorbs an unknown flag and then complains that a *file* does
//  not exist, naming the value rather than the flag.
void    suggestOptions(CLI::App const& app, int argc, char** argv)
{
    //  Every long name, from the app and one level of subcommands -- which is
    //  as deep as this CLI goes.
    //  Match against every spelling, including aliases, but suggest the
    //  canonical one -- steering a user to an alias teaches the wrong name.
    std::vector<std::pair<std::string, std::string>> known;
    auto                        collect = [&](CLI::App const& sub) {
        for (auto const* opt : sub.get_options())
        {
            auto const& names = opt->get_lnames();
            if (names.empty()) continue;

            for (auto const& n : names)
                known.emplace_back("--" + n, "--" + names.front());
        }
    };

    collect(app);
    for (auto const* sub : app.get_subcommands({}))
        collect(*sub);

    for (int i = 1; i < argc; i++)
    {
        std::string token = argv[i];

        if (token == "--")                          //  end of options
            break;

        if (token.size() < 3 || token.compare(0, 2, "--") != 0)
            continue;

        token = token.substr(0, token.find('='));   //  --opt=value

        //  Spelled correctly: nothing to say.
        if (std::any_of(known.begin(), known.end(),
                        [&](auto const& k) { return k.first == token; }))
            continue;

        std::string     best;
        std::size_t     bestDist = 3;               //  suggest only a near miss

        for (auto const& [spelling, canonical] : known)
        {
            auto    d = editDistance(token, spelling);
            if (d < bestDist) { bestDist = d; best = canonical; }
        }

        if (!best.empty())
            std::cerr << "did you mean " << best << "?\n";
    }
}

} // namespace

int main(int argc, char * argv[])
{
    CLI::App    app("referee");
    app.require_subcommand(1);

    bool        flDebug     = false;

    app.add_flag("--debug", flDebug, "produce debug logs");

    // Extra directories searched for `import` targets that are not found next
    // to the file importing them. Repeatable, searched in the order given.
    // Registered per subcommand rather than on the app so that it can be given
    // after the subcommand name, which is where users expect to type it.
    std::vector<std::string>    includePaths;
    // Directories scanned for the .so objects backing `func` declarations.
    // Same shape as -I, and inert unless the specification declares a func.
    std::vector<std::string>    libraryPaths;
    auto    addIncludeOption = [&](CLI::App* sub) {
        sub->add_option("-L,--library", libraryPaths,
            "A .so implementing `func` declarations, or a directory of them (repeatable)")
            ->allow_extra_args(false)
            ->check(CLI::ExistingPath);
        sub->add_option("-I,--include", includePaths,
            "Directory to search for imported .ref files (repeatable)")
            // One directory per occurrence, as a compiler's -I does. Left
            // greedy it would swallow the trace positionals that follow it.
            ->allow_extra_args(false)
            ->check(CLI::ExistingDirectory);
    };

    // compile subcommand: emits LLVM IR to stdout
    std::string compileRef;
    auto        compile = app.add_subcommand("compile", "Compile REF file to LLVM IR");
    compile->add_option("reffile", compileRef, "REF file to compile")
        ->required()
        ->check(CLI::ExistingFile);
    addIncludeOption(compile);

    // header subcommand: emits a C header for the specification's named types
    // and its `func` prototypes. Takes an optional trace, because a struct
    // holding an unsized array has a trace-dependent C type.
    std::string headerRef, headerLike, headerOut;
    auto        header = app.add_subcommand(
        "header", "Emit a C header for the specification's types and functions");
    header->add_option("reffile", headerRef, "REF specification file")
        ->required()
        ->check(CLI::ExistingFile);
    //  `--like` was the original spelling and is kept working, but `--trace`
    //  says what it is -- and in a feature whose other flag loads .so objects,
    //  a name one keystroke from `--link` invites exactly the wrong reading.
    header->add_option("--trace,--like", headerLike,
        "Trace whose array extents the header is generated against")
        ->check(CLI::ExistingFile);
    header->add_option("-o,--output", headerOut, "Write here instead of stdout");
    addIncludeOption(header);

    // execute subcommand
    std::string runRef;
    std::vector<std::string> runData;
    std::string runConf;
    auto        execute = app.add_subcommand(
        "execute",
        "Execute compiled REF specs against a CSV / YAML / RDB trace");
    execute
        ->add_option("reffile", runRef, "REF specification file")
        ->required()
        ->check(CLI::ExistingFile);
    // Traces given bare are expected to satisfy the specification, which is
    // what `referee execute spec.ref trace.csv` has always meant.
    std::vector<std::string>    runSuccess;
    std::vector<std::string>    runFailure;
    int                         runVerbose = -1;

    execute
        ->add_option("datafile", runData,
            "Trace files expected to pass (.csv / .yml / .yaml / .rdb)")
        ->check(CLI::ExistingFile);
    execute
        ->add_option("--success", runSuccess,
            "Traces that must satisfy the specification")
        ->check(CLI::ExistingFile);
    // A corpus that must be *rejected* is what keeps a specification from
    // going vacuous; a trace here that passes is a failure of the run.
    execute
        ->add_option("--failure", runFailure,
            "Traces that must violate the specification")
        ->check(CLI::ExistingFile);
    std::string                 runSuite;
    execute
        ->add_option("--suite", runSuite,
            "Manifest of traces and what each is expected to do")
        ->check(CLI::ExistingFile);
    execute
        ->add_option("-v,--verbose", runVerbose,
            "0 = closing summary only, 1 = a line per trace, 2 = requirements too")
        ->check(CLI::Range(0, 2));
    execute
        ->add_option("--conf", runConf,
            "Conf file (.csv / .yml / .yaml); not used when datafile is .rdb")
        ->check(CLI::ExistingFile);
    addIncludeOption(execute);

    try {
        app.parse(argc, argv);

        if(flDebug)
            spdlog::set_level(spdlog::level::debug);

        if(app.got_subcommand("compile"))
        {
            std::ifstream   is(compileRef, std::ios_base::in);
            if (!Referee::printIR(is, compileRef, std::cout, includePaths)) return 1;
        }
        else if(app.got_subcommand("header"))
        {
            if (headerOut.empty())
            {
                Referee::emitHeader(headerRef, std::cout, includePaths, headerLike);
            }
            else
            {
                std::ofstream   os(headerOut, std::ios_base::out | std::ios_base::trunc);
                if (!os) { std::cerr << "cannot write " << headerOut << "\n"; return 1; }
                Referee::emitHeader(headerRef, os, includePaths, headerLike);
            }
        }
        else if(app.got_subcommand("execute"))
        {
            std::vector<Referee::Trace>     traces;
            if (!runSuite.empty())
                traces = Referee::readSuite(runSuite);
            for (auto const& p : runData)    traces.push_back({p, false});
            for (auto const& p : runSuccess) traces.push_back({p, false});
            for (auto const& p : runFailure) traces.push_back({p, true});

            if (traces.empty())
            {
                std::cerr << "referee: no trace given\n";
                return 1;
            }

            // Default to full detail for a lone trace, since there is no
            // volume problem and the requirement lines are the useful output;
            // to a line per trace for a corpus, where they are not.
            auto    detail = runVerbose >= 0
                           ? static_cast<Referee::Detail>(runVerbose)
                           : (traces.size() == 1 ? Referee::Detail::Requirements
                                                 : Referee::Detail::Traces);

            std::ifstream   refStream(runRef, std::ios_base::in);
            bool            allPass = Referee::executeAll(
                                refStream, runRef, traces, runConf,
                                std::cout, detail, includePaths, libraryPaths);
            if (!allPass) return 1;
        }
    }
    catch (const CLI::ParseError &e)
    {
        auto    status = app.exit(e);
        suggestOptions(app, argc, argv);
        return status;
    }
    catch(std::exception& e)
    {
        std::cerr << "exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
