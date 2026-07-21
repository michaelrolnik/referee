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

#include "database.hpp"
#include "ingest.hpp"

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char* argv[])
{
    CLI::App    app("rdb — referee database tool");
    app.require_subcommand(1);

    auto*   buildCmd = app.add_subcommand(
        "build",
        "Pack a CSV/YAML trace into a .rdb file using the schema in a .ref");
    std::string     refFile;
    std::string     dataFile;
    std::string     confFile;
    std::string     outFile;
    buildCmd->add_option("ref",      refFile,
        "REF source whose data/conf declarations define the schema")
        ->required()->check(CLI::ExistingFile);
    buildCmd->add_option("data",     dataFile,
        "Trace file (.csv / .yml / .yaml)")
        ->required()->check(CLI::ExistingFile);
    buildCmd->add_option("--conf",   confFile,
        "Optional configuration file (.csv / .yml / .yaml)")
        ->check(CLI::ExistingFile);
    buildCmd->add_option("-o,--out", outFile,
        "Output .rdb path")->required();
    // The schema parse resolves `import` the same way `referee` does, so a
    // spec split across files packs identically to an inline one.
    std::vector<std::string>    includePaths;
    buildCmd->add_option("-I,--include", includePaths,
        "Directory to search for imported .ref files (repeatable)")
        ->allow_extra_args(false)
        ->check(CLI::ExistingDirectory);

    auto*   dumpCmd = app.add_subcommand(
        "dump",
        "Pretty-print a .rdb file using its embedded schema");
    std::string     dumpFile;
    dumpCmd->add_option("rdb", dumpFile, "Input .rdb path")
        ->required()->check(CLI::ExistingFile);

    try
    {
        app.parse(argc, argv);

        if (buildCmd->parsed())
            referee::db::ingest(refFile, dataFile, confFile, outFile, includePaths);
        else if (dumpCmd->parsed())
            referee::db::dump(dumpFile, std::cout);
    }
    catch (CLI::ParseError const& e)
    {
        return app.exit(e);
    }
    catch (std::exception const& e)
    {
        std::cerr << "rdb: error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
