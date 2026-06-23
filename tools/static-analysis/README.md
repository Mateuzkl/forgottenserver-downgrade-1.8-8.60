# Static Analysis

Scripts in this directory generate local static-analysis reports for the
server source. Reports are written to `analysis-reports/` and are not tracked
by Git.

## Requirements

Install the project's build dependencies first. On Ubuntu or WSL, the optional
analysis tools can be installed with:

```bash
sudo apt update
sudo apt install -y cppcheck clang-tidy iwyu python3-pip
python3 -m pip install --user lizard
```

## Usage

Run all available tools:

```bash
bash tools/static-analysis/run-static-analysis.sh
```

Run a single tool:

```bash
bash tools/static-analysis/run-static-analysis.sh --cppcheck
bash tools/static-analysis/run-static-analysis.sh --clang-tidy
bash tools/static-analysis/run-static-analysis.sh --lizard
bash tools/static-analysis/run-static-analysis.sh --iwyu
```

PowerShell is also supported:

```powershell
.\tools\static-analysis\run-static-analysis.ps1
```

Use `--help` or `-Help` to list the available options.

## Reports

- `configuration.txt`: CMake configuration output.
- `cppcheck.txt` and `cppcheck.xml`: Cppcheck results.
- `clang-tidy.txt`: clang-tidy results.
- `lizard.txt`: complexity results.
- `iwyu.txt`: Include What You Use results.
