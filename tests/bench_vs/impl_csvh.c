/* csv.h side of the head-to-head benchmark. */

#define CSV_IMPLEMENTATION
#include "../../csv.h"

#include <stdlib.h>

size_t bench_csvh(const char *doc, size_t len, unsigned long *sum);

/* Uses the row API, which is what the README recommends and what a real
 * consumer would write. */
size_t bench_csvh(const char *doc, size_t len, unsigned long *sum)
{
    static char   scratch[1 << 20];
    csv_reader    rd;
    csv_str       row[64];
    size_t        n, fields = 0;
    unsigned long s = 0;

    csv_reader_init_buf(&rd, NULL, scratch, sizeof scratch, doc, len);
    while (csv_next_row(&rd, row, 64, &n) == CSV_EVENT_ROW) {
        size_t f;
        for (f = 0; f < n; f++) {
            size_t i;
            for (i = 0; i < row[f].len; i++)
                s += (unsigned char)row[f].ptr[i];
        }
        fields += n;
    }
    if (rd.err != CSV_OK) abort();
    *sum = s;
    return fields;
}
