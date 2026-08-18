#!/bin/sh
# Footprint numbers quoted in the README, measured rather than guessed.
#
#   sh tools/measure.sh            (or: make measure)
#
# Reports object sizes, per-instance state, worst-case stack frame and whether
# the implementation carries any mutable global state.
set -e

CC=${CC:-cc}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/impl.c" <<'EOF'
/* the library and nothing else, so any symbol found below is ours */
#define CSV_IMPLEMENTATION
#include "csv.h"
EOF

cat > "$TMP/sizes.c" <<'EOF'
#define CSV_IMPLEMENTATION
#include "csv.h"
#include <stdio.h>
int main(void)
{
    printf("  csv_str    %3zu B\n", sizeof(csv_str));
    printf("  csv_opts   %3zu B\n", sizeof(csv_opts));
    printf("  csv_reader %3zu B   (256 of it is the byte-class table)\n",
           sizeof(csv_reader));
    printf("  csv_writer %3zu B\n", sizeof(csv_writer));
#ifndef CSV_NO_ALLOC
    printf("  csv_table  %3zu B\n", sizeof(csv_table));
#endif
    return 0;
}
EOF

echo "per-instance state (no other allocation happens in the core):"
$CC -std=c99 -I"$ROOT" -O2 -o "$TMP/sizes" "$TMP/sizes.c"
"$TMP/sizes"

echo
echo "object size, -Os:"
$CC -std=c99 -I"$ROOT" -Os -DCSV_NO_ALLOC -DCSV_NO_STDIO -c \
    -o "$TMP/core.o" "$TMP/impl.c"
$CC -std=c99 -I"$ROOT" -Os -c -o "$TMP/full.o" "$TMP/impl.c"
printf "  reader+writer only   .text %6s B\n" \
    "$(size -A "$TMP/core.o" | awk '$1==".text"{print $2}')"
printf "  everything           .text %6s B\n" \
    "$(size -A "$TMP/full.o" | awk '$1==".text"{print $2}')"

echo
echo "mutable global state:"
if nm "$TMP/full.o" | grep -E ' [bBdD] ' >/dev/null 2>&1; then
    nm "$TMP/full.o" | grep -E ' [bBdD] ' | sed 's/^/  /'
    echo "  (tab.N are the const error/event name tables: pointer arrays that"
    echo "   need relocation, so they land in .data.rel.ro, not .data)"
else
    echo "  none"
fi

echo
echo "worst-case stack frame (gcc -fstack-usage; 'static' = no VLA, no alloca):"
( cd "$TMP" && $CC -std=c99 -I"$ROOT" -O2 -fstack-usage -DCSV_NO_ALLOC \
    -DCSV_NO_STDIO -c -o su.o "$TMP/impl.c" ) || true
if [ -f "$TMP/su.su" ]; then
    grep -E "csv\.h|impl\.c" "$TMP/su.su" | sort -t"	" -k2 -nr | head -5 |
        awk -F"	" '{ split($1,a,":"); printf "  %-26s %5s B  %s\n", a[4], $2, $3 }'
else
    echo "  (compiler does not support -fstack-usage)"
fi
