# Changelog

All notable changes to csv.h. Versions follow [semver](https://semver.org/).

## 1.1.0

**Added**

- `CSV_ERR_ENCODING`. A document opening with a UTF-16 or UTF-32 byte-order
  mark (`FF FE`, `FE FF`, `00 00 FE FF`) is now rejected instead of parsed a
  byte at a time, which used to yield fields full of NUL padding and no error
  at all. `CSV_FLAG_NO_BOM` opts out along with the rest of BOM handling.
- `CSV_ARRAY_LEN()` is a compile-time error when handed a pointer rather than
  an array. `sizeof(p) / sizeof(p[0])` on a decayed pointer silently computes
  a nonsense capacity, and `csv_foreach_row()` hides the macro, so a heap-
  allocated row array used to parse as a zero-column document. C++ gets a
  reference-to-array probe, GCC and Clang a negative-width bitfield; MSVC in C
  mode has no way to tell the two types apart and keeps the old behaviour.

**Fixed**

- `rd.line` ran ahead in streaming mode. `csv__need_more()` rewound the read
  offset to the start of the incomplete record but not the line counter, so
  every newline inside a quoted field of that record was counted again on each
  re-parse: a three-line document could report line 5 or 6 depending on where
  the chunk boundaries fell. Line numbers are now identical however the input
  is sliced, and the fuzzer's chunking-equivalence property covers `rd.line`
  rather than field bytes alone.
- The writer no longer calls `memcpy()` through a null buffer pointer. A sizing
  pass (`csv_writer_init(&w, NULL, 0, NULL)`) writing an empty field reached
  `memcpy(NULL, src, 0)`: C requires a valid pointer argument however few
  bytes are copied, and UBSan flags it wherever `<string.h>` declares the
  parameter nonnull.

## 1.0.0

First stable release. The API documented in the README is now covered by
semver: breaking changes only with a major version bump.

**Added**

- `examples/demo.c` and `make demo` — a one-screen tour of reading, writing,
  error handling and dialects.

**Fixed**

- `csv_opts_default()` left `escape` and `skip_lines` uninitialised, so any
  reader or writer given `NULL` options read stack garbage for those two
  fields and could skip a random number of leading lines or honour a random
  escape byte.
- One-byte out-of-bounds read when `opts.escape` is set and an unquoted field
  containing an earlier escape ends with a lone escape byte at end of input.
  The trailing escape is now kept literally.

## 0.2.0

**Added**

- `csv_reader_init_mut()` — in-place mode. Un-escaping only ever shortens a
  field, so when the buffer is yours and can be modified the parser rewrites
  those fields where they sit: no scratch buffer, no allocation, and field
  pointers stay valid for the whole document rather than one record.
- `opts.escape` — backslash-style escapes (`\"` as well as `""`), for Python's
  `escapechar` dialect, MySQL and Postgres `COPY`. Reader-side only; the
  default path never looks for an escape character, so it costs nothing when
  off.
- `opts.skip_lines` — drop N physical lines before parsing, for preamble junk
  above the header.
- `CSV_MEMMOVE` override, alongside the existing `mem*` hooks.
- csv-spectrum conformance suite (`make spectrum`), vendored under
  `tests/spectrum/` and checked in buffer, in-place and streamed modes.
  `make spectrum-fetch` re-downloads it from upstream and diffs.
- `make bench-vs`, a head-to-head benchmark against libcsv that also
  cross-checks field counts and checksums.

**Changed**

- `csv_next_row()` drives the field parser directly instead of round-tripping
  through the event machine, and `csv__field()` is force-inlined into both of
  its callers. Together worth ~13% on short fields.
- `examples/csvcat.c` grows its window when a single record needs more room, so
  there is no maximum record size.
- `csv_opts` gained two members. Code using `CSV_OPTS_INIT`, designated
  initialisers or `csv_opts_default()` is unaffected; positional aggregate
  initialisers need updating.

**Fixed**

- Building with `-DCSV_STATIC` no longer produces unused-function warnings for
  entry points the caller does not use.

## 0.1.0

Initial release: RFC 4180 pull parser with a sliding-window streaming
protocol, RFC 4180 writer with quoting and an optional sink, optional
allocating table layer with header-by-name lookup, and configurable delimiter,
quote and comment characters.
