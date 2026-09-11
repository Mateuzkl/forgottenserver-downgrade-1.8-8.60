# perf/profile-baseline — measurement harness

Nothing in this directory is linked into the server. It exists so that the
performance plan's later branches argue from measurements taken on the machine
that will actually run the server, instead of from numbers quoted out of a
document.

## The one rule

The baseline is the flag set the Release build really uses:

```text
-O3 -march=native -mtune=native -fomit-frame-pointer -DNDEBUG
```

A speedup measured against `-O0`, `-O1` or `-O2` is not a result. Compiler flags
change the winner of the XTEA comparison completely, which is how the previous
version of the plan ended up prioritising a port that would have been a
regression.

## Running it

```bash
# baseline flag set only — this is the number that decides anything
./benchmarks/perf_baseline/run.sh

# the same benchmarks across -O2, -O3, -O3 -mavx2 and the baseline,
# to show how much the flags move the answer
./benchmarks/perf_baseline/run.sh --sweep
```

The XTEA benchmark compiles `src/xtea.cpp` itself rather than a copy, so it can
never drift from the shipped implementation. That file includes `otpch.h`, so
the script reuses the include list from a configured build tree. Configure one
first if you have not:

```bash
cmake -B build-release -DCMAKE_BUILD_TYPE=Release
```

Point it elsewhere with `TFS_BUILD_DIR=/path/to/build ./run.sh`.

## What each benchmark answers

### `bench_xtea.cpp`

Compares four implementations at every packet size, after proving all four
produce identical ciphertext and that `decrypt(encrypt(x)) == x`:

| name        | what it is                                                        |
|-------------|-------------------------------------------------------------------|
| `tfs`       | the loop in `src/xtea.cpp` — rounds outer, blocks inner            |
| `bt-scalar` | BlackTek's scalar path — blocks outer, rounds inner                |
| `bt-sse2`   | BlackTek's SSE2 kernel, 4 blocks at a time                         |
| `bt-avx2`   | BlackTek's AVX2 kernel, 8 blocks at a time                         |

Two things to keep in mind when reading the table:

- `bt-scalar` is the path BlackTek actually takes on Linux, because its
  `detect()` in `src/simd_dispatch.h` only implements CPUID for MSVC and ICC and
  falls through to `g_level = Level::Scalar` on GCC and Clang. Porting BlackTek's
  XTEA as-is means shipping that column.
- the winner flips with packet size. There is no single answer, which is why the
  distribution has to be measured — see below.

### `bench_adler.cpp`

`adlerChecksum()` in `src/tools.cpp` against zlib's `adler32_z()`. zlib is
already a required dependency (`find_package(ZLIB REQUIRED)`), so this would add
no packaging cost. Checks byte-identical output across sizes and payload
patterns, including the `> NETWORKMESSAGE_MAXSIZE` guard that returns 0, then
times both.

## Measuring the real packet size distribution

The XTEA table is only actionable next to the distribution of buffer sizes a
real server hands to `xtea::encrypt`. Build with the histogram enabled, run
under real load, and read the log:

```bash
cmake -B build-histogram -DCMAKE_BUILD_TYPE=Release -DENABLE_PACKET_SIZE_HISTOGRAM=ON
cmake --build build-histogram -j"$(nproc)"
./build-histogram/tfs
```

It reports both directions every 100k outgoing packets. The option is off by
default and the macros compile to nothing without it, so the production binary
is unaffected.
