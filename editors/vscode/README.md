# REF language support

Syntax highlighting, bracket/comment handling and snippets for REF requirement
specifications (`.ref`), the language compiled by
[referee](https://github.com/michaelrolnik/referee).

Works in VS Code and its forks — Cursor, Antigravity, VSCodium — since they all
consume the same extension format.

## What it highlights

The keyword lists are generated from `core/referee.g4`, not written by hand, so
they cover the grammar exactly — all 83 reserved words, including the whole
Dwyer specification-pattern vocabulary.

- **Temporal operators** — `G` `F` `H` `O` `Xs` `Xw` `Ys` `Yw` `Us` `Uw` `Ss`
  `Sw` `Rs` `Rw` `Ts` `Tw` `I`, future and past scoped separately so a theme can
  colour them differently. Each only highlights when followed by `(` or a `[`
  time bound, so a capital letter elsewhere is left alone.
- **Quantifiers** — `all`, `some`, `none`, `one`, and the `at least` / `at most` forms, scoped separately from the pattern vocabulary.
- **Specification patterns** — scope words (`globally`, `before`, `after`,
  `while`, `between`, `until`) separately from the rest of the vocabulary
  (`holds`, `eventually`, `once`, `remains`, …). These are *reserved*, so seeing
  them light up is a useful warning that `data at : boolean;` will not parse.
- **Declarations** — `type`, `data`, `conf`, `import`, with the declared name
  scoped as a type / signal / constant respectively.
- **Freeze variables** — `t@( … )` and `__time__`.
- **Literals** — decimal, `0b`, `0o`, `0x`, floats with exponents, strings.
  Characters that REF's `STRING` token does not accept are marked invalid inside
  a string literal, which catches the common case of pasting a path with
  characters the lexer rejects.
- **Time units** — `nanoseconds` … `minutes`.

## What it does not do

Highlighting only. There is no language server, so no diagnostics, completion,
go-to-definition or hover. Compile errors still come from running `referee
compile`. Adding an LSP is tracked in the main README's *What is missing*.

## Install

### Antigravity or Cursor over SSH (remote)

Extensions for a remote window live on the **remote** machine, not your laptop.
Copy the extension into the remote server's extension directory and reload:

```bash
# on the remote host, from a referee checkout
mkdir -p ~/.antigravity-ide-server/extensions/michaelrolnik.referee-ref-0.1.0
cp -r editors/vscode/* ~/.antigravity-ide-server/extensions/michaelrolnik.referee-ref-0.1.0/
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
