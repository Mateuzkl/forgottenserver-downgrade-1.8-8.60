#!/usr/bin/env python3
"""Regenerate the engine-provided Lua globals used by .luacheckrc.

luacheck cannot know which globals the C++ side injects into the Lua state, so
without this list it reports tens of thousands of false "accessing undefined
variable" warnings and becomes unusable. The list is derived from src/ rather
than maintained by hand, so it stays correct as enums are added.

Usage:  python3 tools/generate-lua-globals.py [--check]

  (no args)  print the sorted global names, one per line
  --check    exit non-zero if .luacheckrc is missing any of them
"""
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def engine_globals() -> set[str]:
    """Every name the C++ side registers as a Lua global."""
    source = "\n".join(
        p.read_text(encoding="utf-8", errors="replace") for p in sorted((ROOT / "src").glob("*.cpp"))
    )
    names: set[str] = set()

    # registerEnum(NAME) and registerEnum(Scope::NAME) both expose the last segment,
    # because the macro strips everything up to the final "::".
    for match in re.finditer(r"\bregisterEnum\(\s*([A-Za-z_][\w:]*)\s*\)", source):
        names.add(match.group(1).split("::")[-1])

    # registerEnumClass(Enum::Value) exposes a table named after the enum.
    for match in re.finditer(r"\bregisterEnumClass\(\s*([A-Za-z_][\w:]*)\s*\)", source):
        parts = match.group(1).split("::")
        if len(parts) >= 2:
            names.add(parts[-2])

    # The remaining registrations name their global with a string literal.
    for function in (
        "registerGlobalVariable",
        "registerGlobalBoolean",
        "registerGlobalMethod",
        "registerTable",
        "registerClass",
    ):
        for match in re.finditer(rf'\b{function}\(\s*"([^"]+)"', source):
            names.add(match.group(1))

    # Plain lua_register(state, "name", fn) — used for the free functions such as
    # createCombatArea and the logging helpers.
    for match in re.finditer(r'\blua_register\(\s*[A-Za-z_][\w>.-]*\s*,\s*"([^"]+)"', source):
        names.add(match.group(1))

    return {name for name in names if re.fullmatch(r"[A-Za-z_]\w*", name)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify .luacheckrc covers every global")
    args = parser.parse_args()

    names = sorted(engine_globals())
    if not args.check:
        print("\n".join(names))
        return 0

    declared = set(re.findall(r'"([^"]+)"', (ROOT / ".luacheckrc").read_text(encoding="utf-8")))
    missing = [name for name in names if name not in declared]
    if missing:
        print(f".luacheckrc is missing {len(missing)} engine globals:", file=sys.stderr)
        for name in missing[:20]:
            print(f"  {name}", file=sys.stderr)
        if len(missing) > 20:
            print(f"  ... and {len(missing) - 20} more", file=sys.stderr)
        print("\nRegenerate with: python3 tools/generate-lua-globals.py", file=sys.stderr)
        return 1

    print(f"OK: .luacheckrc covers all {len(names)} engine globals")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
