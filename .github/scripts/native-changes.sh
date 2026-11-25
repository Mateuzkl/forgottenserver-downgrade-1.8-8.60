#!/usr/bin/env bash
set -euo pipefail

# PRs compare the full branch; pushes compare only the pushed commits.
# Manual runs and unavailable history always retain the native build.
build=true
event=${EVENT_NAME:-workflow_dispatch}
head=${HEAD_SHA:-$(git rev-parse HEAD)}
base=${BASE_SHA:-}
if [[ $event != workflow_dispatch ]]; then
  if [[ $base =~ ^0+$ ]]; then
    base=$(git merge-base "origin/${DEFAULT_BRANCH:-main}" "$head" 2>/dev/null || true)
  elif [[ $event == pull_request ]]; then
    base=$(git merge-base "$base" "$head" 2>/dev/null || true)
  fi
  if [[ -n $base ]] && git cat-file -e "$base^{commit}" 2>/dev/null; then
    changed=$(mktemp)
    trap 'rm -f "$changed"' EXIT
    if git diff --no-renames --name-only -z "$base" "$head" -- > "$changed"; then
      build=false
      while IFS= read -r -d '' path; do
        case "$path" in
          *.lua) continue ;;
          *.c|*.cc|*.cpp|*.cxx|*.h|*.hh|*.hpp|*.hxx|*.inl|*.ipp|*.m|*.mm|*.rc|*.cmake|*.vcxproj|*.props|*.targets|\
          src/*|CMakeLists.txt|*/CMakeLists.txt|CMakePresets.json|cmake/*|vcpkg*.json|vcpkg-overlays/*|\
          Dockerfile|.dockerignore|build.sh|browser/*|vc18/*|vc23/*|tools/patches/*)
            build=true
            break
            ;;
        esac
      done < "$changed"
    fi
  fi
fi
echo "Native compilation required: $build"
if [[ -n ${GITHUB_OUTPUT:-} ]]; then
  echo "build=$build" >> "$GITHUB_OUTPUT"
fi
