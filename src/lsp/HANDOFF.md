# `referee-lsp` — Language Server Handoff

Status: **complete and feature-full.** `referee-lsp` gives `.ref` files the full set of
analysis-backed IDE features. Everything is committed, tested, and shipping in the VS Code
extension. **Zero changes to `core/`** — the whole feature set was built on additive surfaces so
it never collides with concurrent work on the compiler/monitor.

Last verified: builds clean (`ninja -C build referee-lsp`), 37 LSP-facing tests green.

---

## Features (all implemented)

| Feature | LSP method | What it does |
|---|---|---|
| Diagnostics | `publishDiagnostics` | live parse + type errors (via `Referee::diagnose`) |
| Completion | `textDocument/completion` | bare identifier → names in scope (signals/confs/types/funcs, imports folded in) + keywords; after `.` → the type's struct fields / enum cases |
| Hover | `textDocument/hover` | a name's declaration: `data pt : Point`, a struct/enum body, a field's type |
| Go-to-definition | `textDocument/definition` | name → its `data`/`conf`/`type`/`func` decl; member → its field in the owning `type`. **Follows `import`s across files.** |
| Document symbols | `textDocument/documentSymbol` | outline/breadcrumbs; struct fields & enum cases nested under their type |
| Find-references | `textDocument/references` | every use across the doc + imports, comments excluded; **type-aware** |
| Rename | `textDocument/rename` + `prepareRename` | rewrite a name + all uses across imports; new identifier validated; **type-aware** |
| Signature help | `textDocument/signatureHelp` | parameters of the enclosing `func` or `std::…` built-in, active param tracked, overloads listed |

**Type-aware** (references + rename): the field/enum-case namespace is kept distinct from
signals/types/funcs (a field `x` is never confused with a signal `x`), and a member is matched only
where the accessed value has the field's owning type (two structs sharing a field name are not
conflated). An LHS too complex to type (e.g. `f(a).x`) is kept rather than dropped.

---

## File map (post-`src/` restructure)

- **`src/lsp/main.cpp`** — the server. JSON-RPC over stdio, `Content-Length` framing, full-document
  sync. Thin: parses the request, calls a `Referee::…` entry, shapes the JSON reply. Keeps an
  in-memory `g_docs` (uri → text) for the request-driven features. `initialize` advertises every
  capability; unknown requests get `reply(id, null)`, unknown notifications are ignored.
- **`src/driver/referee.cpp`** — the analysis entries (the real logic):
  `diagnose`, `complete`, `hover`, `define`, `symbols`, `references`, `rename`, `signatureHelp`,
  plus file-local helpers (`renderType`, `gatherFiles`, `scanBodies`, `resolveChainType`,
  `declCol`, `wordAt`, `commentStart`, …). Declarations + result structs in **`src/driver/referee.hpp`**.
- **`editors/vscode/src/extension.ts`** — the VS Code client (`vscode-languageclient`). Spawns
  `referee-lsp`; server path/args from the `referee.lsp.path` / `referee.lsp.args` settings
  (`${workspaceFolder}` / `${userHome}` expanded in-extension — VS Code does **not** expand these
  in extension-read settings). Command **REF: Restart Language Server**. Extension is at **v0.3.0**.
- **`test/diagnostics.cpp`** — the tests. Suites: `Completion`, `Hover`, `Definition`, `Symbols`,
  `References`, `Rename`, `SignatureHelp`, `Robustness` (37 LSP-facing tests; the file also holds the
  pre-existing `Diagnostics` suite). Tests call the `Referee::…` entries directly.
- **`meson.build`** — `lsp_exe` target builds `referee-lsp` (deps: `core_dep`, `fmt_dep`,
  `antlr4_runtime_dep`, `llvm_dep` for `llvm::json`). `Dockerfile` builds+ships it too.

---

## Design invariants — read before editing

1. **NEVER walk a syntactically broken parse tree.** The core AST visitor
   (`core/antlr2ast.cpp`) assumes a well-formed tree and will **segfault** on incomplete input
   (e.g. `type P : struct { x` while typing — a null member type). Every entry that parses must
   guard `visitProgram` with `if (!errors.any())`, exactly as `diagnose()` does. A segfault is not a
   C++ exception, so `try/catch` will **not** save you. This was a real crash (`fix e5e3d28`) and the
   `Robustness.LspEntriesSurviveBrokenParse` test locks it in. There is a latent null-deref in
   `core/` behind it — left alone on purpose (see coordination).

2. **Stay out of `core/`.** All LSP logic lives in `src/driver/referee.{cpp,hpp}`, `src/lsp/`,
   `editors/vscode/`, `test/diagnostics.cpp`. The compiler/monitor is under active concurrent
   development; keeping the LSP additive is what has kept it conflict-free.

3. **Positions come from the source text, not the AST.** The AST doesn't retain declaration
   positions, so go-to-def/symbols/references locate declarations by scanning text (`declCol`,
   `wordAt`), and use the AST only for *identity* (types, members, owning type). This is why the
   feature set needed no `core/` change.

4. **Completion/signatureHelp blank the caret line before parsing** (so a half-typed token doesn't
   break the parse); hover/define/references/symbols parse the raw text and rely on invariant #1.

5. **Cross-file** = `gatherFiles()` reads the current buffer + transitively imported files from
   **disk**, resolving `import "spec"` the way `Antlr2AST::resolveImport` does (importing file's dir,
   then include paths), deduped by canonical path. Imported files are read from disk, so unsaved
   edits in another tab aren't reflected until saved.

6. **Keywords come from the lexer's own vocabulary** (`getVocabulary().getLiteralName`, alphabetic
   literals only) — they track the grammar, never a hand-kept list. Note `getLiteralName` returns
   `std::string_view` in this ANTLR runtime.

---

## Build / run / use

```bash
ninja -C build referee-lsp            # binary → build/referee-lsp
```
It's an stdio server, not a REPL — an editor launches it. VS Code/Cursor/Antigravity: install the
extension (`cd editors/vscode && npm install && npm run compile && npx @vscode/vsce package`, then
Install from VSIX), set `referee.lsp.path` to the absolute `build/referee-lsp`, reload.
**Server-only changes need no extension reinstall** — just **REF: Restart Language Server**.

Docker: `docker build -t referee:lsp .`; run with
`docker run --rm -i --entrypoint referee-lsp -v "$PWD":"$PWD" -w "$PWD" referee:lsp`, and set
`referee.lsp.path` to `docker` with the matching `referee.lsp.args`.

---

## Known limitations / possible next work

- **Include paths default to empty** from the LSP, so only *relative* imports resolve. A workspace
  setting to pass `-I` dirs (threaded into `gatherFiles` + the AST parse) is the natural extension.
- **Signature help for user funcs needs the func decl to parse**; a call whose *own* line is the only
  thing keeping the doc from parsing works (caret line is blanked), but errors elsewhere fall back to
  built-ins only.
- **Type-aware refs on a complex LHS** (`f(a).x`, `arr[0].x`) keep the occurrence rather than typing
  it — could be tightened with expression typing.
- Not implemented: **folding ranges, semantic tokens, inlay hints, code actions/quick-fixes**
  (e.g. "did you mean `::`?" for a `.`-as-namespace mistake), **formatting**.

---

## Commit trail (oldest → newest)

```
939ae6a  docs: referee-lsp (build target, editor + Docker usage)
454e46f  vscode extension: release 0.2.0 (language-server client) + LICENSE
e564eb9  member completion  (35facc1 tests)
5dbae23  hover
5e46bad  go-to-definition
d09ac17  document symbols
a22ac5b  cross-file go-to-definition (follow imports)
93fc258  bare-identifier completion (names in scope + grammar keywords)
56cce99  find-references (across imports, comments excluded)
54d25e5  rename (across imports, validity check)
b441a11  signature help (func + builtin, active param)
4da9bbd  type-aware reference disambiguation
e5e3d28  fix(lsp): segfault on incomplete syntax — guard visitProgram with errors.any()
```
(Diagnostics — the foundation — landed earlier and was swept into concurrent `-a` commits.)
The extension bump to **v0.3.0** was made downstream (not by these commits).
