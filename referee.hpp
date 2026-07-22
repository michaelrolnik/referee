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

#pragma once

#include <iostream>
#include <memory>
#include <map>
#include <string>
#include <vector>

namespace llvm {
    class LLVMContext;
    class Module;
    class DataLayout;
}

class Antlr2AST;
class Module;       // AST module (core/module.hpp)

class Referee
{
public:
    /// Result of a single REF compilation: an optimized LLVM module ready to
    /// hand to an LLJIT, plus the AST that produced it. The Antlr2AST owner
    /// keeps the AST nodes alive for as long as `ast` is dereferenced — so
    /// keep the Compiled value around until the JIT (or whatever consumer)
    /// is done with both the IR module and the AST.
    struct Compiled
    {
        std::unique_ptr<llvm::LLVMContext>  ctx;        ///< owns the IR
        std::unique_ptr<llvm::Module>       mod;        ///< optimized IR
        std::unique_ptr<Antlr2AST>          astOwner;   ///< owns AST nodes
        ::Module*                           ast = nullptr;  ///< raw view

        Compiled();
        Compiled(Compiled&&) noexcept;
        Compiled& operator=(Compiled&&) noexcept;
        ~Compiled();

        Compiled(Compiled const&)            = delete;
        Compiled& operator=(Compiled const&) = delete;
    };

    /// Parse REF source from `is`, lower it to LLVM IR, and run the standard
    /// O2 optimisation pipeline. Returns the optimized module (and the AST
    /// it was derived from) ready to be either dumped, JITed, or post-processed.
    ///
    /// `dataLayout` controls struct/aggregate layout in the produced IR:
    ///   * pass `&jit->getDataLayout()` when the result will be added to that
    ///     LLJIT — guarantees the JIT sees IR that matches its own layout;
    ///   * pass `nullptr` to use the host-default data layout (suitable for
    ///     plain IR dumps and for round-tripping through `llvm::parseIR`).
    ///
    /// Throws on parse / type / lowering errors.
    /// `includePaths` are searched, in order, when an `import` target is not
    /// found next to the file that imported it. `name` is used as the root
    /// file's own path, so relative imports resolve against its directory and
    /// requirement labels are recorded relative to it.
    /// Extents for arrays declared `T[]`, keyed by declaration name and
    /// ordered outermost-first. See `Antlr2AST::Sizes`.
    using Sizes = std::map<std::string, std::vector<unsigned>>;

    static Compiled compile(std::istream& is, std::string name,
                            llvm::DataLayout const* dataLayout = nullptr,
                            std::vector<std::string> const& includePaths = {},
                            Sizes const& sizes = {});

    /// Convenience wrapper around `compile()` that prints the resulting IR
    /// to `os`. Catches and reports exceptions to stderr. Returns true on
    /// success, false if compilation failed.
    static bool     printIR(std::istream& is, std::string name,
                            std::ostream& os = std::cout,
                            std::vector<std::string> const& includePaths = {});

    /// Compile REF source, JIT it, and evaluate every requirement against
    /// the CSV trace in `csvStream` (and the optional configuration in
    /// `confStream`). Writes one PASS / FAIL / ERROR line per requirement
    /// to `os`. Returns true iff every requirement passed.
    static bool     execute(std::istream& refStream, std::string refName,
                            std::istream& csvStream,  std::string csvName,
                            std::istream* confStream = nullptr,
                            std::string   confName   = "",
                            std::ostream& os         = std::cout,
                            std::vector<std::string> const& includePaths = {});

    /// Cheap, LLVM-free parse of REF source into an AST `::Module`. Useful
    /// for tooling (RDB ingestors, dumpers, IDE plumbing) that needs the
    /// `data` / `conf` / `type` declarations but not the lowered IR.
    ///
    /// Throws on parse errors. The returned `astOwner` keeps the AST nodes
    /// alive for as long as the caller keeps the value around; `ast` is
    /// just a borrowed view into it.
    struct Schema
    {
        std::unique_ptr<Antlr2AST>  astOwner;
        ::Module*                   ast = nullptr;

        Schema();
        Schema(Schema&&) noexcept;
        Schema& operator=(Schema&&) noexcept;
        ~Schema();

        Schema(Schema const&)            = delete;
        Schema& operator=(Schema const&) = delete;
    };
    static Schema   parseSchema(std::istream& is, std::string name,
                                std::vector<std::string> const& includePaths = {},
                                Sizes const& sizes = {});

    /// Emit a C header declaring the specification's named types and the
    /// prototypes of its `func` declarations, so an implementation compiles
    /// against the same layout referee uses rather than a transcription of it.
    /// `sizes` resolves `T[]` extents, which is why the caller may need a
    /// trace: a struct holding an unsized array has a trace-dependent C type.
    static void     emitHeader(std::string const& refPath,
                               std::ostream& os,
                               std::vector<std::string> const& includePaths = {},
                               std::string const& tracePath = {});

    /// One trace to check, and whether it is expected to violate the
    /// specification. A corpus of traces that *must* be rejected is how a
    /// specification is kept from going vacuous: a requirement mistyped into
    /// triviality passes every trace exactly as convincingly as a correct one.
    struct Trace
    {
        std::string path;
        bool        expectFailure = false;

        /// Which requirements this trace must violate, by `@name` or by
        /// label. Empty means "some requirement, any of them" -- the weaker
        /// claim, satisfied by the trace failing for a reason nobody intended.
        /// Naming them is what keeps a corpus honest: a trace that starts
        /// failing for a different reason is then a failure, not a pass.
        std::vector<std::string>    violates;
    };

    /// How much to print. The exit code carries the verdict either way, so
    /// these only affect what a human or a log gets to read.
    enum class Detail
    {
        Summary      = 0,   ///< one closing line; detail only for misbehaviour
        Traces       = 1,   ///< a line per trace, saying what it did
        Requirements = 2,   ///< the requirement table for every trace too
    };

    /// Read a corpus from a manifest: one trace per line, saying what it is
    /// meant to do. Paths are resolved relative to the manifest, so a suite
    /// can be committed and moved as a unit.
    ///
    ///     # comments and blank lines are ignored
    ///     good/nominal.csv      passes
    ///     bad/stuck-valve.csv   fails
    ///     bad/late-alarm.yml    fails  door-closes-in-2s, alarm-within-5s
    ///
    /// A bare `fails` is the weak claim -- satisfied by the trace violating
    /// anything at all. Naming requirements after it is the useful form.
    static std::vector<Trace>   readSuite(std::string const& manifestPath);

    /// Compile the specification once and check every trace with it.
    ///
    /// Compilation dominates: roughly 700ms for a 233-requirement spec against
    /// ~0.12ms per trace row, so checking a hundred traces one invocation at a
    /// time costs seventy seconds where this costs one.
    ///
    /// Returns true iff every trace behaved as declared -- a `expectFailure`
    /// trace that passes is a failure of the run, since it means the
    /// specification has stopped catching what that trace demonstrates.
    ///
    /// `confPath` may be empty, and is shared by every CSV/YAML trace.
    static bool     executeAll(std::istream& refStream, std::string refName,
                               std::vector<Trace> const& traces,
                               std::string const& confPath,
                               std::ostream& os     = std::cout,
                               Detail        detail = Detail::Requirements,
                               std::vector<std::string> const& includePaths = {},
                               std::vector<std::string> const& libraryPaths = {});

    /// Compile REF source, JIT it, and evaluate every requirement against
    /// a packed `.rdb` trace whose state buffer is *already* the layout the
    /// JIT consumes — only pointer fix-up happens at load time. The
    /// `.rdb`'s embedded schema must be structurally identical to the AST's
    /// `data` / `conf` declarations; otherwise execute throws.
    static bool     executeRdb(std::istream& refStream, std::string refName,
                               std::string const& rdbPath,
                               std::ostream& os = std::cout,
                               std::vector<std::string> const& includePaths = {});
};
