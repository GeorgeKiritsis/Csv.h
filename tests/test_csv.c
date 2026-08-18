/* Test suite for csv.h. Build: make test */

#define CSV_IMPLEMENTATION
#include "../csv.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------ harness -- */

static int g_checks, g_fails;
static const char *g_case;

#define CHECK(cond)                                                            \
    do {                                                                       \
        g_checks++;                                                            \
        if (!(cond)) {                                                         \
            g_fails++;                                                         \
            printf("  FAIL %s:%d  %s%s%s\n", __FILE__, __LINE__, #cond,        \
                   g_case ? "   case: " : "", g_case ? g_case : "");           \
        }                                                                      \
    } while (0)

#define CHECK_EQ_STR(got, want)                                                \
    do {                                                                       \
        g_checks++;                                                            \
        if (strcmp((got), (want)) != 0) {                                      \
            g_fails++;                                                         \
            printf("  FAIL %s:%d\n    got  <%s>\n    want <%s>\n", __FILE__,   \
                   __LINE__, (got), (want));                                   \
        }                                                                      \
    } while (0)

#define RUN(fn)                                                                \
    do {                                                                       \
        int before = g_fails;                                                  \
        printf("%-34s", #fn);                                                  \
        fflush(stdout);                                                        \
        fn();                                                                  \
        printf("%s\n", g_fails == before ? "ok" : "FAILED");                   \
        g_case = NULL;                                                         \
    } while (0)

/* ------------------------------------------------------------ helpers -- */

/* Canonical dump: fields joined by '|', rows terminated by '/', with control
 * characters and the separators themselves backslash-escaped. */
static void esc(char **p, csv_str s)
{
    size_t i;
    for (i = 0; i < s.len; i++) {
        char c = s.ptr[i];
        switch (c) {
        case '\n': *(*p)++ = '\\'; *(*p)++ = 'n'; break;
        case '\r': *(*p)++ = '\\'; *(*p)++ = 'r'; break;
        case '\t': *(*p)++ = '\\'; *(*p)++ = 't'; break;
        case '|':  *(*p)++ = '\\'; *(*p)++ = '|'; break;
        case '/':  *(*p)++ = '\\'; *(*p)++ = '/'; break;
        case '\\': *(*p)++ = '\\'; *(*p)++ = '\\'; break;
        default:   *(*p)++ = c;
        }
    }
}

/* One-shot parse of the whole buffer. Returns the sticky error. */
static csv_error dump(const char *in, size_t len, const csv_opts *o, char *out)
{
    char       scratch[8192];
    csv_reader rd;
    char      *p = out;
    csv_event  e;
    int        first_in_row = 1;

    csv_reader_init_buf(&rd, o, scratch, sizeof scratch, in, len);
    while ((e = csv_next(&rd)) != CSV_EVENT_END) {
        if (e == CSV_EVENT_ERROR) break;
        if (e == CSV_EVENT_FIELD) {
            if (!first_in_row) *p++ = '|';
            esc(&p, rd.field);
            first_in_row = 0;
        } else if (e == CSV_EVENT_ROW) {
            *p++ = '/';
            first_in_row = 1;
        }
    }
    *p = 0;
    return rd.err;
}

/* Same document, fed `chunk` bytes at a time through a sliding window. */
static csv_error dump_streamed(const char *in, size_t len, const csv_opts *o,
                               size_t chunk, char *out)
{
    char       scratch[8192], window[8192];
    csv_reader rd;
    char      *p = out;
    size_t     wlen = 0, fed = 0;

    csv_reader_init(&rd, o, scratch, sizeof scratch);
    for (;;) {
        size_t take = len - fed < chunk ? len - fed : chunk;
        int    final;
        if (take > sizeof window - wlen) take = sizeof window - wlen;
        memcpy(window + wlen, in + fed, take);
        wlen += take;
        fed  += take;
        final = (fed == len);
        csv_reader_feed(&rd, window, wlen, final);

        for (;;) {
            csv_str   row[64];
            size_t    n, i, consumed;
            csv_event e = csv_next_row(&rd, row, CSV_ARRAY_LEN(row), &n);
            if (e == CSV_EVENT_ROW) {
                for (i = 0; i < n; i++) {
                    if (i) *p++ = '|';
                    esc(&p, row[i]);
                }
                *p++ = '/';
                continue;
            }
            if (e == CSV_EVENT_NEED_MORE) {
                consumed = csv_consumed(&rd);
                memmove(window, window + consumed, wlen - consumed);
                wlen -= consumed;
                if (wlen == sizeof window) { *p = 0; return CSV_ERR_NO_SPACE; }
                break;
            }
            *p = 0;
            return rd.err; /* END or ERROR */
        }
    }
}

/* ---------------------------------------------------------- test cases -- */

typedef struct {
    const char *in;
    const char *want;
    unsigned    flags;
    char        delim;
    char        comment;
    char        escape;
    unsigned    skip;
} tcase;

static void run_table(const tcase *t, size_t n)
{
    char out[8192];
    size_t i, c;

    for (i = 0; i < n; i++) {
        csv_opts o = csv_opts_default();
        o.flags      = t[i].flags;
        o.comment    = t[i].comment;
        o.escape     = t[i].escape;
        o.skip_lines = t[i].skip;
        if (t[i].delim) o.delimiter = t[i].delim;
        g_case = t[i].in;

        CHECK(dump(t[i].in, strlen(t[i].in), &o, out) == CSV_OK);
        CHECK_EQ_STR(out, t[i].want);

        /* Every document must parse identically no matter how it is sliced. */
        for (c = 1; c <= 5; c++) {
            char sout[8192];
            CHECK(dump_streamed(t[i].in, strlen(t[i].in), &o, c, sout) == CSV_OK);
            CHECK_EQ_STR(sout, t[i].want);
        }
    }
    g_case = NULL;
}

static void test_basics(void)
{
    static const tcase t[] = {
        { "",                    "",              0, 0, 0 },
        { "a",                   "a/",            0, 0, 0 },
        { "a,b,c",               "a|b|c/",        0, 0, 0 },
        { "a,b,c\n",             "a|b|c/",        0, 0, 0 },
        { "a,b\nc,d\n",          "a|b/c|d/",      0, 0, 0 },
        { "a,b\nc,d",            "a|b/c|d/",      0, 0, 0 },
        { "a,,b",                "a||b/",         0, 0, 0 },
        { ",,",                  "||/",           0, 0, 0 },
        { "a,b,c\nd\n",          "a|b|c/d/",      0, 0, 0 }, /* ragged */
        { "\n",                  "/",             0, 0, 0 },
        { "a\n\nb\n",            "a//b/",         0, 0, 0 },
    };
    run_table(t, CSV_ARRAY_LEN(t));
}

static void test_line_endings(void)
{
    static const tcase t[] = {
        { "a,b\r\nc,d\r\n",      "a|b/c|d/",      0, 0, 0 },
        { "a,b\r\nc,d",          "a|b/c|d/",      0, 0, 0 },
        { "a\rb\r",              "a/b/",          0, 0, 0 }, /* bare CR */
        { "a,b\r\n",             "a|b/",          0, 0, 0 },
        { "\r\n",                "/",             0, 0, 0 },
    };
    run_table(t, CSV_ARRAY_LEN(t));
}

static void test_quoting(void)
{
    static const tcase t[] = {
        { "\"a\",\"b\"",             "a|b/",           0, 0, 0 },
        { "\"a,b\",c",               "a,b|c/",         0, 0, 0 },
        { "\"a\nb\",c",              "a\\nb|c/",       0, 0, 0 },
        { "\"a\r\nb\"",              "a\\r\\nb/",      0, 0, 0 },
        { "\"a\"\"b\"",              "a\"b/",          0, 0, 0 },
        { "\"\"\"\"",                "\"/",            0, 0, 0 },
        { "\"\"",                    "/",              0, 0, 0 },
        { "\"\",\"\"",               "|/",             0, 0, 0 },
        { "a,\"b\"\"c\",d",          "a|b\"c|d/",      0, 0, 0 },
        { "\"a\"\"\"\"b\"",          "a\"\"b/",        0, 0, 0 },
        { "\"x\",\"y\"\"z\"\n1,2",   "x|y\"z/1|2/",    0, 0, 0 },
        { "\"multi\nline\",\"two\ntwo\"", "multi\\nline|two\\ntwo/", 0, 0, 0 },
    };
    run_table(t, CSV_ARRAY_LEN(t));
}

static void test_options(void)
{
    static const tcase t[] = {
        { "\xEF\xBB\xBF" "a,b",  "a|b/",          0, 0, 0 },
        { "\xEF\xBB\xBF" "a,b",  "\xEF\xBB\xBF" "a|b/", CSV_FLAG_NO_BOM, 0, 0 },
        { "a\tb\tc",             "a|b|c/",        0, '\t', 0 },
        { "a;b\nc;d",            "a|b/c|d/",      0, ';',  0 },
        { "# note\na,b",         "a|b/",          0, 0, '#' },
        { "a,b\n# note\nc,d",    "a|b/c|d/",      0, 0, '#' },
        { "# only a comment",    "",              0, 0, '#' },
        { "a\n\n\nb\n",          "a/b/",          CSV_FLAG_SKIP_EMPTY, 0, 0 },
        { "  a , b  ",           "a|b/",          CSV_FLAG_TRIM, 0, 0 },
        { " \"a b\" , c ",       "a b|c/",        CSV_FLAG_TRIM, 0, 0 },
        { "a , b",               "a | b/",        0, 0, 0 }, /* no TRIM: kept */
        { "\"a\"\"b\" , c",      "a\"b|c/",       CSV_FLAG_TRIM, 0, 0 },
    };
    run_table(t, CSV_ARRAY_LEN(t));
}

static void test_lenient_recovery(void)
{
    /* Malformed input that real-world files are full of. Default mode keeps
     * the bytes; strict mode rejects them. */
    static const tcase t[] = {
        { "a\"b,c",              "a\"b|c/",       0, 0, 0 },
        { "\"ab\"cd,x",          "abcd|x/",       0, 0, 0 },
        { "\"ab\"\"cd\"ef",      "ab\"cdef/",     0, 0, 0 },
        { "5\" pipe,x",          "5\" pipe|x/",   0, 0, 0 },
    };
    run_table(t, CSV_ARRAY_LEN(t));
}

static void test_errors(void)
{
    char out[4096];
    csv_opts strict = csv_opts_default();
    csv_opts o      = csv_opts_default();
    strict.flags    = CSV_FLAG_STRICT;

    CHECK(dump("\"abc", 4, &o, out) == CSV_ERR_UNTERMINATED);
    CHECK(dump("a,\"bc", 5, &o, out) == CSV_ERR_UNTERMINATED);
    CHECK(dump("a\"b", 3, &strict, out) == CSV_ERR_BARE_QUOTE);
    CHECK(dump("\"ab\"cd", 6, &strict, out) == CSV_ERR_TRAILING);
    CHECK(dump("a,b\nc,d", 7, &strict, out) == CSV_OK);

    /* Streaming must reach the same verdict. */
    CHECK(dump_streamed("\"abc", 4, &o, 1, out) == CSV_ERR_UNTERMINATED);
    CHECK(dump_streamed("a\"b", 3, &strict, 1, out) == CSV_ERR_BARE_QUOTE);

    /* Error location. */
    {
        csv_reader rd;
        char scratch[64];
        csv_event e;
        csv_reader_init_buf(&rd, &o, scratch, sizeof scratch,
                            "a,b\nc,d\n\"oops\n", 14);
        while ((e = csv_next(&rd)) != CSV_EVENT_END && e != CSV_EVENT_ERROR) {}
        CHECK(rd.err == CSV_ERR_UNTERMINATED);
        CHECK(rd.row == 2);
        CHECK(rd.line == 3); /* reported where the bad field started */
    }
}

static void test_scratch_limits(void)
{
    csv_reader rd;
    char       tiny[2];
    csv_opts   o = csv_opts_default();

    /* Fields that need no un-escaping never touch the scratch buffer... */
    csv_reader_init_buf(&rd, &o, NULL, 0, "\"a long quoted field\",b", 23);
    CHECK(csv_next(&rd) == CSV_EVENT_FIELD);
    CHECK(rd.field.len == 19);
    CHECK(csv_next(&rd) == CSV_EVENT_FIELD);
    CHECK(csv_next(&rd) == CSV_EVENT_ROW);
    CHECK(csv_next(&rd) == CSV_EVENT_END);
    CHECK(rd.err == CSV_OK);

    /* ...but escaped ones do, and running out is a clean error. */
    csv_reader_init_buf(&rd, &o, tiny, sizeof tiny, "\"aa\"\"bb\"", 8);
    CHECK(csv_next(&rd) == CSV_EVENT_ERROR);
    CHECK(rd.err == CSV_ERR_NO_SPACE);

    /* Scratch is rewound per row, so a long document needs only enough for
     * the widest single row. */
    {
        char small[8];
        int  i;
        csv_reader_init(&rd, &o, small, sizeof small);
        csv_reader_feed(&rd, "\"a\"\"b\"\n\"a\"\"b\"\n\"a\"\"b\"\n", 21, 1);
        for (i = 0; i < 3; i++) {
            CHECK(csv_next(&rd) == CSV_EVENT_FIELD);
            CHECK(csv_str_eq_cstr(rd.field, "a\"b"));
            CHECK(csv_next(&rd) == CSV_EVENT_ROW);
        }
        CHECK(csv_next(&rd) == CSV_EVENT_END);
    }
}

static void test_row_api(void)
{
    csv_reader rd;
    char       scratch[256];
    csv_str    row[4];
    size_t     n;
    csv_opts   o = csv_opts_default();

    csv_reader_init_buf(&rd, &o, scratch, sizeof scratch, "a,b\nc,d\n", 8);
    CHECK(csv_next_row(&rd, row, 4, &n) == CSV_EVENT_ROW);
    CHECK(n == 2 && csv_str_eq_cstr(row[0], "a") && csv_str_eq_cstr(row[1], "b"));
    CHECK(csv_next_row(&rd, row, 4, &n) == CSV_EVENT_ROW);
    CHECK(n == 2 && csv_str_eq_cstr(row[0], "c"));
    CHECK(csv_next_row(&rd, row, 4, &n) == CSV_EVENT_END);

    /* Too many columns for the caller's array is an error, not a smash. */
    csv_reader_init_buf(&rd, &o, scratch, sizeof scratch, "1,2,3,4,5\n", 10);
    CHECK(csv_next_row(&rd, row, 4, &n) == CSV_EVENT_ERROR);
    CHECK(rd.err == CSV_ERR_TOO_MANY_COLS);

    /* The foreach macro. */
    {
        int rows = 0, fields = 0;
        csv_reader_init_buf(&rd, &o, scratch, sizeof scratch, "a,b\nc,d,e\n", 10);
        csv_foreach_row (&rd, row, k) {
            rows++;
            fields += (int)k;
        }
        CHECK(rows == 2);
        CHECK(fields == 5);
        CHECK(rd.err == CSV_OK);
    }

    /* Header lookup helper. */
    {
        csv_reader_init_buf(&rd, &o, scratch, sizeof scratch, "id,name,qty\n", 12);
        CHECK(csv_next_row(&rd, row, 4, &n) == CSV_EVENT_ROW);
        CHECK(csv_find(row, n, "name") == 1);
        CHECK(csv_find(row, n, "qty") == 2);
        CHECK(csv_find(row, n, "nope") == CSV_NOT_FOUND);
    }
}

/* ------------------------------------------------------------- writer -- */

static void test_writer(void)
{
    char       buf[512];
    csv_writer w;
    csv_opts   o = csv_opts_default();

    csv_writer_init(&w, buf, sizeof buf, &o);
    csv_write_cstr(&w, "plain");
    csv_write_cstr(&w, "with,comma");
    csv_write_cstr(&w, "with\"quote");
    csv_write_cstr(&w, "with\nnewline");
    csv_write_cstr(&w, " leading");
    csv_write_cstr(&w, "");
    csv_write_row_end(&w);
    buf[w.len] = 0;
    CHECK_EQ_STR(buf,
        "plain,\"with,comma\",\"with\"\"quote\",\"with\nnewline\",\" leading\",\n");
    CHECK(w.err == CSV_OK);

    /* CRLF + quote-all. */
    o.flags = CSV_FLAG_CRLF | CSV_FLAG_QUOTE_ALL;
    csv_writer_init(&w, buf, sizeof buf, &o);
    csv_write_cstr(&w, "a");
    csv_write_cstr(&w, "b");
    csv_write_row_end(&w);
    buf[w.len] = 0;
    CHECK_EQ_STR(buf, "\"a\",\"b\"\r\n");

    /* Overflow is reported, never written past, and `needed` sizes the buffer
     * exactly like snprintf. */
    {
        char small[8];
        size_t needed;
        csv_writer_init(&w, small, sizeof small, NULL);
        csv_write_cstr(&w, "0123456789");
        csv_write_cstr(&w, "abc");
        csv_write_row_end(&w);
        CHECK(w.err == CSV_ERR_NO_SPACE);
        needed = w.needed;
        CHECK(needed == strlen("0123456789,abc\n"));
        {
            char *big = (char *)malloc(needed + 1);
            csv_writer_init(&w, big, needed, NULL);
            csv_write_cstr(&w, "0123456789");
            csv_write_cstr(&w, "abc");
            csv_write_row_end(&w);
            big[w.len] = 0;
            CHECK(w.err == CSV_OK);
            CHECK_EQ_STR(big, "0123456789,abc\n");
            free(big);
        }
    }
}

/* A tiny sink that appends to a fixed buffer, to exercise auto-flushing. */
typedef struct { char *p; size_t len, cap; } sinkbuf;

static size_t sink_append(void *ctx, const char *d, size_t n)
{
    sinkbuf *s = (sinkbuf *)ctx;
    if (n > s->cap - s->len) return 0;
    memcpy(s->p + s->len, d, n);
    s->len += n;
    return n;
}

static void test_writer_sink(void)
{
    char       out[4096], tiny[4];
    sinkbuf    s;
    csv_writer w;
    int        i;

    s.p = out; s.len = 0; s.cap = sizeof out;
    csv_writer_init(&w, tiny, sizeof tiny, NULL);
    csv_writer_sink(&w, sink_append, &s);
    for (i = 0; i < 100; i++) {
        csv_write_cstr(&w, "field with, comma");
        csv_write_cstr(&w, "x");
        csv_write_row_end(&w);
    }
    csv_writer_flush(&w);
    CHECK(w.err == CSV_OK);
    CHECK(s.len == w.needed);
    CHECK(s.len == 100 * strlen("\"field with, comma\",x\n"));
}

/* ---------------------------------------------------- round-trip fuzz -- */

/* unsigned long long: 64-bit on every platform, so the random stream (and
 * therefore the documents tested) is identical everywhere, and MSVC's 32-bit
 * unsigned long does not truncate the seed under /W4 /WX. */
static unsigned long long g_seed = 88172645463325252ULL;
static unsigned rnd(unsigned n)
{
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 7;
    g_seed ^= g_seed << 17;
    return (unsigned)(g_seed % n);
}

static void test_roundtrip(void)
{
    static const char alphabet[] = "ab, \"\r\n\t;|x";
    char    text[16384], scratch[16384];
    csv_str fields[8][8];
    char    store[8][8][32];
    size_t  widths[8];
    int     iter;

    for (iter = 0; iter < 2000; iter++) {
        csv_writer w;
        csv_reader rd;
        size_t     nrows = 1 + rnd(8), i, j;
        csv_opts   o = csv_opts_default();
        if (iter & 1) o.flags |= CSV_FLAG_CRLF;
        if (iter % 7 == 0) o.delimiter = ';';

        csv_writer_init(&w, text, sizeof text, &o);
        for (i = 0; i < nrows; i++) {
            widths[i] = 1 + rnd(8);
            for (j = 0; j < widths[i]; j++) {
                size_t len = rnd(20), k;
                for (k = 0; k < len; k++)
                    store[i][j][k] = alphabet[rnd(sizeof alphabet - 1)];
                fields[i][j] = csv_str_make(store[i][j], len);
                csv_write_str(&w, fields[i][j]);
            }
            csv_write_row_end(&w);
        }
        CHECK(w.err == CSV_OK);

        csv_reader_init_buf(&rd, &o, scratch, sizeof scratch, text, w.len);
        for (i = 0; i < nrows; i++) {
            csv_str got[8];
            size_t  n;
            if (csv_next_row(&rd, got, 8, &n) != CSV_EVENT_ROW) { CHECK(0); break; }
            CHECK(n == widths[i]);
            if (n != widths[i]) break;
            for (j = 0; j < n; j++) CHECK(csv_str_eq(got[j], fields[i][j]));
        }
        CHECK(rd.err == CSV_OK);
    }
}

/* -------------------------------------------------------------- table -- */

static void test_table(void)
{
    static const char doc[] =
        "id,name,note\n"
        "1,widget,\"says \"\"hi\"\"\"\n"
        "2,gadget,\"multi\nline\"\n"
        "3,short\n";
    csv_table t;
    csv_error e = csv_table_parse(&t, doc, sizeof doc - 1, NULL);

    CHECK(e == CSV_OK);
    CHECK(t.nrows == 4);
    CHECK(t.ncols == 3);
    CHECK(csv_table_ncols(&t, 3) == 2);
    CHECK(csv_str_eq_cstr(csv_table_at(&t, 1, 1), "widget"));
    CHECK(csv_str_eq_cstr(csv_table_at(&t, 1, 2), "says \"hi\""));
    CHECK(csv_str_eq_cstr(csv_table_at(&t, 2, 2), "multi\nline"));
    CHECK(csv_table_at(&t, 3, 2).len == 0);   /* ragged: absent cell */
    CHECK(csv_table_at(&t, 99, 0).len == 0);  /* out of range */
    CHECK(csv_table_col(&t, "note") == 2);
    CHECK(csv_table_col(&t, "missing") == CSV_NOT_FOUND);
    CHECK(csv_str_eq_cstr(csv_table_get(&t, 2, "name"), "gadget"));
    /* Un-escaped cells from different rows must all still be valid. */
    CHECK(csv_str_eq_cstr(csv_table_at(&t, 1, 2), "says \"hi\""));
    csv_table_free(&t);

    /* Errors propagate and free everything. */
    e = csv_table_parse(&t, "\"unterminated", 13, NULL);
    CHECK(e == CSV_ERR_UNTERMINATED);
    csv_table_free(&t);

    /* Empty input. */
    e = csv_table_parse(&t, "", 0, NULL);
    CHECK(e == CSV_OK);
    CHECK(t.nrows == 0);
    csv_table_free(&t);
}

static void test_table_load(void)
{
    const char *path = "test_tmp.csv";
    FILE       *fp   = fopen(path, "wb");
    csv_table   t;

    CHECK(fp != NULL);
    if (!fp) return;
    fputs("a,b\n1,\"2,5\"\n", fp);
    fclose(fp);

    CHECK(csv_table_load(&t, path, NULL) == CSV_OK);
    CHECK(t.nrows == 2);
    CHECK(csv_str_eq_cstr(csv_table_get(&t, 1, "b"), "2,5"));
    csv_table_free(&t);
    remove(path);

    CHECK(csv_table_load(&t, "definitely-not-here.csv", NULL) == CSV_ERR_IO);
}

/* opts.escape and opts.skip_lines: the dialects RFC 4180 does not cover. */
static void test_dialects(void)
{
    static const tcase t[] = {
        /* backslash escapes, quoted and unquoted */
        { "a\\,b,c",             "a,b|c/",        0, 0, 0, '\\', 0 },
        { "\"a\\\"b\",c",        "a\"b|c/",       0, 0, 0, '\\', 0 },
        { "\"a\\\\b\"",          "a\\\\b/",       0, 0, 0, '\\', 0 },
        { "\"a\\nb\"",           "anb/",          0, 0, 0, '\\', 0 },
        { "\"a\"\"b\\\"c\"",     "a\"b\"c/",      0, 0, 0, '\\', 0 },
        { "a\\\nb,c",            "a\\nb|c/",      0, 0, 0, '\\', 0 },
        { "x\\;y;z",             "x;y|z/",        0, ';', 0, '\\', 0 },
        /* a backslash is ordinary data when the option is off */
        { "a\\,b",               "a\\\\|b/",      0, 0, 0, 0,    0 },
        /* preamble skipping */
        { "junk\nmore junk\nid,name\n1,x\n", "id|name/1|x/", 0, 0, 0, 0, 2 },
        { "junk\nid,name\n",     "id|name/",      0, 0, 0, 0,    1 },
        { "one line\n",          "",              0, 0, 0, 0,    3 },
        { "no trailing newline", "",              0, 0, 0, 0,    1 },
        { "skip\n#c\na,b\n",     "a|b/",          0, 0, '#', 0,  1 },
    };
    run_table(t, CSV_ARRAY_LEN(t));

    /* An escape with nothing after it is a broken document. */
    {
        char     out[256];
        csv_opts o = csv_opts_default();
        o.escape = '\\';
        CHECK(dump("\"abc\\", 5, &o, out) == CSV_ERR_UNTERMINATED);
    }
}

/* In-place mode: no scratch buffer, fields rewritten over themselves. */
static void test_inplace(void)
{
    char doc[] = "\"say \"\"hi\"\"\",\"a\"\"b\",plain\n"
                 "\"x\"\"y\",\"ab\"cd,2\n";
    char       copy[sizeof doc];
    csv_reader rd;
    csv_str    r0[8], r1[8];
    size_t     n0, n1;

    memcpy(copy, doc, sizeof doc);
    csv_reader_init_mut(&rd, NULL, copy, sizeof doc - 1);

    CHECK(csv_next_row(&rd, r0, 8, &n0) == CSV_EVENT_ROW);
    CHECK(n0 == 3);
    CHECK(csv_str_eq_cstr(r0[0], "say \"hi\""));
    CHECK(csv_str_eq_cstr(r0[1], "a\"b"));
    CHECK(csv_str_eq_cstr(r0[2], "plain"));

    CHECK(csv_next_row(&rd, r1, 8, &n1) == CSV_EVENT_ROW);
    CHECK(n1 == 3);
    CHECK(csv_str_eq_cstr(r1[0], "x\"y"));
    CHECK(csv_str_eq_cstr(r1[1], "abcd"));   /* junk-after-quote, in place */
    CHECK(csv_str_eq_cstr(r1[2], "2"));

    {   /* csv_next_row zeroes the count on END, so keep the widths */
        size_t end_n;
        CHECK(csv_next_row(&rd, r0, 8, &end_n) == CSV_EVENT_END);
        CHECK(end_n == 0);
    }
    CHECK(rd.err == CSV_OK);

    /* Unlike scratch mode, earlier rows stay valid for the whole document. */
    CHECK(csv_str_eq_cstr(r1[0], "x\"y"));

    /* Every field points inside the caller's buffer -- nothing was copied
     * anywhere else, and no allocation happened. */
    CHECK(r0[0].ptr >= copy && r0[0].ptr < copy + sizeof copy);
    CHECK(r1[1].ptr >= copy && r1[1].ptr < copy + sizeof copy);

    /* It also works with escapes, still without a scratch buffer. */
    {
        char       raw[] = "\"a\\\"b\",c\n";
        csv_opts   o  = csv_opts_default();
        csv_reader r3;
        csv_str    e0[8];
        size_t     m0;
        o.escape = '\\';
        csv_reader_init_mut(&r3, &o, raw, sizeof raw - 1);
        CHECK(csv_next_row(&r3, e0, 8, &m0) == CSV_EVENT_ROW);
        CHECK(m0 == 2);
        CHECK(csv_str_eq_cstr(e0[0], "a\"b"));
        CHECK(r3.err == CSV_OK);
    }

    /* Same document, same answers, whichever mode you use. */
    {
        char       scratch[256];
        char       again[sizeof doc];
        csv_reader r2;
        csv_str    f[8];
        size_t     m;
        int        row = 0;
        memcpy(again, doc, sizeof doc);
        csv_reader_init_buf(&r2, NULL, scratch, sizeof scratch, again,
                            sizeof doc - 1);
        while (csv_next_row(&r2, f, 8, &m) == CSV_EVENT_ROW) {
            size_t k;
            csv_str *want = row ? r1 : r0;
            CHECK(m == (row ? n1 : n0));
            for (k = 0; k < m; k++) CHECK(csv_str_eq(f[k], want[k]));
            row++;
        }
        CHECK(row == 2);
    }
}

/* Nasty option interactions and degenerate inputs. */
static void test_edge_cases(void)
{
    char       out[4096];
    csv_opts   o = csv_opts_default();
    csv_reader rd;
    char       scratch[64];

    /* A document that is nothing but a BOM. */
    CHECK(dump("\xEF\xBB\xBF", 3, &o, out) == CSV_OK);
    CHECK_EQ_STR(out, "");
    CHECK(dump_streamed("\xEF\xBB\xBF", 3, &o, 1, out) == CSV_OK);
    CHECK_EQ_STR(out, "");

    /* Truncated BOM prefixes must not be mistaken for one. */
    CHECK(dump("\xEF\xBB", 2, &o, out) == CSV_OK);
    CHECK_EQ_STR(out, "\xEF\xBB/");

    /* Runs of quotes. */
    CHECK(dump("\"\"\"\"\"\"", 6, &o, out) == CSV_OK);
    CHECK_EQ_STR(out, "\"\"/");
    CHECK(dump("\"", 1, &o, out) == CSV_ERR_UNTERMINATED);

    /* Comment character colliding with the delimiter: the delimiter wins for
     * everything except the first byte of a record. */
    o.comment = ',';
    CHECK(dump(",a,b\nc,d\n", 9, &o, out) == CSV_OK);
    CHECK_EQ_STR(out, "c|d/");
    o = csv_opts_default();

    /* CRLF-terminated blank lines skipped across every chunk boundary. */
    {
        csv_opts s = csv_opts_default();
        size_t   c;
        s.flags = CSV_FLAG_SKIP_EMPTY;
        for (c = 1; c <= 4; c++) {
            CHECK(dump_streamed("a\r\n\r\n\r\nb\r\n", 10, &s, c, out) == CSV_OK);
            CHECK_EQ_STR(out, "a/b/");
        }
    }

    /* TRIM must not eat anything inside quotes, and must cope with a field
     * that is entirely blanks. */
    {
        csv_opts t = csv_opts_default();
        t.flags = CSV_FLAG_TRIM;
        CHECK(dump("  ,\" x \" ,  ", 12, &t, out) == CSV_OK);
        CHECK_EQ_STR(out, "| x |/");
    }

    /* KEEP_SCRATCH means escaped fields accumulate: exhaustion is clean. */
    {
        csv_opts k = csv_opts_default();
        int      i, hit = 0;
        k.flags = CSV_FLAG_KEEP_SCRATCH;
        csv_reader_init(&rd, &k, scratch, 8);
        csv_reader_feed(&rd, "\"a\"\"b\"\n\"a\"\"b\"\n\"a\"\"b\"\n", 21, 1);
        for (i = 0; i < 8; i++) {
            csv_event e = csv_next(&rd);
            if (e == CSV_EVENT_ERROR) { hit = 1; break; }
            if (e == CSV_EVENT_END) break;
        }
        CHECK(hit == 1);
        CHECK(rd.err == CSV_ERR_NO_SPACE);
        /* Sticky: it stays failed. */
        CHECK(csv_next(&rd) == CSV_EVENT_ERROR);
    }

    /* Writer with no room at all, and no sink. */
    {
        csv_writer w;
        csv_writer_init(&w, NULL, 0, NULL);
        csv_write_cstr(&w, "x");
        CHECK(w.err == CSV_ERR_NO_SPACE);
        CHECK(w.len == 0);
        CHECK(w.needed == 1);
        csv_write_row_end(&w);
        CHECK(w.needed == 2); /* keeps counting after the failure */
    }

    /* An empty record, and a sink that under-delivers. */
    {
        csv_writer w;
        char       buf[16];
        csv_writer_init(&w, buf, sizeof buf, NULL);
        csv_write_row(&w, NULL, 0);
        CHECK(w.err == CSV_OK);
        CHECK(w.len == 1);
    }
    {
        csv_writer w;
        char       buf[4];
        sinkbuf    s;
        char       tiny_out[8];
        s.p = tiny_out; s.len = 0; s.cap = sizeof tiny_out;
        csv_writer_init(&w, buf, sizeof buf, NULL);
        csv_writer_sink(&w, sink_append, &s);
        csv_write_cstr(&w, "0123456789abcdef"); /* overruns the sink */
        CHECK(w.err == CSV_ERR_IO);
    }

    /* Ragged table extremes. */
    {
        csv_table t;
        CHECK(csv_table_parse(&t, ",,,\n\n1\n", 7, NULL) == CSV_OK);
        CHECK(t.nrows == 3);
        CHECK(t.ncols == 4);
        CHECK(csv_table_ncols(&t, 1) == 1);
        CHECK(csv_table_at(&t, 0, 3).len == 0);
        CHECK(csv_table_at(&t, 1, 3).len == 0);
        CHECK(csv_str_eq_cstr(csv_table_at(&t, 2, 0), "1"));
        csv_table_free(&t);
        csv_table_free(&t); /* double free must be harmless */
    }
}

static void test_misc(void)
{
    CHECK(strcmp(csv_strerror(CSV_OK), "ok") == 0);
    CHECK(strcmp(csv_strerror(CSV_ERR_NOMEM), "out of memory") == 0);
    CHECK(strcmp(csv_event_name(CSV_EVENT_ROW), "row") == 0);
    CHECK(csv_str_eq(CSV_LIT("abc"), csv_str_make("abc", 3)));
    CHECK(!csv_str_eq(CSV_LIT("abc"), CSV_LIT("abd")));
    CHECK(csv_str_eq(CSV_LIT(""), csv_str_make(NULL, 0)));
    CHECK(strcmp(CSV_VERSION_STRING, "1.0.0") == 0);
}

int main(void)
{
    printf("csv.h %s test suite\n\n", CSV_VERSION_STRING);
    RUN(test_basics);
    RUN(test_line_endings);
    RUN(test_quoting);
    RUN(test_options);
    RUN(test_lenient_recovery);
    RUN(test_errors);
    RUN(test_scratch_limits);
    RUN(test_row_api);
    RUN(test_writer);
    RUN(test_writer_sink);
    RUN(test_roundtrip);
    RUN(test_table);
    RUN(test_table_load);
    RUN(test_dialects);
    RUN(test_inplace);
    RUN(test_edge_cases);
    RUN(test_misc);
    printf("\n%d checks, %d failures\n", g_checks, g_fails);
    return g_fails != 0;
}
