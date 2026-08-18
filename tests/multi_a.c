/* TU #1: defines the implementation. */
#define CSV_IMPLEMENTATION
#include "../csv.h"

#include <stdio.h>

int other_tu(void); /* from multi_b.c */

int main(void)
{
    char       scratch[64];
    csv_reader rd;
    csv_str    row[4];
    size_t     n;

    csv_reader_init_buf(&rd, NULL, scratch, sizeof scratch, "a,b\n", 4);
    if (csv_next_row(&rd, row, 4, &n) != CSV_EVENT_ROW || n != 2) {
        printf("multi-TU: parse failed\n");
        return 1;
    }
    if (other_tu() != 3) {
        printf("multi-TU: second unit failed\n");
        return 1;
    }
    printf("multi-TU link ok\n");
    return 0;
}
