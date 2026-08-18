/* libcsv side of the head-to-head benchmark. Note the include is <csv.h>:
 * libcsv installs a header with exactly the same name as ours, which is the
 * whole argument for namespacing the file if you vendor it. */

#include <csv.h>

#include <stddef.h>
#include <stdlib.h>

size_t bench_libcsv(const char *doc, size_t len, unsigned long *sum);

typedef struct { size_t fields; unsigned long sum; } ctx;

static void on_field(void *data, size_t n, void *user)
{
    ctx                 *c = (ctx *)user;
    const unsigned char *p = (const unsigned char *)data;
    size_t               i;
    for (i = 0; i < n; i++) c->sum += p[i];
    c->fields++;
}

static void on_row(int c, void *user) { (void)c; (void)user; }

size_t bench_libcsv(const char *doc, size_t len, unsigned long *sum)
{
    struct csv_parser p;
    ctx               c;

    c.fields = 0;
    c.sum    = 0;

    if (csv_init(&p, 0) != 0) abort();
    if (csv_parse(&p, doc, len, on_field, on_row, &c) != len) abort();
    csv_fini(&p, on_field, on_row, &c);
    csv_free(&p);

    *sum = c.sum;
    return c.fields;
}
