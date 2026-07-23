#!/usr/bin/env python3
"""
Check the run-trace schema, and check it against examples.

A schema that accepts everything is worse than none, so this asserts what must
be *rejected* as well as what must pass -- the invariants the format exists to
hold, which prose in a spec cannot enforce.

    python3 schema/test/validate.py               # self-test on the examples
    python3 schema/test/validate.py run.ndjson    # validate a producer's output
"""
import json, pathlib, sys

try:
    import jsonschema
except ImportError:
    sys.exit("needs jsonschema:  pip install jsonschema")

here   = pathlib.Path(__file__).parent
schema = json.loads((here.parent / "run-trace.schema.json").read_text())

jsonschema.Draft202012Validator.check_schema(schema)
validator = jsonschema.Draft202012Validator(schema)

failed = 0

#  Files named on the command line are validated line by line. This used to
#  ignore argv entirely and print `ok` for the bundled example regardless --
#  which is exactly how a producer/schema divergence stayed hidden.
for arg in sys.argv[1:]:
    for n, l in enumerate(pathlib.Path(arg).read_text().splitlines(), 1):
        if not l.strip():
            continue
        for err in validator.iter_errors(json.loads(l)):
            print(f"{arg}:{n}: {err.message}")
            failed += 1

lines  = [json.loads(l) for l in (here / "example.ndjson").read_text().splitlines() if l.strip()]

for n, doc in enumerate(lines, 1):
    for err in validator.iter_errors(doc):
        print(f"example line {n}: {err.message}")
        failed += 1

def rejects(what, mutate):
    """The invariant holds only if the mutated document is refused."""
    global failed
    doc = json.loads(json.dumps(next(l for l in lines if l["kind"] == "requirement")))
    mutate(doc)
    if not list(validator.iter_errors(doc)):
        print(f"accepted what it must reject: {what}")
        failed += 1

rejects("vacuous without a reason",        lambda d: d.__setitem__("vacuous", True))
rejects("a reason without vacuous",        lambda d: d.__setitem__("vacuity", {"reason": "evaluated_once"}))
#  Witnesses on a temporal row are optional -- referee derives them lazily and
#  omits them today -- but a *state* row must never carry them.
rejects("state row carrying witnesses",    lambda d: d["rows"][0].__setitem__("witnesses", [0, 0, 0, 0]))
rejects("window row lacking a window",     lambda d: d["rows"][2].pop("windows"))
rejects("a malformed source label",        lambda d: d.__setitem__("where", "nonsense"))
rejects("a verdict outside pass/fail",     lambda d: d.__setitem__("verdict", "maybe"))
rejects("an unknown field",                lambda d: d.__setitem__("extra", 1))
rejects("a row with neither ref nor values",
        lambda d: d["rows"][1].pop("values"))
rejects("a row with both ref and values",
        lambda d: d["rows"][0].__setitem__("values", [True, True, True, True]))
rejects("sparse without change indices",
        lambda d: d["rows"][1].__setitem__("encoding", "sparse"))
rejects("change indices without sparse",
        lambda d: d["rows"][1].__setitem__("at", [0, 1, 2, 3]))

print("ok" if not failed else f"{failed} problem(s)")
sys.exit(1 if failed else 0)
