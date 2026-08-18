# csv.h - build, test, sanitize, fuzz. No configuration, no dependencies.

CC      ?= cc
CXX     ?= c++
WARN     = -Wall -Wextra -Wpedantic -Wshadow -Wcast-qual -Wstrict-prototypes \
           -Wmissing-prototypes -Wpointer-arith -Wwrite-strings -Wconversion \
           -Wsign-conversion -Wvla
# The test tables lean on C99's "unlisted members are zeroed" rule, which is
# the one warning in the set above that fires on the tests but not the header.
TWARN    = $(WARN) -Wno-missing-field-initializers
CFLAGS  ?= -O2
B        = build

.PHONY: all test spectrum spectrum-fetch check asan strict cxx multi fuzz \
        fuzz-run examples demo bench bench-vs measure diff clean

all: test

$(B):
	@mkdir -p $(B)

# ---------------------------------------------------------------- testing
test: $(B)
	$(CC) -std=c99 $(TWARN) $(CFLAGS) -o $(B)/test_csv tests/test_csv.c
	@$(B)/test_csv

# The community acid-test corpus, checked in buffer, in-place and streamed modes.
spectrum: $(B)
	$(CC) -std=c99 $(TWARN) $(CFLAGS) -o $(B)/test_spectrum tests/test_spectrum.c
	@$(B)/test_spectrum

# Re-download the corpus from upstream to verify the vendored copies.
spectrum-fetch:
	@sh tests/spectrum/fetch.sh

asan: $(B)
	$(CC) -std=c99 $(TWARN) -O1 -g -fsanitize=address,undefined \
	      -fno-omit-frame-pointer -o $(B)/test_asan tests/test_csv.c
	@$(B)/test_asan
	$(CC) -std=c99 $(TWARN) -O1 -g -fsanitize=address,undefined \
	      -fno-omit-frame-pointer -o $(B)/spectrum_asan tests/test_spectrum.c
	@$(B)/spectrum_asan

# The header must be warning-clean as C99/C11/C17 and as C++.
strict: $(B)
	@for std in c99 c11 c17; do \
	   echo "  cc -std=$$std"; \
	   $(CC) -std=$$std $(TWARN) -Werror -O2 -o $(B)/std_$$std tests/test_csv.c || exit 1; \
	 done
	@echo "  cc -DCSV_NO_ALLOC -DCSV_NO_STDIO (freestanding-ish core)"
	@$(CC) -std=c99 $(WARN) -Werror -O2 -DCSV_NO_ALLOC -DCSV_NO_STDIO \
	       -c -o $(B)/core_only.o tests/core_only.c
	@echo "  cc -DCSV_STATIC"
	@$(CC) -std=c99 $(WARN) -Werror -O2 -DCSV_STATIC \
	       -c -o $(B)/static.o tests/core_only.c

cxx: $(B)
	$(CXX) -std=c++11 -Wall -Wextra -O2 -x c++ -c -o $(B)/cxx11.o tests/core_only.c
	$(CXX) -std=c++17 -Wall -Wextra -O2 -x c++ -c -o $(B)/cxx17.o tests/core_only.c

# Two translation units including the header, one defining the implementation.
multi: $(B)
	$(CC) -std=c99 $(WARN) -Werror -O2 -o $(B)/multi tests/multi_a.c tests/multi_b.c
	@$(B)/multi

# Differential test against Python's csv module on random hostile documents.
diff: $(B)
	$(CC) -std=c99 -Wall -Wextra -O2 -o $(B)/dumpjson tests/dumpjson.c
	@python3 tests/diff_python.py 2000

check: test spectrum asan strict cxx multi examples diff
	@echo "all checks passed"

# ---------------------------------------------------------------- fuzzing
fuzz: $(B)
	clang -std=c99 -g -O1 -fsanitize=fuzzer,address,undefined \
	      -o $(B)/fuzz_csv fuzz/fuzz_csv.c

fuzz-run: fuzz
	@mkdir -p corpus
	$(B)/fuzz_csv corpus -max_total_time=$(or $(SECONDS_),60) -print_final_stats=1

# --------------------------------------------------------------- examples
examples: $(B)
	$(CC) -std=c99 $(WARN) $(CFLAGS) -o $(B)/demo      examples/demo.c
	$(CC) -std=c99 $(WARN) $(CFLAGS) -o $(B)/csvcat    examples/csvcat.c
	$(CC) -std=c99 $(WARN) $(CFLAGS) -o $(B)/csvselect examples/csvselect.c

# The guided tour: reading, writing, errors and dialects in one screen.
demo: $(B)
	$(CC) -std=c99 $(WARN) $(CFLAGS) -o $(B)/demo examples/demo.c
	@$(B)/demo

# Struct sizes, .text size, worst-case stack frame: the numbers in the README.
measure:
	@sh tools/measure.sh

bench: $(B)
	$(CC) -std=c99 -Wall -Wextra -O2 -o $(B)/bench tests/bench.c
	@$(B)/bench

# Head-to-head against libcsv. Needs libcsv-dev installed. The two libraries
# each ship a header called csv.h and both export csv_strerror(), so they are
# linked from separate translation units.
bench-vs: $(B)
	$(CC) -std=c99 -Wall -Wextra -O2 -o $(B)/bench_vs tests/bench_vs/main.c \
	      tests/bench_vs/impl_csvh.c tests/bench_vs/impl_libcsv.c -lcsv
	@$(B)/bench_vs

clean:
	rm -rf $(B)
