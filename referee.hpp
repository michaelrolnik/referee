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
    static Compiled compile(std::istream& is, std::string name,
                            llvm::DataLayout const* dataLayout = nullptr,
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
                                std::vector<std::string> const& includePaths = {});

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
