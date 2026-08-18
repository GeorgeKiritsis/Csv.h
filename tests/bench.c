/* Throughput check. Generates a document in memory, parses it, prints MB/s.
 * Build: make bench */

#define CSV_IMPLEMENTATION
#include "../csv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now(void) /* CPU time; plain C89, no POSIX feature macros */
{
    return (double)clock() / (double)CLOCKS_PER_SEC;
}

static size_t gen(char *p, size_t cap, int quoted)
{
    static const char *plain[] = { "12345", "hello", "a", "some longer value",
                                   "42", "", "xyz" };
    static const char *quote[] = { "\"a,b\"", "\"say \"\"hi\"\"\"",
                                   "\"multi\nline\"", "plain" };
    size_t len = 0;
    int    col = 0;
    while (len < cap - 64) {
        const char *v = quoted ? quote[(len + (size_t)col) % 4]
                               : plain[(len + (size_t)col) % 7];
        size_t n = strlen(v);
        memcpy(p + len, v, n);
        len += n;
        p[len++] = (++col % 8) ? ',' : '\n';
    }
    p[len - 1] = '\n';
    return len;
}

static double run(const char *label, char *doc, size_t len, char *scratch,
                  size_t scap)
{
    csv_reader rd;
    csv_opts   o = csv_opts_default();
    double     t0, dt;
    size_t     nf = 0;
    csv_event  e;
    int        rep, reps = 5;

    t0 = now();
    for (rep = 0; rep < reps; rep++) {
        csv_reader_init_buf(&rd, &o, scratch, scap, doc, len);
        while ((e = csv_next(&rd)) != CSV_EVENT_END) {
            if (e == CSV_EVENT_ERROR) { printf("parse error!\n"); return 0; }
            nf += (e == CSV_EVENT_FIELD);
        }
    }
    dt = now() - t0;
    printf("%-22s %6.1f MB/s   (%zu fields, %zu rows)\n", label,
           (double)len * reps / dt / 1e6, nf / (size_t)reps, rd.row);
    return dt;
}

int main(void)
{
    size_t cap = 64u << 20;
    char  *doc = (char *)malloc(cap);
    char  *scratch = (char *)malloc(1 << 20);
    size_t len;

    if (!doc || !scratch) return 1;

    len = gen(doc, cap, 0);
    run("unquoted", doc, len, scratch, 1 << 20);
    len = gen(doc, cap, 1);
    run("quoted + escapes", doc, len, scratch, 1 << 20);

    free(doc);
    free(scratch);
    return 0;
}
