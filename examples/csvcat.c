/*
 * csvcat - stream a CSV file of any size through a sliding window and print
 * every field. The parser itself allocates nothing; the window grows only if
 * a single record needs more room than it has, so there is no maximum record
 * size other than the memory you are willing to give it.
 *
 *   csvcat file.csv          csvcat -d';' file.csv          cat x | csvcat
 */

#define CSV_IMPLEMENTATION
#include "../csv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW0  (64 * 1024)
#define MAX_COLS 512

static char scratch[64 * 1024];

int main(int argc, char **argv)
{
    csv_opts   opts = csv_opts_default();
    csv_reader rd;
    FILE      *fp = stdin;
    char      *window = NULL;
    size_t     wcap = WINDOW0, wlen = 0;
    int        i, eof = 0, rc = 0;

    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-d", 2) == 0 && argv[i][2])
            opts.delimiter = argv[i][2];
        else if (strcmp(argv[i], "-t") == 0)
            opts.delimiter = '\t';
        else if ((fp = fopen(argv[i], "rb")) == NULL)
            return fprintf(stderr, "csvcat: cannot open %s\n", argv[i]), 1;
    }

    if ((window = (char *)malloc(wcap)) == NULL) return 1;
    csv_reader_init(&rd, &opts, scratch, sizeof scratch);

    while (!eof) {
        size_t got = fread(window + wlen, 1, wcap - wlen, fp);
        wlen += got;
        eof = (got == 0);
        csv_reader_feed(&rd, window, wlen, eof);

        for (;;) {
            csv_str   row[MAX_COLS];
            size_t    n, k, consumed;
            csv_event e = csv_next_row(&rd, row, MAX_COLS, &n);

            if (e == CSV_EVENT_ROW) {
                printf("%zu:", rd.row);
                for (k = 0; k < n; k++)
                    printf(" [" CSV_FMT "]", CSV_ARG(row[k]));
                putchar('\n');
                continue;
            }
            if (e == CSV_EVENT_NEED_MORE) {
                /* Drop everything the parser is done with, keep the tail. */
                consumed = csv_consumed(&rd);
                memmove(window, window + consumed, wlen - consumed);
                wlen -= consumed;
                if (wlen == wcap) {
                    /* One record fills the whole window: give it more. */
                    char *bigger = (char *)realloc(window, wcap * 2);
                    if (!bigger) {
                        fprintf(stderr, "csvcat: out of memory at line %zu\n",
                                rd.line);
                        rc = 1;
                        goto done;
                    }
                    window = bigger;
                    wcap  *= 2;
                }
                break;
            }
            if (e == CSV_EVENT_ERROR) {
                fprintf(stderr, "csvcat: line %zu, field %zu: %s\n",
                        rd.line, rd.col + 1, csv_strerror(rd.err));
                rc = 1;
            }
            goto done; /* CSV_EVENT_END or CSV_EVENT_ERROR */
        }
    }

done:
    free(window);
    if (fp != stdin) fclose(fp);
    return rc;
}
