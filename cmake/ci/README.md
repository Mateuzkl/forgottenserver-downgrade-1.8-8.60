# CI builds and diagnostics

Dependency caches include MSVC and manifest/configuration hashes. Older caches
seed vcpkg, which checks ABIs before reuse. Successful configuration saves new
caches before source/test failures can discard the dependency build.
Compiler caching uses checksum-verified sccache 0.16.0 and GitHub Actions storage,
with GitHub-owned actions only. Cache server I/O failures allow compilation
without cache. Statistics collection cannot replace or fail successful tests.

Stage logs preserve native exit codes and elapsed seconds. Diagnostics expire
after seven days and contain configuration records, cache statistics and JUnit
reports for CTest jobs. CTest requires a nonempty suite; test isolation is retained.
Compiler optimizations, existing native fixtures and browser audits are unchanged.

Windows binary artifacts retain their previous scope, rather than bundling an
entire installation. They expire after fourteen days and validate x64 PE headers
and packaged DLL dependency closure. Windows resolves API-set imports.

The Linux/GCC job uses bounded ccache storage. Restored statistics
are reset before compilation so hit/miss reports describe the current run. Linking, tests and
package validation still execute on each run. Compare cold and warm timings
separately; CI validation does not replace interactive application testing.

The Windows and Linux/GCC CI jobs set ENABLE_TEST_LTO=OFF. Tests share a separate
library compiled from the same sources and dependency settings without IPO/LTO,
avoiding repeated whole-program optimization when linking each test executable.
The production tfslib and tfs keep their existing Release IPO/LTO settings.
This trades an extra library compilation for faster test linking; compiler caches
retain both variants. The option defaults to ON, preserving other build profiles.
Generated build.ninja files are included in diagnostics to inspect target flags.
