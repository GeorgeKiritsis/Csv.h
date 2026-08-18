/* Head-to-head benchmark: csv.h vs libcsv (rgamble/libcsv, LGPL-2.1).
 *
 * The two libraries cannot live in one translation unit -- both export
 * csv_strerror() and a csv_error symbol, and both ship a header called
 * "csv.h" -- so each is wrapped in its own TU behind the tiny interface below.
 *
 *   make bench-vs        (requires libcsv-dev)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Each backend parses the document and folds every field byte into a
 * checksum, so nothing can be optimised away and both are forced to actually
 * deliver the field contents. Returns fields parsed; *sum gets the checksum. */
size_t bench_csvh(const char *doc, size_t len, unsigned long *sum);
size_t bench_libcsv(const char *doc, size_t len, unsigned long *sum);

static double now(void) { return (double)clock() / (double)CLOCKS_PER_SEC; }

/* Three shapes, because CSV parsers are dominated by per-field overhead and
 * the mix of short/long/quoted fields is what decides who wins. */
enum { GEN_SHORT, GEN_QUOTED, GEN_LONG };

static const char *g_shape[] = { "short fields", "quoted+escapes",
                                 "long fields" };

static size_t gen(char *p, size_t cap, int shape)
{
    static const char *plain[] = { "12345", "hello", "a", "some longer value",
                                   "42", "", "xyz" };
    static const char *quote[] = { "\"a,b\"", "\"say \"\"hi\"\"\"",
                                   "\"multi\nline\"", "plain" };
    static char        big[221];
    size_t             len = 0;
    int                col = 0;

    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = 0;

    while (len < cap - 512) {
        const char *v = shape == GEN_QUOTED ? quote[(len + (size_t)col) % 4]
                      : shape == GEN_LONG   ? big
                      : plain[(len + (size_t)col) % 7];
        size_t n = strlen(v);
        memcpy(p + len, v, n);
        len += n;
        p[len++] = (++col % 8) ? ',' : '\n';
    }
    p[len - 1] = '\n';
    return len;
}

static double timeit(size_t (*fn)(const char *, size_t, unsigned long *),
                     const char *doc, size_t len, int reps,
                     size_t *fields, unsigned long *sum)
{
    double t0 = now();
    int    i;
    for (i = 0; i < reps; i++) {
        *sum    = 0;
        *fields = fn(doc, len, sum);
    }
    return now() - t0;
}

int main(void)
{
    const size_t  cap  = 64u << 20;
    const int     reps = 5;
    char         *doc  = (char *)malloc(cap);
    int           w;

    if (!doc) return 1;

    printf("%-18s %14s %14s %10s\n", "workload", "csv.h", "libcsv 3.0.3", "ratio");
    printf("%-18s %14s %14s %10s\n", "--------", "-----", "------------", "-----");

    for (w = 0; w < 3; w++) {
        size_t        len = gen(doc, cap, w);
        size_t        fa, fb;
        unsigned long sa, sb;
        double        ta, tb;
        double        mba, mbb;

        /* warm the page cache / branch predictors equally */
        bench_csvh(doc, len, &sa);
        bench_libcsv(doc, len, &sb);

        ta = timeit(bench_csvh,   doc, len, reps, &fa, &sa);
        tb = timeit(bench_libcsv, doc, len, reps, &fb, &sb);

        mba = (double)len * reps / ta / 1e6;
        mbb = (double)len * reps / tb / 1e6;

        printf("%-18s %9.1f MB/s %9.1f MB/s %9.2fx\n",
               g_shape[w], mba, mbb, mba / mbb);

        if (fa != fb || sa != sb)
            printf("  !! disagreement: csv.h %zu fields sum %lu / "
                   "libcsv %zu fields sum %lu\n", fa, sa, fb, sb);
        else
            printf("  (both: %zu fields, checksum %lu -- identical)\n", fa, sa);
    }

    free(doc);
    return 0;
}
