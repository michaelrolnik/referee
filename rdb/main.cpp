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
#include "merge.hpp"
#include "loaders/row.hpp"

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>

#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
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

    //  merge: several sources, each sampling some of the signals at its own
    //  rate, folded into one complete-row trace and packed to .rdb.
    auto*   mergeCmd = app.add_subcommand(
        "merge",
        "Merge multi-rate trace sources into one .rdb (union of times, each signal held forward)");
    std::string                 mergeRef;
    std::vector<std::string>    mergeSources;
    std::string                 mergeConf;
    std::string                 mergeOut;
    std::string                 mergeLeading = "trim";
    std::string                 mergeOverlap = "error";
    std::vector<std::string>    mergeIncludePaths;
    mergeCmd->add_option("ref", mergeRef,
        "REF source whose data/conf declarations define the schema")
        ->required()->check(CLI::ExistingFile);
    mergeCmd->add_option("sources", mergeSources,
        "Two or more trace files (.csv / .yml / .yaml), each sampling some signals")
        ->required()->expected(-1)->check(CLI::ExistingFile);
    mergeCmd->add_option("--conf", mergeConf,
        "Optional configuration file (.csv / .yml / .yaml)")
        ->check(CLI::ExistingFile);
    mergeCmd->add_option("-o,--out", mergeOut, "Output .rdb path")->required();
    mergeCmd->add_option("--leading", mergeLeading,
        "Leading gap before a signal's first sample: trim | zero | backfill")
        ->check(CLI::IsMember({"trim", "zero", "backfill"}));
    mergeCmd->add_option("--overlap", mergeOverlap,
        "A column shared by two sources: error | merge")
        ->check(CLI::IsMember({"error", "merge"}));
    mergeCmd->add_option("-I,--include", mergeIncludePaths,
        "Directory to search for imported .ref files (repeatable)")
        ->check(CLI::ExistingDirectory);

    try
    {
        app.parse(argc, argv);

        if (buildCmd->parsed())
            referee::db::ingest(refFile, dataFile, confFile, outFile, includePaths);
        else if (dumpCmd->parsed())
            referee::db::dump(dumpFile, std::cout);
        else if (mergeCmd->parsed())
        {
            if (mergeSources.size() < 2)
                throw std::runtime_error("merge: give at least two sources");

            //  Each source becomes a `loader::Row` mergeTraces reads by name.
            //  CSV/YAML open directly; a `.rdb` is already-packed binary, so it
            //  is decoded back to a flat CSV in memory first and wrapped the
            //  same way -- a `.rdb` stands in anywhere a CSV does.
            auto    isRdb = [](std::string const& p)
            {
                return p.size() >= 4 && p.substr(p.size() - 4) == ".rdb";
            };

            std::vector<std::unique_ptr<std::istream>>      streams;
            std::vector<std::unique_ptr<loader::Row>>       owned;
            std::vector<loader::Row*>                       docs;
            for (auto const& path : mergeSources)
            {
                if (isRdb(path))
                {
                    referee::db::Reader     rdb(path);
                    auto    csv = std::make_unique<std::stringstream>();
                    referee::db::toCsv(rdb, *csv);
                    owned.push_back(loader::Row::open(*csv, path + ".csv"));
                    streams.push_back(std::move(csv));
                }
                else
                {
                    auto    in = std::make_unique<std::ifstream>(path);
                    if (!*in)
                        throw std::runtime_error("merge: cannot open '" + path + "'");
                    owned.push_back(loader::Row::open(*in, path));
                    streams.push_back(std::move(in));
                }
                docs.push_back(owned.back().get());
            }

            auto    leading = mergeLeading == "zero"     ? referee::db::LeadingGap::Zero
                            : mergeLeading == "backfill"  ? referee::db::LeadingGap::Backfill
                            :                               referee::db::LeadingGap::Trim;
            auto    overlap = mergeOverlap == "merge"     ? referee::db::Overlap::Merge
                            :                               referee::db::Overlap::Error;

            auto    merged  = referee::db::mergeTraces(docs, leading, overlap);

            //  The merged table is a CSV in memory; ingest turns it into the
            //  .rdb, deriving the schema and column order from the .ref. It is
            //  named `.csv` so the ingestor picks the CSV loader for it.
            std::ifstream       ref(mergeRef);
            if (!ref)
                throw std::runtime_error("merge: cannot open '" + mergeRef + "'");

            std::istringstream  data(merged);
            std::ifstream       conf;
            if (!mergeConf.empty())
            {
                conf.open(mergeConf);
                if (!conf)
                    throw std::runtime_error("merge: cannot open '" + mergeConf + "'");
            }

            std::ofstream       out(mergeOut, std::ios::binary);
            if (!out)
                throw std::runtime_error("merge: cannot write '" + mergeOut + "'");

            referee::db::ingest(ref,  mergeRef,
                                data, "merged.csv",
                                mergeConf.empty() ? nullptr : &conf, mergeConf,
                                out,  mergeIncludePaths);
        }
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
