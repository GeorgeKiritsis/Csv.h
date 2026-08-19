# Security Policy

## Supported versions

| Version | Supported |
| ------- | --------- |
| 1.0.x   | :white_check_mark: |
| < 1.0   | :x: (pre-release, do not use) |

Only the latest release gets fixes. Because `csv.h` is vendored, meaning you
copy the file into your own tree, there is no package manager that can push a
fix to you. If a security release happens, you have to re-copy the header
yourself. Watch the repository for releases if that matters to you.

## Threat model

The library is written to be safe against **hostile input** and to trust the
**calling program**.

**In scope.** Anything a malicious or malformed CSV document can do to a
correctly written caller:

- out-of-bounds read or write
- use of uninitialised memory
- infinite loop, or a parse that fails to make progress
- unbounded memory or stack growth
- any crash, assertion failure or undefined behaviour reached from input alone

Reports here are treated as security issues even if you cannot show
exploitability. A parser that reads one byte past a buffer is a bug worth
fixing whether or not you can turn it into anything.

**Out of scope.** Violations of the caller's side of the contract, which are
programming errors rather than vulnerabilities:

- calling `csv_reader_feed()` in the middle of a record, or on a reader created
  with `csv_reader_init_mut()`
- setting `delimiter` equal to `quote`, or either to `\r` or `\n`
- passing `csv_reader_init_mut()` a buffer you do not own or that is read-only
- using a `csv_str` after the buffer it points into has been freed, moved or
  re-fed (see "Object lifetimes" in the README)
- passing a scratch buffer that is too small, which is reported as
  `CSV_ERR_NO_SPACE` rather than being undefined behaviour

Those cases are guarded by `CSV_ASSERT`. Note that assertions compile out under
`NDEBUG`, so a release build will not catch them for you.

## Properties you can rely on

These are the invariants the parser is designed and tested to hold. If you can
break one with input alone, that is a vulnerability:

- the reader and writer never allocate, so a hostile document cannot exhaust
  the heap through them
- stack use is bounded (≤ 192 bytes per call on x86-64), with no recursion, no
  `alloca` and no VLAs, so no input can cause stack exhaustion
- the parser never writes to your input buffer unless you opted in with
  `csv_reader_init_mut()`
- the writer never writes past the end of the buffer you gave it. It reports
  `CSV_ERR_NO_SPACE` and keeps counting the size it would have needed
- errors are sticky. After a failure the reader keeps returning
  `CSV_EVENT_ERROR` rather than producing further records

## Known limitations that are not bugs

- **`csv_table_parse()` and `csv_table_load()` allocate in proportion to the
  input.** That is the point of the table layer, but it means a 10 GB file
  costs you 10 GB. If the input is untrusted, either bound the size before you
  call it or use the streaming reader, which allocates nothing.
- **A record must fit in the window you provide.** A document consisting of one
  enormous record will return `CSV_EVENT_NEED_MORE` with `csv_consumed() == 0`
  and it is up to you to grow the buffer or reject the file. Deciding when to
  stop growing is the caller's policy choice, so bound it if the input is
  untrusted.
- **CSV injection is an output-side problem and this library does not address
  it.** A field beginning with `=`, `+`, `-` or `@` may be executed as a
  formula when the file is opened in a spreadsheet. If you write CSV that
  someone will open in Excel or Sheets, sanitise those fields yourself before
  passing them to the writer. Quoting does not prevent this.
- **No encoding validation.** The parser is byte-oriented. It will happily
  carry invalid UTF-8 through to your fields. Validate the encoding if your
  consumers require it.

## Reporting a vulnerability

Please report privately first, through **GitHub's private vulnerability
reporting**, using the "Report a vulnerability" button under the repository's
Security tab. That opens a draft advisory only you and I can see.

If you would rather use email, write to [your email here].

Please do not open a public issue for a memory-safety bug until there is a fix
available.

**What to include.** The most useful report is one I can reproduce in a minute:

- the input document, or the fuzzer artifact, as an attachment or a hex dump.
  Whitespace and line endings matter, so please do not paste it as prose
- the `csv_opts` you used (delimiter, quote, comment, escape, flags), or a note
  that they were the defaults
- which entry point you called: `csv_reader_init_mut`, `csv_reader_init_buf`,
  the streaming path, or the table layer
- compiler, version, flags, and the sanitizer output if you have it

A failing `LLVMFuzzerTestOneInput` artifact from `fuzz/fuzz_csv.c` is the ideal
report, since it drops straight into the existing harness.

**What to expect.** This is maintained by one person, so
these are honest targets rather than an SLA:

| | |
| --- | --- |
| Acknowledgement | within 5 days |
| Assessment, in scope or not, with reasoning | within 14 days |
| Fix and release for an accepted memory-safety issue | as fast as I can, realistically within 30 days |
| Public advisory | after the fix is released, or after 90 days, whichever comes first |

If a report is declined I will tell you why rather than letting it go quiet. If
you disagree with the assessment, say so. "Out of scope" is a judgement call
and I would rather be argued out of a wrong one.

**Credit.** Reporters are credited in the CHANGELOG and in the advisory unless
you ask not to be. There is no bounty. This is a single header file maintained
for free, and I would rather be honest about that than imply otherwise.
