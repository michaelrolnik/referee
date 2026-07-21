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

#include <CLI/App.hpp>
#include "CLI/Formatter.hpp"
#include "CLI/Config.hpp"

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
    auto    addIncludeOption = [&](CLI::App* sub) {
        sub->add_option("-I,--include", includePaths,
            "Directory to search for imported .ref files (repeatable)")
            ->check(CLI::ExistingDirectory);
    };

    // compile subcommand: emits LLVM IR to stdout
    std::string compileRef;
    auto        compile = app.add_subcommand("compile", "Compile REF file to LLVM IR");
    compile->add_option("reffile", compileRef, "REF file to compile")
        ->required()
        ->check(CLI::ExistingFile);
    addIncludeOption(compile);

    // execute subcommand
    std::string runRef;
    std::string runData;
    std::string runConf;
    auto        execute = app.add_subcommand(
        "execute",
        "Execute compiled REF specs against a CSV / YAML / RDB trace");
    execute
        ->add_option("reffile", runRef, "REF specification file")
        ->required()
        ->check(CLI::ExistingFile);
    execute
        ->add_option("datafile", runData,
            "Trace file (.csv / .yml / .yaml / .rdb)")
        ->required()
        ->check(CLI::ExistingFile);
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
        else if(app.got_subcommand("execute"))
        {
            // Dispatch on the datafile extension: `.rdb` is the packed
            // execute-ready slab; everything else goes through the CSV/YAML
            // tabular reader.
            auto    isRdb = runData.size() >= 4
                         && runData.compare(runData.size() - 4, 4, ".rdb") == 0;

            std::ifstream   refStream(runRef, std::ios_base::in);
            bool            allPass;
            if (isRdb)
            {
                allPass = Referee::executeRdb(refStream, runRef, runData,
                                              std::cout, includePaths);
            }
            else
            {
                std::ifstream   dataStream(runData, std::ios_base::in);
                std::ifstream   confStream;
                if (!runConf.empty())
                    confStream.open(runConf, std::ios_base::in);
                allPass = Referee::execute(
                    refStream, runRef,
                    dataStream, runData,
                    runConf.empty() ? nullptr : &confStream, runConf,
                    std::cout, includePaths);
            }
            if (!allPass) return 1;
        }
    }
    catch (const CLI::ParseError &e)
    {
        return app.exit(e);
    }
    catch(std::exception& e)
    {
        std::cerr << "exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
