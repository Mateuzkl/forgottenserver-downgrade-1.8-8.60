#!/usr/bin/env python3
"""Exercise real audit scripts with isolated command stubs; never start a server."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[1]
LUA = shutil.which("lua")
REQUIRED = ("rsync", "cp", "mkdir", "dirname", "basename", "tail")


@unittest.skipUnless(os.name == "posix" and LUA and all(map(shutil.which, REQUIRED)),
                     "Requires Bash, Lua and rsync; run inside Linux/WSL")
class AuditScriptsTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="tfs audit scripts ")
        self.root = Path(self.temporary.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.audit = self.root / "audit"
        self.baseline = self.audit / "baseline-source"
        (self.baseline / "src/tests").mkdir(parents=True)
        (self.baseline / "src/benchs").mkdir()
        (self.baseline / "data/scripts/eventcallbacks/creature").mkdir(parents=True)
        (self.audit / "baseline-build").mkdir()
        for name in ("game.h", "party.h", "events.h"):
            (self.baseline / "src" / name).write_text("immutable baseline " + name)
        for name in REQUIRED:
            (self.bin / name).symlink_to(shutil.which(name))
        self.environment = dict(os.environ, PATH=str(self.bin), MOCK_REPO=str(REPO),
                                MOCK_LUA=LUA, MOCK_ARGS=str(self.root / "arguments.jsonl"))
        self.tool("cmake", """
import json, os, sys
from pathlib import Path
args = sys.argv[1:]
with open(os.environ['MOCK_ARGS'], 'a') as f: f.write(json.dumps(args) + '\\n')
if '-S' in args:
    build = Path(args[args.index('-B') + 1]); build.mkdir(parents=True, exist_ok=True)
    (build / 'fixture-source').write_text(args[args.index('-S') + 1])
""")
        self.tool("ctest", """
import os, subprocess, sys
from pathlib import Path
args = sys.argv[1:]
source = Path((Path(args[args.index('--test-dir') + 1]) / 'fixture-source').read_text())
repo = Path(os.environ['MOCK_REPO'])
for name in ['tools/test_step_tile_callbacks.lua', 'data/events/scripts/player.lua']:
    assert (source / name).read_bytes() == (repo / name).read_bytes(), name
raise SystemExit(subprocess.call([os.environ['MOCK_LUA'],
    str(source / 'tools/test_step_tile_callbacks.lua'), str(source / 'data/events/scripts/player.lua')]))
""")
        self.tool("ninja", """
print('fake-cxx src/tests/CMakeFiles/test_combat.dir/Unity/unity_0_cxx.cxx.o -o src/tests/test_combat')
""")
        self.tool("fake-cxx", """
import os, sys
from pathlib import Path
args = sys.argv[1:]; output = Path(args[args.index('-o') + 1])
status = os.environ.get('MOCK_BENCH_EXIT' if output.name.startswith('bench_') else 'MOCK_TEST_EXIT', '0')
output.write_text('#!/bin/sh\\necho fixture-output\\nexit ' + status + '\\n')
output.chmod(0o755)
""")

    def tearDown(self):
        self.temporary.cleanup()

    def tool(self, name, source):
        path = self.bin / name
        path.write_text("#!/usr/bin/python3\n" + source)
        path.chmod(0o755)

    def run_script(self, name, *arguments):
        return subprocess.run(["/bin/bash", str(REPO / "scripts/profiling" / name),
                               str(self.audit), *arguments], env=self.environment,
                              text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    def test_candidate_copies_real_lua_inputs_with_optional_ccache(self):
        for cached in (False, True):
            if cached:
                self.tool("ccache", "raise SystemExit(0)\n")
            for mode in ("release", "asan"):
                with self.subTest(ccache=cached, mode=mode):
                    result = self.run_script("build_audit_candidate.sh", mode)
                    self.assertEqual(result.returncode, 0, result.stdout)
                    calls = [json.loads(line) for line in Path(self.environment['MOCK_ARGS']).read_text().splitlines()]
                    configure = next(call for call in reversed(calls) if '-S' in call)
                    self.assertIn('-DCMAKE_CXX_COMPILER_LAUNCHER=' + ('ccache' if cached else ''), configure)
                    if mode == 'asan':
                        self.assertIn('-DENABLE_ASAN=ON', configure)
                        self.assertIn('-DCMAKE_CXX_FLAGS=-fsanitize=undefined', configure)

    def test_candidate_replaces_stale_lua_inputs(self):
        shutil.copytree(self.baseline, self.audit / 'candidate-source')
        for relative in ('tools/test_step_tile_callbacks.lua', 'data/events/scripts/player.lua'):
            path = self.audit / 'candidate-source' / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('stale fixture')
        result = self.run_script('build_audit_candidate.sh')
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_expected_red_regression_does_not_mask_a_successful_benchmark(self):
        self.environment['MOCK_TEST_EXIT'] = '9'
        result = self.run_script('check_audit_baseline.sh', 'test_combat_messages', 'bench_networkmessage')
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn('test_combat_messages exit=9', result.stdout)
        self.assertIn('bench_networkmessage exit=0', result.stdout)

    def test_failed_benchmark_propagates_status_after_printing_its_log(self):
        self.environment['MOCK_BENCH_EXIT'] = '42'
        result = self.run_script('check_audit_baseline.sh', 'bench_networkmessage')
        self.assertEqual(result.returncode, 42, result.stdout)
        self.assertIn('bench_networkmessage exit=42', result.stdout)
        self.assertIn('fixture-output', result.stdout)

    def test_red_fixtures_leave_baseline_headers_untouched(self):
        result = self.run_script('check_audit_baseline.sh')
        self.assertEqual(result.returncode, 0, result.stdout)
        for name in ('game.h', 'party.h', 'events.h'):
            self.assertEqual((self.audit / 'regression-source/src' / name).read_bytes(),
                             (self.baseline / 'src' / name).read_bytes())
        header = (self.audit / 'regression-source/src/tests/protocolgame_audit_support.h').read_bytes()
        self.assertEqual(header, (REPO / 'scripts/profiling/fixtures/protocolgame_audit_support.h').read_bytes())


if __name__ == '__main__':
    unittest.main(verbosity=2)
