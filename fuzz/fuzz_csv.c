/*
 * libFuzzer entry point for csv.h.
 *
 * Three properties are asserted on every input:
 *   1. the parser never crashes, reads out of bounds or loops forever;
 *   2. a document parses identically however it is sliced into chunks,
 *      line numbers included -- rd.line is part of the observable output,
 *      and comparing only field bytes once let a counting bug through;
 *   3. parse -> write -> parse is the identity on the field values.
 *
 * Build:  make fuzz && ./build/fuzz_csv corpus -max_total_time=60
 */

#define CSV_IMPLEMENTATION
#include "../csv.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_DOC   (1u << 20)
#define SER_CAP   (4u << 20)
#define WINDOW    (1u << 16)

static char g_scratch[MAX_DOC + 16];
static char g_window[WINDOW];
static char g_a[SER_CAP], g_b[SER_CAP];
static char g_out[SER_CAP];

/* Append a length-prefixed field so no field content can forge a separator. */
static int ser(char **p, const char *end, csv_str s, char tag)
{
    if ((size_t)(end - *p) < s.len + 32) return 0;
    *p += sprintf(*p, "%c%zu:", tag, s.len);
    memcpy(*p, s.ptr, s.len);
    *p += s.len;
    return 1;
}

/* Row terminator for the serialised form. It carries rd.line so that the
 * chunking-equivalence property covers the line counter and not just the
 * field bytes. The round-trip property passes lines = 0: rewriting a
 * document legitimately changes its line structure (an unterminated final
 * record gains a terminator), so only the field values are comparable. */
static int ser_rowend(char **p, const char *end, const csv_reader *rd,
                      int lines)
{
    if ((size_t)(end - *p) < 32) return 0;
    if (lines) *p += sprintf(*p, "@%zu", rd->line);
    *(*p)++ = ';';
    return 1;
}

/* Both parsers below work at row granularity. That matters: a row that is cut
 * short by an error contributes nothing, so the two sides stay comparable. */
static csv_error parse_oneshot(const char *d, size_t n, const csv_opts *o,
                               char *buf, size_t cap, int lines)
{
    csv_reader rd;
    char      *p = buf, *end = buf + cap;

    csv_reader_init_buf(&rd, o, g_scratch, sizeof g_scratch, d, n);
    for (;;) {
        csv_str row[512];
        size_t  i, k;
        if (csv_next_row(&rd, row, CSV_ARRAY_LEN(row), &k) != CSV_EVENT_ROW) break;
        for (i = 0; i < k; i++)
            if (!ser(&p, end, row[i], 'f')) { *p = 0; return rd.err; }
        if (!ser_rowend(&p, end, &rd, lines)) break;
    }
    *p = 0;
    return rd.err;
}

static csv_error parse_streamed(const char *d, size_t n, const csv_opts *o,
                                size_t chunk, char *buf, size_t cap)
{
    csv_reader rd;
    char      *p = buf, *end = buf + cap;
    size_t     wlen = 0, fed = 0;

    if (chunk == 0) chunk = 1;
    csv_reader_init(&rd, o, g_scratch, sizeof g_scratch);
    for (;;) {
        size_t take = n - fed < chunk ? n - fed : chunk;
        int    final;
        if (take > sizeof g_window - wlen) take = sizeof g_window - wlen;
        memcpy(g_window + wlen, d + fed, take);
        wlen += take;
        fed  += take;
        final = (fed == n);
        csv_reader_feed(&rd, g_window, wlen, final);

        for (;;) {
            csv_str   row[512];
            size_t    i, k, consumed;
            csv_event e = csv_next_row(&rd, row, CSV_ARRAY_LEN(row), &k);
            if (e == CSV_EVENT_ROW) {
                for (i = 0; i < k; i++)
                    if (!ser(&p, end, row[i], 'f')) { *p = 0; return rd.err; }
                if (!ser_rowend(&p, end, &rd, 1)) { *p = 0; return rd.err; }
                continue;
            }
            if (e == CSV_EVENT_NEED_MORE) {
                consumed = csv_consumed(&rd);
                memmove(g_window, g_window + consumed, wlen - consumed);
                wlen -= consumed;
                if (wlen == sizeof g_window) { *p = 0; return CSV_ERR_NO_SPACE; }
                break;
            }
            *p = 0;
            return rd.err;
        }
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    csv_opts  o = csv_opts_default();
    unsigned  cfg;
    csv_error ea, eb;

    if (size < 2 || size > MAX_DOC) return 0;
    cfg   = data[0];
    data += 1;
    size -= 1;

    o.flags = cfg & (CSV_FLAG_STRICT | CSV_FLAG_TRIM | CSV_FLAG_SKIP_EMPTY |
                     CSV_FLAG_NO_BOM | CSV_FLAG_KEEP_SCRATCH);
    if (cfg & 0x20) o.delimiter = ';';
    if (cfg & 0x40) o.comment   = '#';

    /* (1)+(2): chunked parsing must agree with one-shot parsing. */
    ea = parse_oneshot((const char *)data, size, &o, g_a, SER_CAP - 1, 1);
    eb = parse_streamed((const char *)data, size, &o, 1 + (cfg >> 5), g_b,
                        SER_CAP - 1);
    if (ea != eb || strcmp(g_a, g_b) != 0) {
        fprintf(stderr, "chunking mismatch: %s vs %s\n", csv_strerror(ea),
                csv_strerror(eb));
        abort();
    }

    /* (3): round-trip through the writer, with plain options so that neither
     * comments nor blank-line skipping can legitimately eat a row. */
    {
        csv_opts   po = csv_opts_default();
        csv_writer w;
        csv_reader rd;
        csv_event  e;

        /* BOM stripping is deliberately lossy: a field whose first bytes are
         * EF BB BF loses them if it is written back out at offset 0. Turn it
         * off so the round-trip property is actually well defined. */
        po.delimiter = o.delimiter;
        po.flags     = CSV_FLAG_NO_BOM;
        if (parse_oneshot((const char *)data, size, &po, g_a, SER_CAP - 1, 0)
            != CSV_OK)
            return 0;

        csv_writer_init(&w, g_out, sizeof g_out, &po);
        csv_reader_init_buf(&rd, &po, g_scratch, sizeof g_scratch,
                            (const char *)data, size);
        while ((e = csv_next(&rd)) != CSV_EVENT_END) {
            if (e == CSV_EVENT_ERROR) return 0;
            if (e == CSV_EVENT_FIELD)      csv_write_str(&w, rd.field);
            else if (e == CSV_EVENT_ROW)   csv_write_row_end(&w);
        }
        if (w.err != CSV_OK) return 0; /* output buffer exhausted, not a bug */

        if (parse_oneshot(g_out, w.len, &po, g_b, SER_CAP - 1, 0) != CSV_OK ||
            strcmp(g_a, g_b) != 0) {
            fprintf(stderr, "round-trip mismatch\n");
            abort();
        }
    }
    return 0;
}
