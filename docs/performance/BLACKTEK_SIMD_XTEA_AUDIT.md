# SIMD XTEA research audit

## Exact baselines and scope

Target: `perf/realistic-load-latency`, PR #318, `ff7600fa7d927dd63427244423aa8287c57e1d62`.
The user's explicit request is to work on this branch, overriding the attachment's
suggestion to create another research branch. No PR #319 changes are included.
Existing raid, daily-store and world edits are not part of this experiment.

BlackTek reference: `828b89395f28852fe2476655d30f1b90507c6066`.
OTLand reference: `1561fa91d0a0a879dd90792a87f82ea31083de03` (architecture only).

## Source audit before implementation

- `Connection::send` queues a per-recipient OutputMessage and posts
  `internalSend` to `socket.get_executor()`. That socket, read/write timers and
  handlers use a per-connection `asio::strand`. `internalSend` calls
  `Protocol::onSendMessage` before async_write; its capture retains the buffer.
- The incoming `Connection::parsePacket` calls `Protocol::onRecvMessage` on
  that same strand. XTEA decrypt runs there, before parsePacket posts gameplay
  work to the reactor. No new thread/strand/queue is needed.
- The local (ignored) `config.lua` currently sets `networkThreads=4` and
  `performanceMetricsEnabled=true`, **not** the attachment's assumed 2. The
  configuration and the live server must not be silently changed for a chart.
- Current encrypt/decrypt iterate rounds first, scanning/reloading/storing
  every block each round. Expanded round keys contain 64 uint32 words.
- Current CMake Release flags are O3, omit-frame-pointer, NDEBUG; native CPU
  tuning defaults ON, Unity ON and IPO when supported. Native tuning is already
  machine-specific; portable validation must use native tuning OFF.
- `ProtocolCryptoFrame` aggregates length/padding, XTEA and crypto header; it
  must not be described as pure XTEA time. Precise XTEA calls/bytes are currently
  unavailable. Diagnostics themselves default disabled in PerformanceMetrics.

## BlackTek runtime findings

`simd_dispatch.h` only implements CPUID/XGETBV detection under MSVC/Intel.
Its GCC/Clang branch explicitly resets `g_level=Scalar`; `otserv.cpp` calls
`detect()` once and the audited source does not replace that selection elsewhere.
Consequently GCC/Clang normal XTEA dispatch cannot enter its explicit SSE2/AVX2
branches. A focused executable will verify this on both installed compilers.

`premake5.lua` separately sets global x86_64 Release `vectorextensions "AVX2"`.
That permits auto-vectorized instructions outside runtime-guarded functions,
including scalar code; a runtime scalar selection is not a portable binary.
Do not attribute an external whole-server CPU observation to explicit AVX2 XTEA.

MSVC detection checks SSE2/SSE4.1, AVX2, OSXSAVE and XCR0 XMM/YMM state.
Unlike the proposed target implementation it does not explicitly check CPUID
AVX or the available CPUID maximum before querying leaf 7. No complete BlackTek
Windows/Linux gameplay workload has been executed here.

Useful ideas: four/eight independent 8-byte blocks retained in registers across
32 rounds, unaligned loads/stores, scalar remainder. Its mutable global dispatch
and global AVX2 build policy must not be copied.

## Chosen design / acceptance gates

Keep the scalar oracle and frozen PR #318 test reference. Separate SSE2/AVX2
translation units from neutral detection. GCC/Clang target attributes and MSVC
per-file options, excluded from Unity/PCH, must prevent accidental global AVX2.
Thread-safe immutable function-local backend selection, read-only status and a
compile-time force-scalar control; no mutable gameplay backend switch.

Tests must compare ciphertext and decrypt with the frozen baseline, including
unaligned buffers, exact batch boundaries, sentinel bytes, maximum packet size,
and full protocol framing. No length/padding/checksum/packet order changes.

Pure XTEA timers and fixed atomic byte counters are opt-in; no per-packet
logging/allocations/new locking. Quantitative claims require Release benchmarks
with matching flags and must separate layout improvement from explicit SIMD.

## Implementation

- Scalar, SSE2 and AVX2 kernels preserve the original 32 rounds, 64 expanded
  round keys, little-endian block layout and in-place API. Four/eight independent
  blocks stay in registers across rounds; unaligned loads/stores are used.
- AVX2 tails use SSE2 for four to seven blocks, then scalar for fewer blocks.
  The public API bypasses indirect dispatch for payloads below 32 bytes.
- Immutable, thread-safe function-local dispatch replaces mutable global state.
  GCC/Clang CPU builtins include OS AVX usability; MSVC checks maximum CPUID leaf,
  AVX, OSXSAVE, XCR0 XMM/YMM state and AVX2. Non-x86 selects scalar.
- No new global AVX2 compiler flag. Separate kernels are excluded from Unity/PCH.
  GCC/Clang use target attributes and noinline; MSVC applies /arch:AVX2 to its
  AVX2 translation unit only and disables LTCG for the explicit kernels.
- `TFS_XTEA_FORCE_SCALAR` defaults OFF and is a compile-time A/B control, not a
  live gameplay switch. Read-only backend status is logged once at startup.
- `Protocol::xteaEncrypt` and `Protocol::xteaDecrypt` bracket only XTEA and
  record bytes; crypto-header work has its own scope. `cryptoFrame` is retained.
  Added accounting uses fixed relaxed atomics, no packet allocations or logs.
  Metrics remain opt-in. Scoped elapsed time includes scheduler preemption;
  it is not thread CPU time. Independently exchanged report counters can cross
  reporting boundaries; ratios are diagnostic aggregates, not exact CPU shares.
- No transport, reactor, packet order, checksum, RSA, padding, gameplay, rate
  limiter or production configuration behavior was changed.

The MSVC full build exposed duplicate anonymous helper definitions when
container.cpp, player.cpp and tile.cpp were combined into a Unity translation
unit. They are now excluded from MSVC Unity individually, as with the isolated
crypto files; their C++ behavior was not changed.

## Correctness and build validation

The tests use always-active CHECK, not Release-disabled assert. Test keys cover
zero, all-one, incremental, realistic and 16 seeded random keys. Lengths include
zero, small tails, four/eight-block boundaries and 65,528 bytes. Offsets 0/1/3/7,
sentinels and exact allocations exercise unaligned access and overreads. Every
available backend is compared against the frozen PR #318 algorithm, decrypted
back and checked against a golden ciphertext. Sixteen simultaneous first users
exercise dispatch initialization. The forced build verifies Scalar selection.

Production `Protocol::onSendMessage` / `onRecvMessage` framing is compared with
the frozen reference, with and without checksums, old-storage sentinels, maximum
payloads, real 0x8C health and 0xB4 text payload serializers and padding bytes.
No change in on-wire bytes was detected.

| Build/check | Observed result |
| --- | --- |
| GCC 13.3, C++23 focused Release, native OFF, IPO | 3 cases passed |
| GCC 13.3 focused Release, native ON, IPO | 3 cases passed |
| GCC forced Scalar focused Release | 3 cases passed; Scalar selected |
| GCC focused Debug ASan + UBSan, leak detection enabled | 3 cases passed; no findings |
| Clang 18.1.3 focused portable Release | 3 cases passed |
| MSVC 19.51 focused C++23 Release | 3 cases passed |
| Full GCC Release, Unity, IPO, native ON | Server built; 6 selected CTest targets passed |
| Full GCC Release, Unity, IPO, native OFF | Server built; outputmessage and SIMD targets passed |
| Full MSVC Release x64, v145, C++23 | Server built after Unity isolation fix |
| BlackTek GCC/Clang detection probe | Both select Scalar on this AVX2-capable CPU |
| GCC baseline vectorization diagnostic | Original loop already vectorizes with native tuning |

The final six-target GCC run also covers source-list parity, connection write
lifetime, output framing, metrics counters, existing XTEA and SIMD tests. A
pre-existing MSVC chrono `last` shadow warning remains. No whole-server sanitizer,
Valgrind, TSan, non-x86 execution or actual older-CPU execution is claimed.

## Microbenchmark results

Machine: Intel Core i5-10300H, four cores/eight logical processors, AVX2/SSE2,
WSL Ubuntu 24.04. Same key/buffers/compiler flags are used for the frozen PR #318
baseline, retained scalar, public auto, explicit SSE2 and explicit AVX2.
Each size/direction uses five rotated matched samples, 1,000 warm-up calls and
30,000 measured calls per sample; the reported value is the median. This is an
isolated cryptography benchmark, not an end-to-end packet or server CPU test.

GCC 13.3 Release, native ON, IPO; public auto selects AVX2 (ns/operation):

| Bytes | Encrypt baseline | Encrypt auto | Speedup | Decrypt baseline | Decrypt auto | Speedup |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8 | 101.091 | 101.865 | 0.992x | 103.672 | 108.016 | 0.960x |
| 32 | 143.449 | 86.126 | 1.666x | 153.691 | 87.425 | 1.758x |
| 64 | 219.943 | 82.397 | 2.669x | 218.465 | 81.599 | 2.677x |
| 128 | 236.767 | 152.924 | 1.548x | 242.839 | 154.531 | 1.571x |
| 256 | 358.224 | 320.601 | 1.117x | 377.424 | 311.758 | 1.211x |
| 512 | 583.455 | 598.360 | 0.975x | 616.470 | 614.953 | 1.002x |
| 1024 | 1300.275 | 1188.502 | 1.094x | 1416.412 | 1300.259 | 1.089x |
| 4096 | 4463.317 | 4808.420 | 0.928x | 4794.216 | 4907.599 | 0.977x |

Native tuning already lets the compiler use AVX2 in the original algorithm.
The 4096-byte auto encrypt result is about 7.7% slower, not a throughput win.
Small single-block calls have no material speedup; the direct scalar bypass
avoids charging an indirect-call path to every tiny packet.

GCC Release, native OFF, IPO, on the same machine:

| Bytes | Encrypt baseline ns | Encrypt auto ns | Speedup | Decrypt baseline ns | Decrypt auto ns | Speedup |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8 | 98.512 | 100.063 | 0.985x | 99.327 | 99.735 | 0.996x |
| 32 | 139.864 | 79.618 | 1.757x | 147.271 | 84.268 | 1.748x |
| 64 | 169.457 | 85.240 | 1.988x | 165.823 | 83.173 | 1.994x |
| 128 | 262.477 | 156.777 | 1.674x | 252.110 | 149.488 | 1.686x |
| 256 | 485.607 | 322.490 | 1.506x | 482.447 | 322.130 | 1.498x |
| 512 | 924.770 | 632.883 | 1.461x | 928.957 | 630.850 | 1.473x |
| 1024 | 1790.793 | 1256.623 | 1.425x | 1812.477 | 1279.483 | 1.417x |
| 4096 | 6760.707 | 4686.503 | 1.443x | 6693.868 | 4709.499 | 1.421x |

This portable binary can enter explicit AVX2 only after runtime OS/CPU checks;
portable compilation is not the same as forcing Scalar on an AVX2 machine.
Explicit SSE2 is not universally faster than the compiler's scalar loop either:
portable 4096-byte encrypt is 8990.983 ns, versus baseline 6760.707 ns. These
data do not establish a gain on SSE2-only hardware. Compiler/size-dependent
regressions remain a performance acceptance limitation, not a correctness bug.

## Whole-server StressBot experiment

The source database was snapshotted into a private, isolated schema. Its data,
key and world files were copied into an ignored working directory. Benchmarks
use game port 17712, not the user's normal 7172. Source database/config/world
files are not restored or overwritten. Private dumps, credentials, generated
programs and copied bot changes are not repository deliverables.

Both binaries use GCC native Release/Unity/IPO, networkThreads=2, opt-in metrics,
the same map with 647 Demons, 1,000 existing seeded bot accounts, packet cap,
workload settings and seed 180252. The forced binary uses the retained
round-major scalar algorithm, which may itself be auto-vectorized with native
flags. It is not an exact, uninstrumented historical server executable.

The ignored bot copy corrects existing 8.60 speech parsing: monster speech types
19/20 carry positions and 0x28 is a re-login/death marker. Its self-tests and a
ten-bot gameplay pilot passed. The original StressBot repository is unchanged.

Initial trials restored player rows only and paced login attempts 60 ms apart.
That is insufficient for a rigorous fixture reset. First LOGIN_ONLY trials also
hit the original consecutive-connection rate limiter and admitted only 40 bots.
Later private trials allow a burst of 2,000 connections; the production setting
is unchanged. These admission-limited runs are discarded, not used for a CPU
claim. A revised run restores the complete private SQL snapshot between sides,
paces logins 150 ms apart, and requires the full population throughout the
45-second measurement window. Packet/workload limits and the 30-second client
login timeout are not relaxed to conceal overload.

Initial results below are diagnostic only. CPU is percent of **one** core;
divide by eight for an approximate percentage of this machine's logical CPU
capacity. Main-thread CPU is reactor CPU, confirmed by startServer's runLoop;
network CPU is inferred from the two startup IO worker TIDs, not ptrace-verified.

| Mode | Backend | Final connected / requested | CPU one-core % | Main % | Approx. RSS MiB | Valid |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| LOGIN_ONLY 600 | Scalar | 40/600 | 7.84 | 2.82 | 165.98 | No: rate limited |
| LOGIN_ONLY 600 | Auto | 40/600 | 8.37 | 3.33 | 163.67 | No: rate limited |
| REALISTIC 600 | Scalar | 600/600 | 138.88 | 92.37 | 381.71 | Individual run only |
| REALISTIC 600 | Auto | 553/600 | 121.36 | 97.39 | 399.35 | No: login timeouts |
| REALISTIC 1000 | Scalar | 574/1000 | 122.70 | 100.01 | 452.77 | No: login timeouts |
| REALISTIC 1000 | Auto | 524/1000 | 109.70 | 90.93 | 422.83 | No: login timeouts |
| TORTURE 1000 | Scalar | 564/1000 | 87.61 | 75.69 | 759.14 | No: login timeouts |
| TORTURE 1000 | Auto | 638/1000 | 113.24 | 96.34 | 1126.40 | No: login timeouts |

All these completed trials had zero parser errors/unknown opcodes. Missing bots
are still a hard validity failure; lower CPU with fewer connected bots does not
prove an optimization. Combat work also differs substantially: initial REALISTIC
600 reports 418 versus 162 successful attacks in the sampled metric windows.
The main/reactor thread approaches one-core saturation and login responses miss
the client's timeout. This does not establish that XTEA caused the timeouts.
XTEA is already on network strands; changing it cannot remove reactor gameplay
queue pressure by itself. No RTT result is available from this harness: ping
replies and local queue latency are not network round-trip measurements.

The completed full-fixture repeats also failed to produce a valid matched pair:

| Repeat / wait after login ramp | Backend | Final connected | CPU one-core % | Main % | RSS MiB | Measurement validity |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| 2 / 20 seconds | Auto | 600/600 | 124.00 | 93.16 | 400.43 | Individual run passes |
| 2 / 20 seconds | Scalar | 600/600 | 127.26 | 102.45 | 406.82 | First four seconds still admit bots |
| 3 / 50 seconds | Scalar | 557/600 | 126.15 | 98.14 | 507.19 | 43 login timeouts |
| 3 / 50 seconds | Auto | 563/600 | 119.03 | 103.24 | 516.24 | 37 login timeouts |

Repeat 2's observed CPU corresponds to about 15.5% versus 15.9% of eight logical
processors. That small difference is **not** an accepted saving: Scalar starts
the window at 564/600 and reaches 600 four seconds later. Its sampled successful
attacks are 195, versus Auto's 722, so equal final headcounts do not mean equal
completed work. Repeat 3 reserves the last login's 30-second timeout budget plus
20 seconds of settling before measurement, but loses bots on both sides. All
repeats still show zero parser errors/unknown opcodes. No client timeout was
extended, reconnect enabled or missing-bot criterion weakened to obtain a pass.

Per-thread percentages slightly above 100 are procfs sampling/accounting
artifacts; they indicate approximate single-core saturation, not extra cores.
RSS is a process snapshot, not evidence of a leak or measured live allocation
ownership. Until there is a valid matched pair, no whole-server CPU saving is
claimed. The 1,000-bot admission/measurement gate is not met either. Further
capacity work requires diagnosing reactor/login pressure separately from this
crypto-only port; these trials cannot justify a gameplay or scheduler change.

## Reproducing the focused checks

From the repository root, with GCC/Clang and CMake/Ninja available:

```sh
cmake -S tools/xtea -B build/xtea-portable -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DENABLE_NATIVE_OPTIMIZATIONS=OFF
cmake --build build/xtea-portable
ctest --test-dir build/xtea-portable --output-on-failure
build/xtea-portable/bench_xtea > build/xtea-portable.csv

cmake -S tools/xtea -B build/xtea-native -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DENABLE_NATIVE_OPTIMIZATIONS=ON
cmake --build build/xtea-native
build/xtea-native/bench_xtea > build/xtea-native.csv

cmake -S tools/xtea -B build/xtea-scalar -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DTFS_XTEA_FORCE_SCALAR=ON
cmake --build build/xtea-scalar
ctest --test-dir build/xtea-scalar --output-on-failure

cmake -S tools/xtea -B build/xtea-sanitized -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON -DENABLE_IPO=OFF
cmake --build build/xtea-sanitized
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/xtea-sanitized --output-on-failure
```

For the full server, use its normal dependency/toolchain setup and toggle only
`TFS_XTEA_FORCE_SCALAR` to build matched scalar/auto binaries. Preserve compiler,
IPO, Unity, native tuning, instrumentation and gameplay options between them.
The audited local full-build cache has been returned to force-scalar OFF.

## Recommendation

Correctness and build checks passed in the tested configurations. Medium-size
AVX2 gains and portable-build gains are real microbenchmark observations, but
native large-packet and explicit SSE2 regressions prevent a blanket performance
recommendation. Whole-server CPU reduction and 1,000 connected active bots remain
unproven. Keep this as experimental research on the requested branch; do not
describe it as a universal 50% CPU reduction or as a completed capacity fix.
