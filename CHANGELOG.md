# Changelog

All notable changes to csv.h. Versions follow [semver](https://semver.org/).

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
