// Tokenize a .ref file with the real VS Code TextMate engine, so the grammar is
// checked against the same implementation Cursor uses rather than by eye.
const fs = require('fs');
const path = require('path');
const oniguruma = require('vscode-oniguruma');
const vsctm = require('vscode-textmate');

const GRAMMAR = path.join(__dirname, '..', 'syntaxes', 'ref.tmLanguage.json');

const wasmBin = fs.readFileSync(
  path.join('node_modules', 'vscode-oniguruma', 'release', 'onig.wasm')
).buffer;

const vscodeOnigurumaLib = oniguruma.loadWASM(wasmBin).then(() => ({
  createOnigScanner: (patterns) => new oniguruma.OnigScanner(patterns),
  createOnigString: (s) => new oniguruma.OnigString(s),
}));

const registry = new vsctm.Registry({
  onigLib: vscodeOnigurumaLib,
  loadGrammar: async (scopeName) => {
    if (scopeName === 'source.ref') {
      return vsctm.parseRawGrammar(fs.readFileSync(GRAMMAR, 'utf8'), GRAMMAR);
    }
    return null;
  },
});

(async () => {
const grammar = await registry.loadGrammar('source.ref');
if (!grammar) { console.error('grammar failed to load'); process.exit(1); }

const file = process.argv[2] || path.join(__dirname, 'sample.ref');
const text = fs.readFileSync(file, 'utf8').split('\n');

// Expectation checks: [substring, scope-fragment that must be on it]
const expectations = process.argv[3] ? JSON.parse(process.argv[3]) : [
  ['// line comment', 'comment.line.double-slash'],
  ['# hash comment', 'comment.line.number-sign'],
  ['import', 'keyword.control.import'],
  ['State', 'entity.name.type'],
  ['enum', 'storage.type.composite'],
  ['struct', 'storage.type.composite'],
  ['number', 'support.type.primitive'],
  ['both', 'variable.other.signal'],
  ['limit', 'variable.other.constant'],
  ['G', 'keyword.operator.temporal.future'],
  ['F', 'keyword.operator.temporal.future'],
  ['Us', 'keyword.operator.temporal.future'],
  ['Ss', 'keyword.operator.temporal.past'],
  ['I', 'keyword.operator.temporal.integral'],
  ['@', 'keyword.operator.freeze'],
  ['__time__', 'variable.language.time'],
  ['globally', 'keyword.control.pattern.scope'],
  ['holds', 'keyword.control.pattern'],
  ['milliseconds', 'keyword.other.unit'],
  ['1.5', 'constant.numeric.float'],
  ['0xFF', 'constant.numeric.hex'],
  ['&&', 'keyword.operator.logical'],
  ['||', 'keyword.operator.logical'],
  ['<=', 'keyword.operator.comparison'],
  ['true', 'constant.language.boolean'],
  ['all', 'keyword.control.quantifier'],
  ['some', 'keyword.control.quantifier'],
  ['none', 'keyword.control.quantifier'],
  ['one', 'keyword.control.quantifier'],
  ['most', 'keyword.control.quantifier'],
  ['at', 'keyword.control.pattern'],
  ['least', 'keyword.control.pattern'],
  ['in', 'keyword.control.pattern'],
];
const found = new Map();

let ruleStack = vsctm.INITIAL;
const dump = process.env.DUMP === '1';
for (let i = 0; i < text.length; i++) {
  const line = text[i];
  const r = grammar.tokenizeLine(line, ruleStack);
  for (const t of r.tokens) {
    const frag = line.substring(t.startIndex, t.endIndex);
    const scopes = t.scopes.join(' ');
    if (dump && frag.trim()) console.log(`${String(i + 1).padStart(3)}  ${JSON.stringify(frag).padEnd(22)} ${scopes}`);
    if (frag.trim()) {
      if (!found.has(frag.trim())) found.set(frag.trim(), new Set());
      found.get(frag.trim()).add(scopes);
    }
  }
  ruleStack = r.ruleStack;
}

let bad = 0;
for (const [needle, wantScope] of expectations) {
  const scopes = found.get(needle);
  if (!scopes) { console.log(`  MISSING TOKEN  ${JSON.stringify(needle)}`); bad++; continue; }
  const ok = [...scopes].some((s) => s.includes(wantScope));
  if (!ok) {
    console.log(`  WRONG SCOPE    ${JSON.stringify(needle)}`);
    console.log(`                 want *${wantScope}*`);
    console.log(`                 got  ${[...scopes].join('\n                      ')}`);
    bad++;
  } else {
    console.log(`  ok             ${JSON.stringify(needle).padEnd(22)} -> ${wantScope}`);
  }
}
process.exit(bad ? 1 : 0);
})();
