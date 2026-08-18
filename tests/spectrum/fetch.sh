#!/bin/sh
# Re-download the csv-spectrum corpus from upstream and diff it against the
# vendored copies, so you can confirm nothing here was transcribed by hand.
#
#   make spectrum-fetch
#
# Upstream: https://github.com/max-mapper/csv-spectrum (MIT/BSD, csvkit-derived)
set -e

BASE=https://raw.githubusercontent.com/max-mapper/csv-spectrum/master/csvs
DIR=$(dirname "$0")
TMP=$(mktemp -d)
FILES="comma_in_quotes empty empty_crlf escaped_quotes json newlines \
       newlines_crlf quotes_and_newlines simple simple_crlf utf8"
status=0

for f in $FILES; do
    if ! curl -fsSL "$BASE/$f.csv" -o "$TMP/$f.csv"; then
        echo "  ?? $f.csv  (download failed)"
        status=1
        continue
    fi
    if cmp -s "$TMP/$f.csv" "$DIR/$f.csv"; then
        echo "  ok $f.csv"
    else
        echo "  !! $f.csv differs from upstream:"
        diff "$DIR/$f.csv" "$TMP/$f.csv" || true
        status=1
    fi
done

rm -rf "$TMP"
exit $status
