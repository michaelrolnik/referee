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
                            Sizes const& sizes = {},
                            bool embedSchema = false);

    /// A single diagnostic from `diagnose`: a parse or type error, positioned.
    /// Lines and columns are 0-based (LSP convention); the range is half-open.
    struct Diagnostic
    {
        unsigned    startLine = 0, startCol = 0;
        unsigned    endLine   = 0, endCol   = 0;
        std::string message;
    };

    /// Parse + type-check REF source from `is` WITHOUT lowering to LLVM, returning
    /// every diagnostic found (all syntax errors; then, on a clean parse, the first
    /// AST/type error). This is the language server's analysis entry — the cheap
    /// subset of `compile`. Arrays may stay unsized (`T[]`) since the editor holds
    /// no trace. `name` roots import resolution + labels, as in `compile`. Never throws.
    static std::vector<Diagnostic> diagnose(std::istream& is, std::string name,
                                            std::vector<std::string> const& includePaths = {});

    /// One member-completion candidate. `kind` is an LSP CompletionItemKind
    /// (5 = Field for a struct member, 20 = EnumMember for an enum case).
    struct Completion
    {
        std::string label;
        int         kind = 0;
    };

    /// Member completion for the `.` after a signal at (line, character), both
    /// 0-based (LSP). Parses the document — with the caret's own line blanked, so
    /// a half-typed `sig.` does not break the parse — resolves the dotted chain
    /// before the caret to a type, and returns that type's members: struct fields
    /// or enum cases. Empty if the head is not a signal or the type has no members.
    /// Never throws.
    static std::vector<Completion> complete(std::istream& is, std::string name,
                                            std::vector<std::string> const& includePaths,
                                            unsigned line, unsigned character);

    /// Hover text (Markdown) for the symbol at (line, character), both 0-based
    /// (LSP): the identifier — or dotted member chain — under the caret, rendered
    /// as its declaration (`data pt : Point`, a struct/enum body, a field's type).
    /// Empty when the caret is not on a resolvable name, or the document does not
    /// parse. Never throws.
    static std::string hover(std::istream& is, std::string name,
                             std::vector<std::string> const& includePaths,
                             unsigned line, unsigned character);

    /// The source range of a name's declaration, for go-to-definition. `found`
    /// is false when nothing was located; line/columns are 0-based (LSP) and name
    /// a same-document span (import targets are not followed).
    struct Definition
    {
        bool        found    = false;
        unsigned    line     = 0;
        unsigned    startCol = 0;
        unsigned    endCol   = 0;
    };

    /// Locate the declaration of the name — or dotted member — under the caret at
    /// (line, character), both 0-based (LSP). A top-level name resolves to its
    /// `data` / `conf` / `type` / `func` declaration; a member resolves to its
    /// field inside the owning `type`. Same-document only. Never throws.
    static Definition define(std::istream& is, std::string name,
                             std::vector<std::string> const& includePaths,
                             unsigned line, unsigned character);

    /// Compile `refPath` and emit a native object file to `outPath`, ready to
    /// be linked into an ahead-of-time checker. The object exports one symbol,
    /// `referee_module` (see `runtime/referee_checker.h`), and carries the
    /// compiled requirement functions -- no JIT, no LLVM on the consuming side.
    /// `triple` empty means the host; otherwise a target triple to cross-emit
    /// for. An unsized (`T[]`) array needs no trace: it is a runtime
    /// descriptor, so a specification compiles without one.
    static void     emitObject(std::string const& refPath,
                               std::string const& outPath,
                               std::string const& triple = {},
                               std::vector<std::string> const& includePaths = {});

    /// Emit a shared object: `emitObject` followed by a `cc -shared` link, so
    /// the result is `dlopen`-able and exports `referee_module`. The embeddable
    /// form -- a host loads it and drives the table, no LLVM on either side.
    /// Needs a C compiler on the build machine (not the checking one).
    static void     emitShared(std::string const& refPath,
                               std::string const& outPath,
                               std::string const& triple = {},
                               std::vector<std::string> const& includePaths = {});

    /// Emit a standalone executable: `emitObject` linked against the JIT-free
    /// runtime library and its driver, so the result validates `.rdb` traces
    /// with no LLVM, no ANTLR, and no `.ref`. The runtime library
    /// (`libreferee_rt.a`) is located via `$REFEREE_RT_DIR`, then next to the
    /// referee binary, then the build tree. Needs a C++ compiler on the build
    /// machine, not the checking one.
    static void     emitExecutable(std::string const& refPath,
                                   std::string const& outPath,
                                   std::string const& triple = {},
                                   std::vector<std::string> const& includePaths = {});

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
                                Sizes const& sizes = {},
                                bool allowUnsized = false);

    /// Emit a C header declaring the specification's named types and the
    /// prototypes of its `func` declarations, so an implementation compiles
    /// against the same layout referee uses rather than a transcription of it.
    /// The specification alone is enough: a header describes types and
    /// signatures, and an array parameter carries its extent at run time. A
    /// trace is optional, and only makes a difference for a named type whose
    /// member array is unsized -- there the extent is part of the C layout.
    static void     emitHeader(std::string const& refPath,
                               std::ostream& os,
                               std::vector<std::string> const& includePaths = {},
                               std::string const& tracePath = {});

    /// Emit a C skeleton implementing every `func` the specification declares:
    /// the header included, each signature written out, and a body to fill in.
    /// The signatures are generated from the same table the header and the
    /// symbol lookup use, so the first build is a copy rather than a
    /// transcription -- which is where a mismatch would otherwise be made,
    /// and C cannot diagnose one.
    static void     emitStub(std::string const& refPath,
                             std::string const& headerName,
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
                               std::vector<std::string> const& libraryPaths = {},
                               std::string const& explainPath = {});

    /// Run an already-built checker `.so` against traces, reporting exactly as
    /// `execute` does. Loads the object, checks each trace's schema against the
    /// one the checker carries, and drives the requirement table -- no
    /// compilation, no `.ref`. `.rdb` traces only for now.
    static bool     executeChecker(std::string const& soPath,
                                   std::vector<Trace> const& traces,
                                   std::ostream& os,
                                   Detail detail = Detail::Requirements,
                                   std::string const& confPath = {});

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
