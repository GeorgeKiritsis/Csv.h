/*
 * csv.h - RFC 4180 CSV reader and writer in a single C99 header.
 * https://github.com/<you>/csv.h            v0.2.0            MIT licensed
 *
 * ---------------------------------------------------------------------------
 * USAGE
 *
 *   In exactly one translation unit:
 *       #define CSV_IMPLEMENTATION
 *       #include "csv.h"
 *   Everywhere else just #include "csv.h".
 *
 *   Reading a buffer you own and can modify -- no scratch, no allocation:
 *
 *       csv_reader rd;
 *       csv_str    row[64];
 *
 *       csv_reader_init_mut(&rd, NULL, data, len);
 *
 *       csv_foreach_row (&rd, row, n)
 *           for (size_t i = 0; i < n; i++)
 *               printf("[" CSV_FMT "]", CSV_ARG(row[i]));
 *
 *   Reading without touching the input (needs scratch for `""` un-escaping):
 *
 *       char      scratch[4096];
 *       csv_reader rd;
 *       csv_str    row[64];
 *       size_t     n;
 *
 *       csv_reader_init(&rd, NULL, scratch, sizeof scratch);
 *       csv_reader_feed(&rd, data, len, 1);   // 1 = this is the whole input
 *
 *       csv_foreach_row (&rd, row, n)
 *           for (size_t i = 0; i < n; i++)
 *               printf("[" CSV_FMT "]", CSV_ARG(row[i]));
 *
 *       if (rd.err) fprintf(stderr, "line %zu: %s\n", rd.line, csv_strerror(rd.err));
 *
 *   Writing:
 *
 *       char buf[256];
 *       csv_writer w;
 *       csv_writer_init(&w, buf, sizeof buf, NULL);
 *       csv_write_cstr(&w, "name");  csv_write_cstr(&w, "quote \"me\"");
 *       csv_write_row_end(&w);
 *       fwrite(buf, 1, w.len, stdout);
 *
 *   See README.md for streaming over a sliding window, the allocating table
 *   API, and the full option list.
 *
 * ---------------------------------------------------------------------------
 * COMPILE-TIME CONFIGURATION   (define before including the implementation)
 *
 *   CSV_STATIC        make every symbol `static` (single-TU builds)
 *   CSV_API           override linkage/visibility entirely
 *   CSV_NO_ALLOC      drop the malloc-based csv_table layer
 *   CSV_NO_STDIO      drop csv_table_load() and csv_sink_file()
 *   CSV_ASSERT(x)     override assert()
 *   CSV_MEMCPY/CSV_MEMMOVE/CSV_MEMCHR/CSV_MEMCMP/CSV_MEMSET  override <string.h>
 *   CSV_MALLOC/CSV_REALLOC/CSV_FREE               override <stdlib.h>
 *
 * The parser core never allocates, never calls into stdio and keeps no global
 * state, so it is safe on freestanding and embedded targets with CSV_NO_ALLOC
 * + CSV_NO_STDIO. It does not write to your input buffer either, unless you
 * opt into that with csv_reader_init_mut().
 *
 * ---------------------------------------------------------------------------
 * LICENSE
 *
 * Copyright (c) 2026 csv.h contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef CSV_H_INCLUDED
#define CSV_H_INCLUDED

#include <stddef.h>
#include <limits.h>

#define CSV_VERSION_MAJOR 0
#define CSV_VERSION_MINOR 2
#define CSV_VERSION_PATCH 0

#define CSV__STRINGIFY_(x) #x
#define CSV__STRINGIFY(x)  CSV__STRINGIFY_(x)
#define CSV_VERSION_STRING                                                     \
    CSV__STRINGIFY(CSV_VERSION_MAJOR) "." CSV__STRINGIFY(CSV_VERSION_MINOR)    \
    "." CSV__STRINGIFY(CSV_VERSION_PATCH)

#if !defined(CSV_NO_STDIO) && !defined(CSV_NO_ALLOC)
#   include <stdio.h> /* FILE, for csv_table_load() and csv_sink_file() */
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Portability shims. Each is overridable; each has a boring fallback.
 * ========================================================================= */

#ifndef CSV_API
#   ifdef CSV_STATIC
#       define CSV_API static
#   else
#       define CSV_API extern
#   endif
#endif

#ifndef CSV_INLINE
#   if defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L)
#       define CSV_INLINE static inline
#   else
#       define CSV_INLINE static
#   endif
#endif

#if defined(__GNUC__) || defined(__clang__)
#   define CSV_LIKELY(x)   __builtin_expect(!!(x), 1)
#   define CSV_UNLIKELY(x) __builtin_expect(!!(x), 0)
#   define CSV_HOT_INLINE  static __inline__ __attribute__((always_inline))
#elif defined(_MSC_VER)
#   define CSV_LIKELY(x)   (x)
#   define CSV_UNLIKELY(x) (x)
#   define CSV_HOT_INLINE  static __forceinline
#else
#   define CSV_LIKELY(x)   (x)
#   define CSV_UNLIKELY(x) (x)
#   define CSV_HOT_INLINE  CSV_INLINE
#endif

/* A compile-time assertion that works in C99 and C++98 alike. */
#define CSV__CAT_(a, b) a##b
#define CSV__CAT(a, b)  CSV__CAT_(a, b)
#define CSV_STATIC_ASSERT(cond, tag)                                           \
    typedef char CSV__CAT(csv__static_assert_##tag##_, __LINE__)[(cond) ? 1 : -1]

/* The dispatch table below is indexed by `unsigned char`. */
CSV_STATIC_ASSERT(UCHAR_MAX == 255, byte_is_octet);

/* =========================================================================
 * Strings: a borrowed (pointer, length) pair. Never NUL-terminated, never
 * owned, never allocated. Valid until the end of the current row (see below).
 * ========================================================================= */

typedef struct csv_str {
    const char *ptr;
    size_t      len;
} csv_str;

/* printf("%s is " CSV_FMT "\n", k, CSV_ARG(v)); */
#define CSV_FMT       "%.*s"
#define CSV_ARG(s)    (int)(s).len, (s).ptr
#define CSV_LIT(lit)  csv_str_make((lit), sizeof(lit) - 1)
#define CSV_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

CSV_API csv_str csv_str_make(const char *p, size_t n);
CSV_API int     csv_str_eq(csv_str a, csv_str b);
CSV_API int     csv_str_eq_cstr(csv_str a, const char *z);

/* =========================================================================
 * Errors and events. Both are X-macro tables so the enum, the name table and
 * the human-readable message can never drift apart.
 * ========================================================================= */

#define CSV_ERROR_LIST(X)                                                      \
    X(CSV_OK,                 "ok")                                            \
    X(CSV_ERR_UNTERMINATED,   "unterminated quoted field")                     \
    X(CSV_ERR_BARE_QUOTE,     "unescaped quote in unquoted field")             \
    X(CSV_ERR_TRAILING,       "unexpected data after closing quote")           \
    X(CSV_ERR_NO_SPACE,       "scratch or output buffer too small")            \
    X(CSV_ERR_TOO_MANY_COLS,  "row has more fields than the caller's array")   \
    X(CSV_ERR_NOMEM,          "out of memory")                                 \
    X(CSV_ERR_IO,             "i/o error")

#define CSV_EVENT_LIST(X)                                                      \
    X(CSV_EVENT_FIELD,     "field")                                            \
    X(CSV_EVENT_ROW,       "row")                                              \
    X(CSV_EVENT_END,       "end")                                              \
    X(CSV_EVENT_NEED_MORE, "need-more")                                        \
    X(CSV_EVENT_ERROR,     "error")

#define CSV__ENUM_ENTRY(id, msg) id,
typedef enum csv_error { CSV_ERROR_LIST(CSV__ENUM_ENTRY) CSV__ERROR_COUNT } csv_error;
typedef enum csv_event { CSV_EVENT_LIST(CSV__ENUM_ENTRY) CSV__EVENT_COUNT } csv_event;
#undef CSV__ENUM_ENTRY

CSV_API const char *csv_strerror(csv_error e);
CSV_API const char *csv_event_name(csv_event e);

/* =========================================================================
 * Options
 * ========================================================================= */

enum {
    CSV_FLAG_NONE         = 0,
    /* Reader */
    CSV_FLAG_STRICT       = 1u << 0, /* reject bare quotes and post-quote junk */
    CSV_FLAG_TRIM         = 1u << 1, /* strip spaces/tabs around fields        */
    CSV_FLAG_SKIP_EMPTY   = 1u << 2, /* blank lines produce no row             */
    CSV_FLAG_NO_BOM       = 1u << 3, /* do not strip a leading UTF-8 BOM       */
    CSV_FLAG_KEEP_SCRATCH = 1u << 4, /* keep un-escaped fields valid for the
                                        whole parse, not just the current row  */
    /* Writer */
    CSV_FLAG_CRLF         = 1u << 5, /* end rows with \r\n (RFC 4180) not \n   */
    CSV_FLAG_QUOTE_ALL    = 1u << 6  /* quote every field, not just those that
                                        need it                                */
};

typedef struct csv_opts {
    char     delimiter;  /* 0 -> ','  */
    char     quote;      /* 0 -> '"'  */
    char     comment;    /* 0 -> off; else skip lines starting with this char */
    char     escape;     /* 0 -> off; else `\x` yields x, as well as `""`.
                            Reader-side only: the writer always doubles quotes,
                            which every dialect understands. */
    unsigned flags;
    unsigned skip_lines; /* physical lines to drop before parsing starts */
} csv_opts;

/* Aggregate initialiser, usable in static storage: csv_opts o = CSV_OPTS_INIT; */
#define CSV_OPTS_INIT { ',', '"', 0, 0, 0, 0 }
CSV_API csv_opts csv_opts_default(void);

/* =========================================================================
 * Reader
 *
 * Lifetime contract, the only rule that matters:
 *
 *   A csv_str handed to you points either into the buffer you fed, or into
 *   your scratch buffer. It stays valid until the next csv_reader_feed(), and
 *   until the end of the current row (scratch is rewound per row unless you
 *   set CSV_FLAG_KEEP_SCRATCH). Copy it out if you need it longer.
 * ========================================================================= */

typedef struct csv_reader {
    /* --- public, read-only ------------------------------------------- */
    csv_str   field;   /* valid after CSV_EVENT_FIELD                    */
    size_t    row;     /* completed rows so far                          */
    size_t    col;     /* fields emitted in the current row              */
    size_t    line;    /* 1-based physical line, for error messages      */
    csv_error err;     /* sticky; CSV_OK until something goes wrong      */

    /* --- private ------------------------------------------------------ */
    csv_opts      opts;
    const char   *buf;
    char         *mut;   /* non-NULL in in-place mode: same memory as buf */
    size_t        len, pos, row_start;
    char         *scratch;
    size_t        scratch_cap, scratch_len;
    unsigned      skip_left;
    int           final;
    int           state;
    int           bom_done;
    unsigned char stop[256]; /* byte -> "ends an unquoted field" */
} csv_reader;

CSV_API void      csv_reader_init(csv_reader *r, const csv_opts *opts,
                                  void *scratch, size_t scratch_size);
CSV_API void      csv_reader_feed(csv_reader *r, const void *data, size_t len,
                                  int is_final);
CSV_API csv_event csv_next(csv_reader *r);
CSV_API csv_event csv_next_row(csv_reader *r, csv_str *out, size_t cap,
                               size_t *n_out);
/* Bytes of the current buffer that will never be looked at again. After
 * CSV_EVENT_NEED_MORE, move the tail down to offset 0 and refill. */
CSV_API size_t    csv_consumed(const csv_reader *r);

/* Convenience: init + feed(final) for a buffer you already hold entirely. */
CSV_API void csv_reader_init_buf(csv_reader *r, const csv_opts *opts,
                                 void *scratch, size_t scratch_size,
                                 const void *data, size_t len);

/* In-place mode: no scratch buffer at all.
 *
 * Un-escaping `""` only ever makes a field shorter, so if you own the buffer
 * and can let it be modified, the parser rewrites those fields where they
 * already are. Nothing is allocated, nothing is copied anywhere else, and
 * there is no size to get wrong.
 *
 * Requires the complete document up front -- streaming would re-parse bytes
 * this has already rewritten -- so csv_reader_feed() must not be called
 * afterwards. Field pointers stay valid for as long as your buffer does. */
CSV_API void csv_reader_init_mut(csv_reader *r, const csv_opts *opts,
                                 void *data, size_t len);

/* for (size_t n; ...) — iterate whole rows into a fixed array. */
#define csv_foreach_row(rd, arr, n_var)                                        \
    for (size_t n_var;                                                         \
         csv_next_row((rd), (arr), CSV_ARRAY_LEN(arr), &(n_var))               \
             == CSV_EVENT_ROW;)

/* Index of `name` in a header row, or CSV_NOT_FOUND. */
#define CSV_NOT_FOUND ((size_t)-1)
CSV_API size_t csv_find(const csv_str *fields, size_t n, const char *name);

/* =========================================================================
 * Writer
 *
 * Writes into a caller buffer. If the buffer fills and a sink is installed it
 * is drained automatically; otherwise `err` becomes CSV_ERR_NO_SPACE and
 * `needed` keeps counting, snprintf-style, so you can size the buffer exactly.
 * ========================================================================= */

typedef size_t (*csv_sink_fn)(void *ctx, const char *data, size_t len);

typedef struct csv_writer {
    /* --- public, read-only ------------------------------------------- */
    size_t      len;     /* bytes currently in buf                        */
    size_t      needed;  /* bytes the whole output would have taken       */
    csv_error   err;

    /* --- private ------------------------------------------------------ */
    char       *buf;
    size_t      cap;
    csv_sink_fn sink;
    void       *ctx;
    csv_opts    opts;
    int         nfield;
} csv_writer;

CSV_API void      csv_writer_init(csv_writer *w, void *buf, size_t cap,
                                  const csv_opts *opts);
CSV_API void      csv_writer_sink(csv_writer *w, csv_sink_fn fn, void *ctx);
CSV_API csv_error csv_write(csv_writer *w, const char *s, size_t n);
CSV_API csv_error csv_write_str(csv_writer *w, csv_str s);
CSV_API csv_error csv_write_cstr(csv_writer *w, const char *z);
CSV_API csv_error csv_write_row(csv_writer *w, const csv_str *f, size_t n);
CSV_API csv_error csv_write_row_end(csv_writer *w);
CSV_API csv_error csv_writer_flush(csv_writer *w);

#if !defined(CSV_NO_STDIO) && !defined(CSV_NO_ALLOC)
CSV_API size_t csv_sink_file(void *ctx, const char *data, size_t len); /* ctx: FILE* */
#endif

/* =========================================================================
 * Table: the allocating convenience layer. Everything above works without it.
 * Compile with -DCSV_NO_ALLOC to remove it entirely.
 * ========================================================================= */
#ifndef CSV_NO_ALLOC

typedef struct csv_table {
    csv_str *cells;   /* all cells, row-major, ragged                      */
    size_t  *rows;    /* rows[i] .. rows[i+1] is row i; nrows + 1 entries   */
    size_t   nrows;
    size_t   ncols;   /* widest row                                        */
    char    *text;    /* owned input copy, NULL when borrowing             */
    char    *arena;   /* owned un-escape scratch                           */
    size_t   ncells;
} csv_table;

/* Borrows `data`: it must outlive the table. Nothing is copied except the
 * contents of fields that contain escaped quotes. */
CSV_API csv_error csv_table_parse(csv_table *t, const void *data, size_t len,
                                  const csv_opts *opts);
CSV_API void      csv_table_free(csv_table *t);
CSV_API size_t    csv_table_ncols(const csv_table *t, size_t row);
CSV_API csv_str   csv_table_at(const csv_table *t, size_t row, size_t col);
/* Header helpers: row 0 is treated as the header. */
CSV_API size_t    csv_table_col(const csv_table *t, const char *name);
CSV_API csv_str   csv_table_get(const csv_table *t, size_t row, const char *name);

#ifndef CSV_NO_STDIO
/* Owns everything, including a copy of the file contents. */
CSV_API csv_error csv_table_load(csv_table *t, const char *path,
                                 const csv_opts *opts);
#endif
#endif /* CSV_NO_ALLOC */

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* CSV_H_INCLUDED */

/* =========================================================================
 * =========================================================================
 *                              IMPLEMENTATION
 * =========================================================================
 * ========================================================================= */

#ifdef CSV_IMPLEMENTATION
#ifndef CSV_IMPLEMENTATION_ONCE
#define CSV_IMPLEMENTATION_ONCE

#include <string.h>

#ifndef CSV_ASSERT
#   include <assert.h>
#   define CSV_ASSERT(x) assert(x)
#endif
#ifndef CSV_MEMCPY
#   define CSV_MEMCPY(d, s, n) memcpy((d), (s), (n))
#endif
#ifndef CSV_MEMMOVE
#   define CSV_MEMMOVE(d, s, n) memmove((d), (s), (n))
#endif
#ifndef CSV_MEMCHR
#   define CSV_MEMCHR(p, c, n) memchr((p), (c), (n))
#endif
#ifndef CSV_MEMCMP
#   define CSV_MEMCMP(a, b, n) memcmp((a), (b), (n))
#endif
#ifndef CSV_MEMSET
#   define CSV_MEMSET(p, c, n) memset((p), (c), (n))
#endif
#ifndef CSV_NO_ALLOC
#   include <stdlib.h>
#   ifndef CSV_MALLOC
#       define CSV_MALLOC(n)      malloc(n)
#       define CSV_REALLOC(p, n)  realloc((p), (n))
#       define CSV_FREE(p)        free(p)
#   endif
#endif

/* With CSV_STATIC the whole library is `static`, so every entry point the
 * caller happens not to use is an unused-function warning. Silence exactly
 * that, only in that configuration, and only for this file. */
#ifdef CSV_STATIC
#   if defined(__GNUC__) || defined(__clang__)
#       pragma GCC diagnostic push
#       pragma GCC diagnostic ignored "-Wunused-function"
#   elif defined(_MSC_VER)
#       pragma warning(push)
#       pragma warning(disable : 4505)
#   endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ misc */

CSV_API csv_str csv_str_make(const char *p, size_t n)
{
    csv_str s;
    s.ptr = p;
    s.len = n;
    return s;
}

CSV_API int csv_str_eq(csv_str a, csv_str b)
{
    return a.len == b.len && (a.len == 0 || CSV_MEMCMP(a.ptr, b.ptr, a.len) == 0);
}

CSV_API int csv_str_eq_cstr(csv_str a, const char *z)
{
    size_t n = strlen(z);
    return a.len == n && (n == 0 || CSV_MEMCMP(a.ptr, z, n) == 0);
}

#define CSV__NAME_ENTRY(id, msg) msg,
CSV_API const char *csv_strerror(csv_error e)
{
    static const char *const tab[] = { CSV_ERROR_LIST(CSV__NAME_ENTRY) };
    return (unsigned)e < CSV__ERROR_COUNT ? tab[e] : "unknown error";
}

CSV_API const char *csv_event_name(csv_event e)
{
    static const char *const tab[] = { CSV_EVENT_LIST(CSV__NAME_ENTRY) };
    return (unsigned)e < CSV__EVENT_COUNT ? tab[e] : "unknown event";
}
#undef CSV__NAME_ENTRY

CSV_API csv_opts csv_opts_default(void)
{
    csv_opts o;
    o.delimiter  = ',';
    o.quote      = '"';
    o.comment    = 0;
    o.escape     = 0;
    o.flags      = CSV_FLAG_NONE;
    o.skip_lines = 0;
    return o;
}

CSV_API size_t csv_find(const csv_str *fields, size_t n, const char *name)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (csv_str_eq_cstr(fields[i], name)) return i;
    return CSV_NOT_FOUND;
}

/* ---------------------------------------------------------------- reader */

enum {
    CSV__S_ROW_BEGIN,  /* positioned at the first byte of a record   */
    CSV__S_IN_ROW,     /* mid-record, next call parses a field       */
    CSV__S_ROW_EMIT,   /* record complete, owe the caller a ROW      */
    CSV__S_DONE,
    CSV__S_ERR
};

CSV_INLINE int csv__is_blank(char c, char delim)
{
    return (c == ' ' || c == '\t') && c != delim;
}

CSV_INLINE int csv__fail(csv_reader *r, csv_error e)
{
    r->err   = e;
    r->state = CSV__S_ERR;
    return -1;
}

/* Bump-allocate from the caller's scratch. Never moves earlier allocations,
 * which is what keeps previously returned fields valid. */
CSV_INLINE char *csv__scratch(csv_reader *r, size_t n)
{
    char *p;
    if (n > r->scratch_cap - r->scratch_len) { csv__fail(r, CSV_ERR_NO_SPACE); return NULL; }
    p = r->scratch + r->scratch_len;
    r->scratch_len += n;
    return p;
}

CSV_API void csv_reader_init(csv_reader *r, const csv_opts *opts,
                             void *scratch, size_t scratch_size)
{
    unsigned i;
    CSV_ASSERT(r != NULL);
    CSV_MEMSET(r, 0, sizeof *r);

    r->opts = opts ? *opts : csv_opts_default();
    if (!r->opts.delimiter) r->opts.delimiter = ',';
    if (!r->opts.quote)     r->opts.quote     = '"';
    CSV_ASSERT(r->opts.delimiter != r->opts.quote);
    CSV_ASSERT(r->opts.delimiter != '\n' && r->opts.delimiter != '\r');
    CSV_ASSERT(r->opts.quote != '\n' && r->opts.quote != '\r');

    r->scratch     = (char *)scratch;
    r->scratch_cap = scratch ? scratch_size : 0;
    r->skip_left   = r->opts.skip_lines;
    r->line        = 1;
    r->state       = CSV__S_ROW_BEGIN;

    /* Unquoted fields end at a delimiter or a line break. In strict mode a
     * quote also stops the scan so we can diagnose `a"b`; in the default
     * lenient mode such a quote is just data. */
    for (i = 0; i < 256; i++) r->stop[i] = 0;
    r->stop[(unsigned char)r->opts.delimiter] = 1;
    r->stop[(unsigned char)'\n']              = 1;
    r->stop[(unsigned char)'\r']              = 1;
    if (r->opts.flags & CSV_FLAG_STRICT)
        r->stop[(unsigned char)r->opts.quote] = 1;
}

CSV_API void csv_reader_feed(csv_reader *r, const void *data, size_t len,
                             int is_final)
{
    /* Feeding is only meaningful at a record boundary: after NEED_MORE, after
     * END, or before the first byte. Feeding mid-record would silently drop
     * the fields already emitted for it. */
    CSV_ASSERT(r->state == CSV__S_ROW_BEGIN || r->state == CSV__S_DONE ||
               r->state == CSV__S_ERR);
    CSV_ASSERT(r->mut == NULL && "in-place mode cannot stream");
    r->buf         = (const char *)data;
    r->len         = len;
    r->pos         = 0;
    r->row_start   = 0;
    r->final       = is_final;
    if (!(r->opts.flags & CSV_FLAG_KEEP_SCRATCH)) r->scratch_len = 0;
    if (r->state == CSV__S_DONE && len) r->state = CSV__S_ROW_BEGIN;
}

CSV_API void csv_reader_init_buf(csv_reader *r, const csv_opts *opts,
                                 void *scratch, size_t scratch_size,
                                 const void *data, size_t len)
{
    csv_reader_init(r, opts, scratch, scratch_size);
    csv_reader_feed(r, data, len, 1);
}

CSV_API void csv_reader_init_mut(csv_reader *r, const csv_opts *opts,
                                 void *data, size_t len)
{
    csv_reader_init(r, opts, NULL, 0);
    csv_reader_feed(r, data, len, 1);
    r->mut = (char *)data; /* set last: feed() asserts we are not in it yet */
}

CSV_API size_t csv_consumed(const csv_reader *r)
{
    return r->state == CSV__S_DONE ? r->len : r->row_start;
}

/* Rewind to the start of the partially seen record and ask for a refill. */
CSV_INLINE csv_event csv__need_more(csv_reader *r)
{
    CSV_ASSERT(!r->final);
    r->pos   = r->row_start;
    r->state = CSV__S_ROW_BEGIN;
    return CSV_EVENT_NEED_MORE;
}

/* Consume one line terminator at r->pos. 1 = done, 0 = need more input. */
CSV_INLINE int csv__eat_eol(csv_reader *r)
{
    if (r->buf[r->pos] == '\r') {
        if (r->pos + 1 >= r->len && !r->final) return 0; /* \r\n may straddle */
        r->pos++;
        if (r->pos < r->len && r->buf[r->pos] == '\n') r->pos++;
    } else {
        r->pos++;
    }
    r->line++;
    return 1;
}

/* Skip comment lines and, if asked, blank lines. 1 = ok, 0 = need more. */
CSV_INLINE int csv__skip_noise(csv_reader *r)
{
    for (;;) {
        char c;
        if (r->pos >= r->len) return 1; /* EOF or need-more, decided by caller */
        c = r->buf[r->pos];

        /* opts.skip_lines drops whole physical lines before parsing begins,
         * for the preamble junk that spreadsheets like to put above a header.
         * Physical, so a quoted newline in the preamble counts as a line. */
        if (r->skip_left) {
            const char *nl = (const char *)CSV_MEMCHR(r->buf + r->pos, '\n',
                                                      r->len - r->pos);
            if (!nl) {
                if (!r->final) return 0;
                r->pos = r->len;
                r->skip_left = 0;
            } else {
                r->pos = (size_t)(nl - r->buf) + 1;
                r->line++;
                r->skip_left--;
            }
            r->row_start = r->pos;
            continue;
        }

        if (r->opts.comment && c == r->opts.comment) {
            const char *nl = (const char *)CSV_MEMCHR(r->buf + r->pos, '\n',
                                                      r->len - r->pos);
            if (!nl) {
                if (!r->final) return 0;
                r->pos = r->len;
                return 1;
            }
            r->pos = (size_t)(nl - r->buf) + 1;
            r->line++;
            r->row_start = r->pos;
            continue;
        }
        if ((r->opts.flags & CSV_FLAG_SKIP_EMPTY) && (c == '\n' || c == '\r')) {
            if (!csv__eat_eol(r)) return 0;
            r->row_start = r->pos;
            continue;
        }
        return 1;
    }
}

/* Locate the closing quote of a field whose body starts at `from`.
 * Returns 1 and sets *end (index of the closing quote) and *nrem (bytes that
 * un-escaping will remove), 0 if more input is needed, -1 on error. */
CSV_INLINE int csv__scan_quoted(csv_reader *r, size_t from, size_t *end,
                                size_t *nrem)
{
    const char  *b = r->buf;
    const size_t n = r->len;
    const char   q = r->opts.quote;
    const char   e = r->opts.escape;
    size_t       i = from, rem = 0;

    if (e) {
        /* Escape-aware scan: one byte at a time, because an escape can hide
         * anything, including the closing quote. Only pays when opted in. */
        for (;;) {
            char c;
            if (i >= n) {
                if (!r->final) return 0;
                return csv__fail(r, CSV_ERR_UNTERMINATED);
            }
            c = b[i];
            if (c == e) {
                if (i + 1 >= n) {
                    if (!r->final) return 0;
                    return csv__fail(r, CSV_ERR_UNTERMINATED);
                }
                rem++;
                i += 2;
                continue;
            }
            if (c == q) {
                if (i + 1 == n && !r->final) return 0;
                if (i + 1 < n && b[i + 1] == q) { rem++; i += 2; continue; }
                *end  = i;
                *nrem = rem;
                return 1;
            }
            i++;
        }
    }

    for (;;) {
        const char *hit = (i < n) ? (const char *)CSV_MEMCHR(b + i, q, n - i)
                                  : NULL;
        size_t qi;
        if (!hit) {
            if (!r->final) return 0;
            return csv__fail(r, CSV_ERR_UNTERMINATED);
        }
        qi = (size_t)(hit - b);
        if (qi + 1 == n && !r->final) return 0; /* "" or end-of-field? */
        if (qi + 1 < n && b[qi + 1] == q) { rem++; i = qi + 2; continue; }
        *end  = qi;
        *nrem = rem;
        return 1;
    }
}

/* Where an un-escaped field goes: over itself in in-place mode, otherwise
 * bump-allocated from the caller's scratch. NULL means the scratch is full. */
CSV_INLINE char *csv__unescape_dst(csv_reader *r, size_t start, size_t need)
{
    if (r->mut) return r->mut + start;
    return csv__scratch(r, need);
}

/* Rewrite [from, to) with escapes resolved. `dst` may overlap the source as
 * long as it starts at or before it, which is exactly the in-place case:
 * un-escaping only ever removes bytes, so the write cursor never overtakes
 * the read cursor. Returns the number of bytes written. */
CSV_INLINE size_t csv__unescape(const csv_reader *r, char *dst, size_t from,
                                size_t to, int quoted)
{
    const char  *b = r->buf;
    const char   q = r->opts.quote;
    const char   e = r->opts.escape;
    size_t       i = from, w = 0;

    if (!e) {
        /* Common case: only `""` pairs, so copy whole runs between them. */
        while (i < to) {
            const char *hit = (const char *)CSV_MEMCHR(b + i, q, to - i);
            size_t      hi  = hit ? (size_t)(hit - b) : to;
            CSV_MEMMOVE(dst + w, b + i, hi - i);
            w += hi - i;
            if (!hit) break;
            dst[w++] = q;  /* keep one of the pair */
            i = hi + 2;    /* skip both */
        }
        return w;
    }

    while (i < to) {
        char c = b[i];
        if (c == e && i + 1 < to)  { dst[w++] = b[i + 1]; i += 2; continue; }
        if (quoted && c == q)      { dst[w++] = q;        i += 2; continue; }
        dst[w++] = c;
        i++;
    }
    return w;
}

CSV_INLINE void csv__count_lines(csv_reader *r, const char *p, size_t n)
{
    const char *nl;
    while (n && (nl = (const char *)CSV_MEMCHR(p, '\n', n)) != NULL) {
        r->line++;
        n -= (size_t)(nl - p) + 1;
        p  = nl + 1;
    }
}

/* Parse exactly one field starting at r->pos, consume its terminator.
 * 1 = field produced, 0 = need more input, -1 = error.
 *
 * Force-inlined: it has two callers (the event loop and the row loop) and
 * letting the compiler make its own decision costs ~15% on whichever one it
 * declines to inline into. */
CSV_HOT_INLINE int csv__field(csv_reader *r, int *ends_row)
{
    const char    *b     = r->buf;
    const size_t   n     = r->len;
    const unsigned flags = r->opts.flags;
    const char     dl    = r->opts.delimiter;
    const char     qc    = r->opts.quote;
    size_t         i     = r->pos;
    const char    *out   = b + i;
    size_t         outlen = 0;

    if (flags & CSV_FLAG_TRIM)
        while (i < n && csv__is_blank(b[i], dl)) i++;

    if (i < n && b[i] == qc) {
        /* ---------------------------------------------------- quoted ---- */
        size_t start = i + 1, end, nrem;
        int rc = csv__scan_quoted(r, start, &end, &nrem);
        if (rc <= 0) return rc;

        csv__count_lines(r, b + start, end - start);

        if (nrem == 0) {
            out    = b + start;
            outlen = end - start;
        } else {
            /* The exact output size is known before we copy anything: each
             * `""` pair and each escape drops exactly one byte. */
            size_t need = (end - start) - nrem;
            char  *dst  = csv__unescape_dst(r, start, need);
            size_t w;
            if (!dst) return -1;
            w = csv__unescape(r, dst, start, end, 1);
            CSV_ASSERT(w == need);
            out    = dst;
            outlen = w;
        }
        i = end + 1;

        if (flags & CSV_FLAG_TRIM)
            while (i < n && csv__is_blank(b[i], dl)) i++;

        /* Anything other than a delimiter, a line break or EOF here means the
         * file is malformed, e.g. `"ab"cd`. Strict mode rejects it; otherwise
         * we glue the stray bytes onto the field, which is what every
         * real-world tool does. */
        if (i < n && b[i] != dl && b[i] != '\n' && b[i] != '\r') {
            size_t js = i, jlen;
            char  *dst;
            if (flags & CSV_FLAG_STRICT) return csv__fail(r, CSV_ERR_TRAILING);
            while (i < n && !r->stop[(unsigned char)b[i]]) i++;
            if (i >= n && !r->final) return 0;
            jlen = i - js;
            /* In in-place mode the quoted part already sits at `start`, so the
             * stray bytes only have to be slid up against it. */
            dst = r->mut ? r->mut + (size_t)(out - b)
                         : csv__scratch(r, outlen + jlen);
            if (!dst) return -1;
            if (dst != out) CSV_MEMMOVE(dst, out, outlen);
            CSV_MEMMOVE(dst + outlen, b + js, jlen);
            out    = dst;
            outlen += jlen;
        }
    } else {
        /* -------------------------------------------------- unquoted ---- */
        size_t start = i, nrem = 0;
        if (CSV_LIKELY(!r->opts.escape)) {
            while (i < n && !r->stop[(unsigned char)b[i]]) i++;
        } else {
            while (i < n) {
                char c = b[i];
                if (c == r->opts.escape) {
                    if (i + 1 >= n) { i++; break; }
                    nrem++;
                    i += 2;
                    continue;
                }
                if (r->stop[(unsigned char)c]) break;
                i++;
            }
        }
        if (i >= n && !r->final) return 0;
        if ((flags & CSV_FLAG_STRICT) && i < n && b[i] == qc)
            return csv__fail(r, CSV_ERR_BARE_QUOTE);
        out    = b + start;
        outlen = i - start;
        if (nrem) {
            char *dst = csv__unescape_dst(r, start, outlen - nrem);
            if (!dst) return -1;
            outlen = csv__unescape(r, dst, start, i, 0);
            out    = dst;
        }
        if (flags & CSV_FLAG_TRIM)
            while (outlen && csv__is_blank(out[outlen - 1], dl)) outlen--;
    }

    /* --------------------------------------------------- terminator ---- */
    if (i >= n) {
        if (!r->final) return 0; /* the field may continue in the next chunk */
        *ends_row = 1;
    } else if (b[i] == dl) {
        i++;
        *ends_row = 0;
    } else {
        r->pos = i;
        if (!csv__eat_eol(r)) return 0;
        i = r->pos;
        *ends_row = 1;
    }

    r->pos       = i;
    r->field.ptr = out;
    r->field.len = outlen;
    return 1;
}

CSV_API csv_event csv_next(csv_reader *r)
{
    for (;;) {
        switch (r->state) {
        case CSV__S_ERR:
            return CSV_EVENT_ERROR;

        case CSV__S_DONE:
            return CSV_EVENT_END;

        case CSV__S_ROW_BEGIN: {
            r->col       = 0;
            r->row_start = r->pos;
            if (!(r->opts.flags & CSV_FLAG_KEEP_SCRATCH)) r->scratch_len = 0;

            if (!r->bom_done && !(r->opts.flags & CSV_FLAG_NO_BOM)) {
                static const char bom[3] = { (char)0xEF, (char)0xBB, (char)0xBF };
                if (r->len - r->pos >= 3) {
                    if (CSV_MEMCMP(r->buf + r->pos, bom, 3) == 0) r->pos += 3;
                    r->bom_done = 1;
                    r->row_start = r->pos;
                } else if (!r->final) {
                    return csv__need_more(r);
                } else {
                    r->bom_done = 1;
                }
            }

            if (!csv__skip_noise(r)) return csv__need_more(r);

            if (r->pos >= r->len) {
                if (!r->final) return csv__need_more(r);
                r->row_start = r->pos;
                r->state = CSV__S_DONE;
                return CSV_EVENT_END;
            }
            r->state = CSV__S_IN_ROW;
            continue;
        }

        case CSV__S_IN_ROW: {
            int ends_row = 0;
            int rc = csv__field(r, &ends_row);
            if (rc < 0) return CSV_EVENT_ERROR;
            if (rc == 0) return csv__need_more(r);
            r->col++;
            r->state = ends_row ? CSV__S_ROW_EMIT : CSV__S_IN_ROW;
            return CSV_EVENT_FIELD;
        }

        case CSV__S_ROW_EMIT:
            r->row++;
            r->state = CSV__S_ROW_BEGIN;
            return CSV_EVENT_ROW;

        default:
            CSV_ASSERT(0 && "unreachable");
            return CSV_EVENT_ERROR;
        }
    }
}

/* The first field goes through csv_next() so that all the start-of-record
 * housekeeping lives in exactly one place; the rest of the record is pulled
 * straight from csv__field(), which is worth roughly 15% on short fields
 * because it skips a state dispatch and an event round-trip per field. */
CSV_API csv_event csv_next_row(csv_reader *r, csv_str *out, size_t cap,
                               size_t *n_out)
{
    csv_event e = csv_next(r);
    size_t    k = 0;

    if (e != CSV_EVENT_FIELD) { *n_out = 0; return e; }

    for (;;) {
        if (CSV_UNLIKELY(k == cap)) {
            csv__fail(r, CSV_ERR_TOO_MANY_COLS);
            *n_out = 0;
            return CSV_EVENT_ERROR;
        }
        out[k++] = r->field;

        if (r->state == CSV__S_ROW_EMIT) {
            r->row++;
            r->state = CSV__S_ROW_BEGIN;
            *n_out   = k;
            return CSV_EVENT_ROW;
        }
        {
            int ends_row = 0;
            int rc = csv__field(r, &ends_row);
            if (CSV_UNLIKELY(rc <= 0)) {
                *n_out = 0;
                /* rc == 0 means the record is cut short: rewind and let the
                 * caller refill. The fields collected so far are dropped and
                 * re-emitted after the next feed. */
                return rc < 0 ? CSV_EVENT_ERROR : csv__need_more(r);
            }
            r->col++;
            r->state = ends_row ? CSV__S_ROW_EMIT : CSV__S_IN_ROW;
        }
    }
}

/* ---------------------------------------------------------------- writer */

CSV_API void csv_writer_init(csv_writer *w, void *buf, size_t cap,
                             const csv_opts *opts)
{
    CSV_MEMSET(w, 0, sizeof *w);
    w->buf  = (char *)buf;
    w->cap  = cap;
    w->opts = opts ? *opts : csv_opts_default();
    if (!w->opts.delimiter) w->opts.delimiter = ',';
    if (!w->opts.quote)     w->opts.quote     = '"';
}

CSV_API void csv_writer_sink(csv_writer *w, csv_sink_fn fn, void *ctx)
{
    w->sink = fn;
    w->ctx  = ctx;
}

CSV_API csv_error csv_writer_flush(csv_writer *w)
{
    if (w->len && w->sink) {
        if (w->sink(w->ctx, w->buf, w->len) != w->len) w->err = CSV_ERR_IO;
        w->len = 0;
    }
    return w->err;
}

CSV_INLINE void csv__put(csv_writer *w, const char *d, size_t n)
{
    w->needed += n;
    if (w->err) return;
    if (n > w->cap - w->len) {
        if (!w->sink) { w->err = CSV_ERR_NO_SPACE; return; }
        if (csv_writer_flush(w) != CSV_OK) return;
        if (n > w->cap) { /* larger than the whole buffer: pass it straight on */
            if (w->sink(w->ctx, d, n) != n) w->err = CSV_ERR_IO;
            return;
        }
    }
    CSV_MEMCPY(w->buf + w->len, d, n);
    w->len += n;
}

CSV_INLINE int csv__needs_quotes(const csv_writer *w, const char *s, size_t n)
{
    size_t i;
    if (w->opts.flags & CSV_FLAG_QUOTE_ALL) return 1;
    if (n == 0) return 0;
    if (csv__is_blank(s[0], w->opts.delimiter) ||
        csv__is_blank(s[n - 1], w->opts.delimiter)) return 1;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c == w->opts.delimiter || c == w->opts.quote ||
            c == '\n' || c == '\r') return 1;
    }
    return 0;
}

CSV_API csv_error csv_write(csv_writer *w, const char *s, size_t n)
{
    if (w->nfield++) csv__put(w, &w->opts.delimiter, 1);

    if (!csv__needs_quotes(w, s, n)) {
        csv__put(w, s, n);
    } else {
        size_t seg = 0, i;
        csv__put(w, &w->opts.quote, 1);
        for (i = 0; i < n; i++) {
            if (s[i] != w->opts.quote) continue;
            csv__put(w, s + seg, i - seg + 1);
            csv__put(w, &w->opts.quote, 1); /* double it */
            seg = i + 1;
        }
        csv__put(w, s + seg, n - seg);
        csv__put(w, &w->opts.quote, 1);
    }
    return w->err;
}

CSV_API csv_error csv_write_str(csv_writer *w, csv_str s)
{
    return csv_write(w, s.ptr, s.len);
}

CSV_API csv_error csv_write_cstr(csv_writer *w, const char *z)
{
    return csv_write(w, z, strlen(z));
}

CSV_API csv_error csv_write_row_end(csv_writer *w)
{
    csv__put(w, (w->opts.flags & CSV_FLAG_CRLF) ? "\r\n" : "\n",
             (w->opts.flags & CSV_FLAG_CRLF) ? 2 : 1);
    w->nfield = 0;
    return w->err;
}

CSV_API csv_error csv_write_row(csv_writer *w, const csv_str *f, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) csv_write_str(w, f[i]);
    return csv_write_row_end(w);
}

#if !defined(CSV_NO_STDIO) && !defined(CSV_NO_ALLOC)
CSV_API size_t csv_sink_file(void *ctx, const char *data, size_t len)
{
    return fwrite(data, 1, len, (FILE *)ctx);
}
#endif

/* ----------------------------------------------------------------- table */
#ifndef CSV_NO_ALLOC

CSV_API void csv_table_free(csv_table *t)
{
    if (!t) return;
    CSV_FREE(t->cells);
    CSV_FREE(t->rows);
    CSV_FREE(t->arena);
    CSV_FREE(t->text);
    CSV_MEMSET(t, 0, sizeof *t);
}

CSV_INLINE int csv__grow(void **p, size_t *cap, size_t need, size_t elem)
{
    size_t c = *cap ? *cap : 16;
    void  *q;
    if (need <= *cap) return 1;
    while (c < need) {
        if (c > (size_t)-1 / 2 / elem) return 0;
        c *= 2;
    }
    q = CSV_REALLOC(*p, c * elem);
    if (!q) return 0;
    *p   = q;
    *cap = c;
    return 1;
}

CSV_API csv_error csv_table_parse(csv_table *t, const void *data, size_t len,
                                  const csv_opts *opts)
{
    csv_reader rd;
    csv_opts   o = opts ? *opts : csv_opts_default();
    size_t     cell_cap = 0, row_cap = 0;
    csv_event  e;

    CSV_MEMSET(t, 0, sizeof *t);

    /* Un-escaping only ever shrinks a field, so `len` bytes of scratch is
     * enough for the entire document -- and because it is never reallocated,
     * every cell stays valid for the life of the table. */
    o.flags |= CSV_FLAG_KEEP_SCRATCH;
    if (len) {
        t->arena = (char *)CSV_MALLOC(len);
        if (!t->arena) return CSV_ERR_NOMEM;
    }

    if (!csv__grow((void **)&t->rows, &row_cap, 1, sizeof *t->rows)) {
        csv_table_free(t);
        return CSV_ERR_NOMEM;
    }
    t->rows[0] = 0;

    csv_reader_init_buf(&rd, &o, t->arena, len, data, len);

    while ((e = csv_next(&rd)) != CSV_EVENT_END) {
        if (e == CSV_EVENT_ERROR) {
            csv_error err = rd.err;      /* t->text is not ours yet: see load() */
            csv_table_free(t);
            return err;
        }
        if (e == CSV_EVENT_FIELD) {
            if (!csv__grow((void **)&t->cells, &cell_cap, t->ncells + 1,
                           sizeof *t->cells)) goto oom;
            t->cells[t->ncells++] = rd.field;
        } else if (e == CSV_EVENT_ROW) {
            size_t w;
            if (!csv__grow((void **)&t->rows, &row_cap, t->nrows + 2,
                           sizeof *t->rows)) goto oom;
            t->rows[++t->nrows] = t->ncells;
            w = t->rows[t->nrows] - t->rows[t->nrows - 1];
            if (w > t->ncols) t->ncols = w;
        }
    }
    return CSV_OK;

oom:
    csv_table_free(t);
    return CSV_ERR_NOMEM;
}

CSV_API size_t csv_table_ncols(const csv_table *t, size_t row)
{
    if (row >= t->nrows) return 0;
    return t->rows[row + 1] - t->rows[row];
}

CSV_API csv_str csv_table_at(const csv_table *t, size_t row, size_t col)
{
    csv_str empty;
    empty.ptr = "";
    empty.len = 0;
    if (row >= t->nrows || col >= csv_table_ncols(t, row)) return empty;
    return t->cells[t->rows[row] + col];
}

CSV_API size_t csv_table_col(const csv_table *t, const char *name)
{
    if (!t->nrows) return CSV_NOT_FOUND;
    return csv_find(t->cells + t->rows[0], csv_table_ncols(t, 0), name);
}

CSV_API csv_str csv_table_get(const csv_table *t, size_t row, const char *name)
{
    size_t c = csv_table_col(t, name);
    csv_str empty;
    empty.ptr = "";
    empty.len = 0;
    return c == CSV_NOT_FOUND ? empty : csv_table_at(t, row, c);
}

#ifndef CSV_NO_STDIO
CSV_API csv_error csv_table_load(csv_table *t, const char *path,
                                 const csv_opts *opts)
{
    FILE   *fp;
    char   *buf = NULL;
    size_t  len = 0, cap = 0;
    csv_error err;

    CSV_MEMSET(t, 0, sizeof *t);
    fp = fopen(path, "rb");
    if (!fp) return CSV_ERR_IO;

    for (;;) {
        size_t got;
        if (len == cap) {
            size_t ncap = cap ? cap * 2 : 65536;
            char  *nbuf = (char *)CSV_REALLOC(buf, ncap);
            if (!nbuf) { CSV_FREE(buf); fclose(fp); return CSV_ERR_NOMEM; }
            buf = nbuf;
            cap = ncap;
        }
        got = fread(buf + len, 1, cap - len, fp);
        len += got;
        if (got == 0) break;
    }
    if (ferror(fp)) { CSV_FREE(buf); fclose(fp); return CSV_ERR_IO; }
    fclose(fp);

    err = csv_table_parse(t, buf, len, opts);
    if (err != CSV_OK) { CSV_FREE(buf); return err; }
    t->text = buf; /* now owned by the table */
    return CSV_OK;
}
#endif /* CSV_NO_STDIO */
#endif /* CSV_NO_ALLOC */

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef CSV_STATIC
#   if defined(__GNUC__) || defined(__clang__)
#       pragma GCC diagnostic pop
#   elif defined(_MSC_VER)
#       pragma warning(pop)
#   endif
#endif

#endif /* CSV_IMPLEMENTATION_ONCE */
#endif /* CSV_IMPLEMENTATION */
