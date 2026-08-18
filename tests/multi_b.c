/* TU #2: header only, no implementation. Must link against TU #1. */
#include "../csv.h"

int other_tu(void);

int other_tu(void)
{
    char       scratch[32];
    csv_reader rd;
    csv_str    row[8];
    size_t     n = 0;

    csv_reader_init_buf(&rd, NULL, scratch, sizeof scratch, "1,2,3\n", 6);
    csv_next_row(&rd, row, 8, &n);
    return (int)n;
}
