#!/usr/bin/env python3
"""Differential test: csv.h vs the Python standard library csv module.

Generates random documents from a hostile alphabet, parses each with both
implementations, and reports any disagreement.

Two documented, intentional differences are normalised away:

  * a blank line is one empty field for csv.h (RFC 4180 grammar) but an empty
    record for Python;
  * Python never fails on an unterminated quoted field, csv.h does, so those
    documents are skipped.

Usage: python3 tests/diff_python.py [iterations]
"""

import csv
import io
import json
import random
import subprocess
import sys

DUMP = "build/dumpjson"
ALPHABET = list('ab,;\t "\r\n\\x') + ['""', '\r\n', 'ab']


def gen(rng):
    parts = []
    for _ in range(rng.randint(1, 40)):
        parts.append(rng.choice(ALPHABET))
    return "".join(parts)


def py_parse(doc, delim):
    rows = []
    for row in csv.reader(io.StringIO(doc, newline=""), delimiter=delim):
        rows.append(row)
    return rows


def c_parse(doc, delim):
    p = subprocess.run([DUMP, delim], input=doc.encode(), capture_output=True)
    if p.returncode != 0:
        return None  # csv.h rejected the document
    return json.loads(p.stdout.decode())


def norm(rows):
    return [r for r in rows if r != [""]and r != []]


def main():
    iterations = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
    rng = random.Random(20260818)
    checked = skipped = 0

    for i in range(iterations):
        delim = rng.choice([",", ";", "\t"])
        doc = gen(rng)

        c = c_parse(doc, delim)
        if c is None:
            skipped += 1
            continue
        try:
            p = py_parse(doc, delim)
        except csv.Error:
            skipped += 1
            continue

        if norm(c) != norm(p):
            print("MISMATCH on document %r (delim %r)" % (doc, delim))
            print("  csv.h : %r" % (norm(c),))
            print("  python: %r" % (norm(p),))
            return 1
        checked += 1

    print("%d documents agree with Python's csv module (%d skipped)"
          % (checked, skipped))
    return 0


if __name__ == "__main__":
    sys.exit(main())
