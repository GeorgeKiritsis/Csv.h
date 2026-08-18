/* csv-spectrum conformance: the community's acid-test corpus for CSV parsers.
 *
 * The .csv files under tests/spectrum/ are the upstream ones from
 * max-mapper/csv-spectrum; `make spectrum-fetch` re-downloads them so you can
 * confirm the vendored copies byte for byte. The expectations below are the
 * upstream json/ files transcribed as (header + records).
 *
 * Every document is checked three ways: whole-buffer, in-place, and streamed
 * one byte at a time.
 *
 * Build: make spectrum
 */

#define CSV_IMPLEMENTATION
#include "../csv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fails, g_checks;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        g_checks++;                                                            \
        if (!(cond)) { g_fails++; printf("    FAIL: "); printf(__VA_ARGS__);    \
                       printf("\n"); }                                         \
    } while (0)

#define END_ROW ((const char *)1) /* row separator inside the flat expectation */

typedef struct {
    const char        *file;
    const char *const *want; /* fields, END_ROW between records, NULL at end */
} spec;

static const char *const w_comma_in_quotes[] = {
    "first", "last", "address", "city", "zip", END_ROW,
    "John", "Doe", "120 any st.", "Anytown, WW", "08123", END_ROW, NULL
};
static const char *const w_empty[] = {
    "a", "b", "c", END_ROW, "1", "", "", END_ROW, "2", "3", "4", END_ROW, NULL
};
static const char *const w_escaped_quotes[] = {
    "a", "b", END_ROW, "1", "ha \"ha\" ha", END_ROW, "3", "4", END_ROW, NULL
};
static const char *const w_json[] = {
    "key", "val", END_ROW,
    "1", "{\"type\": \"Point\", \"coordinates\": [102.0, 0.5]}", END_ROW, NULL
};
static const char *const w_newlines[] = {
    "a", "b", "c", END_ROW,
    "1", "2", "3", END_ROW,
    "Once upon \na time", "5", "6", END_ROW,
    "7", "8", "9", END_ROW, NULL
};
static const char *const w_newlines_crlf[] = {
    "a", "b", "c", END_ROW,
    "1", "2", "3", END_ROW,
    "Once upon \r\na time", "5", "6", END_ROW,
    "7", "8", "9", END_ROW, NULL
};
static const char *const w_quotes_and_newlines[] = {
    "a", "b", END_ROW, "1", "ha \n\"ha\" \nha", END_ROW, "3", "4", END_ROW, NULL
};
static const char *const w_simple[] = {
    "a", "b", "c", END_ROW, "1", "2", "3", END_ROW, NULL
};
static const char *const w_utf8[] = {
    "a", "b", "c", END_ROW, "1", "2", "3", END_ROW,
    "4", "5", "\xca\xa4", END_ROW, NULL
};

static const spec g_spec[] = {
    { "comma_in_quotes.csv",     w_comma_in_quotes },
    { "empty.csv",               w_empty },
    { "empty_crlf.csv",          w_empty },
    { "escaped_quotes.csv",      w_escaped_quotes },
    { "json.csv",                w_json },
    { "newlines.csv",            w_newlines },
    { "newlines_crlf.csv",       w_newlines_crlf },
    { "quotes_and_newlines.csv", w_quotes_and_newlines },
    { "simple.csv",              w_simple },
    { "simple_crlf.csv",         w_simple },
    { "utf8.csv",                w_utf8 }
};

/* Flatten a parse into the same shape as the expectation table. */
static void compare(const char *what, const spec *s, csv_str *got, size_t n,
                    const size_t *widths, size_t nrows)
{
    size_t gi = 0, wi = 0, row;
    (void)n;
    for (row = 0; row < nrows; row++) {
        size_t col;
        for (col = 0; col < widths[row]; col++, gi++, wi++) {
            if (s->want[wi] == NULL || s->want[wi] == END_ROW) {
                CHECK(0, "%s/%s: extra field at row %zu col %zu",
                      what, s->file, row, col);
                return;
            }
            CHECK(csv_str_eq_cstr(got[gi], s->want[wi]),
                  "%s/%s: row %zu col %zu: got <" CSV_FMT "> want <%s>",
                  what, s->file, row, col, CSV_ARG(got[gi]), s->want[wi]);
        }
        CHECK(s->want[wi] == END_ROW, "%s/%s: row %zu is the wrong width",
              what, s->file, row);
        if (s->want[wi] != END_ROW) return;
        wi++;
    }
    CHECK(s->want[wi] == NULL, "%s/%s: missing rows", what, s->file);
}

/* Collect a whole document, however it is being fed. */
typedef struct {
    csv_str cells[64];
    size_t  ncells;
    size_t  widths[16];
    size_t  nrows;
    char    keep[64][128]; /* copies, so scratch reuse cannot fool us */
} doc;

static void doc_add(doc *d, const csv_str *row, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        size_t len = row[i].len < sizeof d->keep[0] ? row[i].len
                                                    : sizeof d->keep[0];
        memcpy(d->keep[d->ncells], row[i].ptr, len);
        d->cells[d->ncells] = csv_str_make(d->keep[d->ncells], len);
        d->ncells++;
    }
    d->widths[d->nrows++] = n;
}

int main(void)
{
    size_t i;
    printf("csv-spectrum conformance\n\n");

    for (i = 0; i < CSV_ARRAY_LEN(g_spec); i++) {
        char   path[256];
        char  *buf;
        long   len;
        FILE  *fp;
        doc    d;

        sprintf(path, "tests/spectrum/%s", g_spec[i].file);
        fp = fopen(path, "rb");
        if (!fp) { printf("    FAIL: cannot open %s\n", path); g_fails++; continue; }
        fseek(fp, 0, SEEK_END);
        len = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        buf = (char *)malloc((size_t)len + 1);
        if (fread(buf, 1, (size_t)len, fp) != (size_t)len) { g_fails++; }
        fclose(fp);

        printf("  %-24s", g_spec[i].file);
        {
            int before = g_fails;

            /* 1. whole buffer, scratch mode */
            {
                char       scratch[4096];
                csv_reader rd;
                csv_str    row[16];
                size_t     n;
                memset(&d, 0, sizeof d);
                csv_reader_init_buf(&rd, NULL, scratch, sizeof scratch, buf,
                                    (size_t)len);
                while (csv_next_row(&rd, row, 16, &n) == CSV_EVENT_ROW)
                    doc_add(&d, row, n);
                CHECK(rd.err == CSV_OK, "%s: %s", g_spec[i].file,
                      csv_strerror(rd.err));
                compare("buffer", &g_spec[i], d.cells, d.ncells, d.widths,
                        d.nrows);
            }

            /* 2. in-place mode, no scratch at all */
            {
                char      *mut = (char *)malloc((size_t)len + 1);
                csv_reader rd;
                csv_str    row[16];
                size_t     n;
                memcpy(mut, buf, (size_t)len);
                memset(&d, 0, sizeof d);
                csv_reader_init_mut(&rd, NULL, mut, (size_t)len);
                while (csv_next_row(&rd, row, 16, &n) == CSV_EVENT_ROW)
                    doc_add(&d, row, n);
                CHECK(rd.err == CSV_OK, "%s in-place: %s", g_spec[i].file,
                      csv_strerror(rd.err));
                compare("in-place", &g_spec[i], d.cells, d.ncells, d.widths,
                        d.nrows);
                free(mut);
            }

            /* 3. streamed one byte at a time through a sliding window */
            {
                char       scratch[4096], window[4096];
                csv_reader rd;
                size_t     wlen = 0, fed = 0;
                memset(&d, 0, sizeof d);
                csv_reader_init(&rd, NULL, scratch, sizeof scratch);
                for (;;) {
                    int final;
                    if (fed < (size_t)len) window[wlen++] = buf[fed++];
                    final = (fed == (size_t)len);
                    csv_reader_feed(&rd, window, wlen, final);
                    {
                        csv_str   row[16];
                        size_t    n, consumed;
                        csv_event e;
                        int       stop = 0;
                        for (;;) {
                            e = csv_next_row(&rd, row, 16, &n);
                            if (e == CSV_EVENT_ROW) { doc_add(&d, row, n); continue; }
                            if (e == CSV_EVENT_NEED_MORE) {
                                consumed = csv_consumed(&rd);
                                memmove(window, window + consumed, wlen - consumed);
                                wlen -= consumed;
                                break;
                            }
                            stop = 1;
                            break;
                        }
                        if (stop) break;
                    }
                }
                CHECK(rd.err == CSV_OK, "%s streamed: %s", g_spec[i].file,
                      csv_strerror(rd.err));
                compare("streamed", &g_spec[i], d.cells, d.ncells, d.widths,
                        d.nrows);
            }
            printf("%s\n", g_fails == before ? "ok" : "FAILED");
        }
        free(buf);
    }

    printf("\n%d checks, %d failures\n", g_checks, g_fails);
    return g_fails != 0;
}
