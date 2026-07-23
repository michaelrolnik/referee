# REF language support

Syntax highlighting, bracket/comment handling, snippets and — via a bundled
language-server client — live diagnostics for REF requirement specifications
(`.ref`), the language compiled by
[referee](https://github.com/michaelrolnik/referee).

Works in VS Code and its forks — Cursor, Antigravity, VSCodium — since they all
consume the same extension format.

## What it highlights

The keyword lists are generated from `core/referee.g4`, not written by hand, so
they cover the grammar exactly — all 83 reserved words, including the whole
Dwyer specification-pattern vocabulary.

- **Temporal operators** — `G` `F` `H` `O` `Xs` `Xw` `Ys` `Yw` `Us` `Uw` `Ss`
  `Sw` `Rs` `Rw` `Ts` `Tw`, and the accumulators `Itg` `Sum` `Cnt`, future and
  past scoped separately so a theme can
  colour them differently. Each only highlights when followed by `(` or a `[`
  time bound, so a capital letter elsewhere is left alone.
- **Quantifiers** — `all`, `some`, `none`, `one`, and the `at least` / `at most` forms, scoped separately from the pattern vocabulary.
- **Specification patterns** — scope words (`globally`, `before`, `after`,
  `while`, `between`, `until`) separately from the rest of the vocabulary
  (`holds`, `eventually`, `once`, `remains`, …). These are *reserved*, so seeing
  them light up is a useful warning that `data at : boolean;` will not parse.
- **Types** — `boolean`, `byte`, `integer`, `number`, `string`.
- **Declarations** — `type`, `data`, `conf`, `import`, with the declared name
  scoped as a type / signal / constant respectively.
- **Freeze variables** — `t@( … )` and `__time__`.
- **Literals** — decimal, `0b`, `0o`, `0x`, floats with exponents, strings.
  Characters that REF's `STRING` token does not accept are marked invalid inside
  a string literal, which catches the common case of pasting a path with
  characters the lexer rejects.
- **Time units** — `nanoseconds` … `minutes`.

## Diagnostics (language server)

The extension launches the `referee-lsp` server and shows **live parse and type
errors** as you edit — the same errors `referee compile` reports, inline, with no
build step. Point it at the server binary:

```jsonc
// Settings (JSON), or a workspace .vscode/settings.json
{
  "referee.lsp.path": "/absolute/path/to/referee/build/referee-lsp"
}
```

The default is `referee-lsp` (found on `PATH`); set an absolute path to a build,
or `"docker"` with `referee.lsp.args` for a containerized server. The command
**REF: Restart Language Server** reloads it after you rebuild. Build the server
with `ninja -C build referee-lsp` in a referee checkout.

It also does **member completion** (type `.` after a struct or enum signal and the
editor lists its fields / enum cases) and **hover** (point at a name to see its
declaration — `data pt : Point`, a struct/enum body, a field's type).
Go-to-definition is not implemented yet.

## Install

The extension now carries a compiled client and a runtime dependency, so build it
before installing — then use the `.vsix` route below (it bundles `out/` and the
`vscode-languageclient` runtime, which a hand copy would miss):

```bash
cd editors/vscode
npm install
npm run compile        # src/extension.ts -> out/extension.js
```

### Antigravity or Cursor over SSH (remote)

Extensions for a remote window live on the **remote** machine, not your laptop —
and so does the `referee-lsp` binary the client launches. Install the `.vsix`
(below) from the remote window's Extensions view; prefer it over a hand copy,
which must include the built `out/` and production `node_modules`, not just the
static assets:

```bash
# on the remote host, from a referee checkout — only if not using the .vsix:
DEST=~/.antigravity-ide-server/extensions/michaelrolnik.referee-ref-0.1.0
mkdir -p "$DEST"
cp -r editors/vscode/{package.json,language-configuration.json,syntaxes,snippets,README.md,out,node_modules} "$DEST/"
```

Then run **Developer: Reload Window** from the command palette.

The directory differs per IDE and per connection mode:

| IDE | Local | Remote (SSH) |
| --- | --- | --- |
| Antigravity | `~/.antigravity/extensions/` | `~/.antigravity-ide-server/extensions/` |
| Cursor | `~/.cursor/extensions/` | `~/.cursor-server/extensions/` |
| VS Code | `~/.vscode/extensions/` | `~/.vscode-server/extensions/` |

If you are unsure which applies, look for the one that already exists and
contains your installed extensions.

### As a .vsix

```bash
cd editors/vscode
npx @vscode/vsce package        # produces referee-ref-0.1.0.vsix
```

Install it from the Extensions view (`…` menu → **Install from VSIX…**), or from
a CLI if your IDE ships one:

```bash
cursor --install-extension referee-ref-0.1.0.vsix
code   --install-extension referee-ref-0.1.0.vsix
```

Installing from the Extensions view is the more reliable route on a remote
window, because it registers the extension with the server rather than relying
on a directory scan.

### Verify

Open any `.ref` file — for example `test/logic/pass.ref`. The language indicator
in the status bar should read **REF**. If it says Plain Text, the extension did
not load; check that the directory name you copied into matches
`publisher.name-version` and that you reloaded the window.

## Development

The grammar is tested against the same TextMate engine VS Code uses, rather than
by eye:

```bash
cd editors/vscode/test
npm install
node tokenize.cjs
```

This matters more than it sounds: Oniguruma is stricter than JavaScript's regex
engine, and an invalid pattern makes VS Code drop the grammar *silently* — the
file just renders unhighlighted with no error anywhere. The test caught exactly
that during development (an `\b?`, which JavaScript tolerates and Oniguruma
rejects).
