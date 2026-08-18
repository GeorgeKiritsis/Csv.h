/* Reads CSV on stdin, prints the whole document as JSON (a list of lists).
 * Used by tests/diff_python.py to diff csv.h against Python's csv module. */

#define CSV_IMPLEMENTATION
#include "../csv.h"

#include <stdio.h>

static void json(csv_str s)
{
    size_t i;
    putchar('"');
    for (i = 0; i < s.len; i++) {
        unsigned char c = (unsigned char)s.ptr[i];
        switch (c) {
        case '"':  fputs("\\\"", stdout); break;
        case '\\': fputs("\\\\", stdout); break;
        case '\n': fputs("\\n", stdout);  break;
        case '\r': fputs("\\r", stdout);  break;
        case '\t': fputs("\\t", stdout);  break;
        default:
            if (c < 0x20 || c > 0x7e) printf("\\u%04x", c);
            else putchar((int)c);
        }
    }
    putchar('"');
}

int main(int argc, char **argv)
{
    static char    buf[1 << 20], scratch[1 << 20];
    static csv_str row[4096];
    csv_opts       o = csv_opts_default();
    csv_reader     rd;
    size_t         len, n, i;
    int            first = 1;

    if (argc > 1 && argv[1][0]) o.delimiter = argv[1][0];
    o.flags = CSV_FLAG_NO_BOM; /* compare raw bytes, like Python does */

    len = fread(buf, 1, sizeof buf, stdin);
    csv_reader_init_buf(&rd, &o, scratch, sizeof scratch, buf, len);

    putchar('[');
    while (csv_next_row(&rd, row, 4096, &n) == CSV_EVENT_ROW) {
        if (!first) putchar(',');
        putchar('[');
        for (i = 0; i < n; i++) {
            if (i) putchar(',');
            json(row[i]);
        }
        putchar(']');
        first = 0;
    }
    if (rd.err != CSV_OK) {
        fprintf(stderr, "%s\n", csv_strerror(rd.err));
        return 1;
    }
    printf("]\n");
    return 0;
}
