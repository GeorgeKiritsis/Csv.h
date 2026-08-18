/*
 * csvselect - project columns by name, using the allocating table layer.
 *
 *   csvselect file.csv name qty        # keeps the header, drops other columns
 */

#define CSV_IMPLEMENTATION
#include "../csv.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    csv_table  t;
    csv_writer w;
    char       buf[8192];
    csv_error  err;
    int        i;
    size_t     r;

    if (argc < 3) {
        fprintf(stderr, "usage: %s file.csv column [column...]\n", argv[0]);
        return 2;
    }

    if ((err = csv_table_load(&t, argv[1], NULL)) != CSV_OK) {
        fprintf(stderr, "%s: %s\n", argv[1], csv_strerror(err));
        return 1;
    }

    for (i = 2; i < argc; i++) {
        if (csv_table_col(&t, argv[i]) == CSV_NOT_FOUND) {
            fprintf(stderr, "no such column: %s\n", argv[i]);
            csv_table_free(&t);
            return 1;
        }
    }

    csv_writer_init(&w, buf, sizeof buf, NULL);
    csv_writer_sink(&w, csv_sink_file, stdout);

    for (r = 0; r < t.nrows; r++) {
        for (i = 2; i < argc; i++)
            csv_write_str(&w, csv_table_get(&t, r, argv[i]));
        csv_write_row_end(&w);
    }
    csv_writer_flush(&w);

    csv_table_free(&t);
    return w.err != CSV_OK;
}
