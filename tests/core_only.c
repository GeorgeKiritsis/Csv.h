/* Compiles the header in restricted configurations: no malloc, no stdio, as
 * C++, and as a single static TU. Exercises every public entry point so the
 * compiler actually type-checks them. Also compiled as C++ by `make cxx`. */

#define CSV_IMPLEMENTATION
#include "../csv.h"

#ifdef __cplusplus
extern "C" {
#endif
int csv_smoke(void);
#ifdef __cplusplus
}
#endif

int csv_smoke(void)
{
    static char scratch[256];
    static char out[256];
    csv_reader  rd;
    csv_writer  w;
    csv_str     row[8];
    size_t      n = 0;
    int         fields = 0;

    csv_reader_init_buf(&rd, NULL, scratch, sizeof scratch, "a,\"b\"\"c\"\n", 9);
    while (csv_next_row(&rd, row, 8, &n) == CSV_EVENT_ROW)
        fields += (int)n;

    if (rd.err != CSV_OK) return -1;
    if (csv_find(row, n, "zzz") != CSV_NOT_FOUND) return -2;
    if (csv_consumed(&rd) == 0) return -3;

    csv_writer_init(&w, out, sizeof out, NULL);
    csv_write_cstr(&w, "x");
    csv_write_str(&w, CSV_LIT("y,z"));
    csv_write_row(&w, row, n);
    csv_write_row_end(&w);
    if (csv_writer_flush(&w) != CSV_OK) return -4;

    return fields + (int)w.needed + (int)csv_str_eq(CSV_LIT("a"), CSV_LIT("a")) +
           (int)csv_strerror(CSV_OK)[0] + (int)csv_event_name(CSV_EVENT_END)[0] +
           (int)csv_opts_default().delimiter;
}

#if !defined(CSV_NO_ALLOC) && !defined(__cplusplus)
/* Nothing else to do; the allocating layer is covered by the main suite. */
#endif
