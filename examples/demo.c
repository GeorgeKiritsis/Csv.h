/*
 * demo - a five-minute tour of csv.h: reading, writing, error handling and
 * dialects, each against a small in-memory document. No files, no arguments.
 *
 *   cc -std=c99 examples/demo.c -o demo && ./demo
 */

#define CSV_IMPLEMENTATION
#include "../csv.h"

#include <stdio.h>

/* Fields are not NUL-terminated, so numbers are parsed from (ptr, len). */
static long to_long(csv_str s)
{
    long   v = 0;
    size_t i;
    for (i = 0; i < s.len; i++) v = v * 10 + (s.ptr[i] - '0');
    return v;
}

/* Reading: in-place mode on a buffer we own. The document has everything the
 * format can throw at you: a UTF-8 BOM, a quoted delimiter, escaped quotes, a
 * field spanning two lines, and a short row. */
static void demo_reading(void)
{
    static char doc[] =
        "\xEF\xBB\xBF" "id,product,note,qty\n"
        "1,\"Widget, large\",\"says \"\"hello\"\"\",12\n"
        "2,Gadget,\"multi\nline note\",7\n"
        "3,Doohickey\n";

    csv_reader rd;
    csv_str    row[8];
    size_t     n, qty, r = 0;
    long       total = 0;

    puts("== reading =====================================================");
    csv_reader_init_mut(&rd, NULL, doc, sizeof doc - 1);

    if (csv_next_row(&rd, row, CSV_ARRAY_LEN(row), &n) != CSV_EVENT_ROW) return;
    qty = csv_find(row, n, "qty");
    printf("  columns: id=%zu product=%zu qty=%zu"
           "  (BOM was eaten: first header is \"" CSV_FMT "\")\n",
           csv_find(row, n, "id"), csv_find(row, n, "product"), qty,
           CSV_ARG(row[0]));

    csv_foreach_row (&rd, row, nf) {
        size_t i;
        printf("  row %zu (%zu fields):", ++r, nf);
        for (i = 0; i < nf; i++) printf(" [" CSV_FMT "]", CSV_ARG(row[i]));
        if (nf > qty) total += to_long(row[qty]);
        else          printf("   <- no qty in this row");
        putchar('\n');
    }
    printf("  total qty: %ld\n\n", total);
}

/* Writing: fields are quoted only when they need to be, quotes are doubled. */
static void demo_writing(void)
{
    char       buf[256];
    csv_writer w;

    puts("== writing =====================================================");
    csv_writer_init(&w, buf, sizeof buf, NULL);
    csv_write_cstr(&w, "id"); csv_write_cstr(&w, "description");
    csv_write_row_end(&w);
    csv_write_cstr(&w, "1");  csv_write_cstr(&w, "needs, a comma");
    csv_write_row_end(&w);
    csv_write_cstr(&w, "2");  csv_write_cstr(&w, "he said \"hi\"");
    csv_write_row_end(&w);

    fwrite(buf, 1, w.len, stdout);
    printf("  (%zu bytes written, err=%s)\n\n", w.len, csv_strerror(w.err));
}

/* Errors: sticky, located, and never fatal to the process. */
static void demo_errors(void)
{
    static char doc[] = "ok,fine\nx,\"oops";

    csv_reader rd;
    csv_str    row[8];
    size_t     n;
    csv_event  e;

    puts("== errors ======================================================");
    csv_reader_init_mut(&rd, NULL, doc, sizeof doc - 1);

    while ((e = csv_next_row(&rd, row, CSV_ARRAY_LEN(row), &n)) == CSV_EVENT_ROW)
        printf("  ok: %zu fields\n", n);

    if (e == CSV_EVENT_ERROR)
        printf("  stopped at line %zu, record %zu, field %zu: %s\n",
               rd.line, rd.row + 1, rd.col + 1, csv_strerror(rd.err));

    printf("  still failed on the next call: %s\n\n",
           csv_event_name(csv_next(&rd)));
}

/* Dialects: every option lives in csv_opts; here, tab-separated values. */
static void demo_dialects(void)
{
    static char doc[] = "name\tcity\nAda\tAthens\n";

    csv_opts   o = csv_opts_default();
    csv_reader rd;
    csv_str    row[8];

    puts("== dialects ====================================================");
    o.delimiter = '\t';
    csv_reader_init_mut(&rd, &o, doc, sizeof doc - 1);

    csv_foreach_row (&rd, row, n) {
        size_t i;
        printf("  tsv:");
        for (i = 0; i < n; i++) printf(" [" CSV_FMT "]", CSV_ARG(row[i]));
        putchar('\n');
    }
}

int main(void)
{
    demo_reading();
    demo_writing();
    demo_errors();
    demo_dialects();
    return 0;
}
