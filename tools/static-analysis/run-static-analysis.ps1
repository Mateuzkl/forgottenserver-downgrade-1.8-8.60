[CmdletBinding()]
param(
    [switch]$Cppcheck,
    [switch]$ClangTidy,
    [switch]$Lizard,
    [switch]$Iwyu,
    [switch]$Deep,
    [switch]$Strict,
    [string]$FixFile,
    [switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Show-Usage {
    @'
Usage: .\tools\static-analysis\run-static-analysis.ps1 [options]

Without a tool switch, all available tools run.

  -Cppcheck              Run only Cppcheck.
  -ClangTidy             Run only clang-tidy.
  -Lizard                Run only Lizard.
  -Iwyu                  Run only Include What You Use.
  -Deep                  Enable Cppcheck --inconclusive findings.
  -Strict                Return a failure when Lizard thresholds are exceeded.
  -FixFile <path>        Run clang-tidy --fix on one explicit file below src/.
  -Help                  Show this help.
'@ | Write-Output
}

if ($Help) {
    Show-Usage
    exit 0
}

$scriptParent = Split-Path -Parent $PSScriptRoot
$repositoryRoot = $null
$git = Get-Command git -ErrorAction SilentlyContinue
if ($git) {
    $gitRootOutput = & $git.Source -C $scriptParent rev-parse --show-toplevel 2>$null
    if (-not [string]::IsNullOrWhiteSpace($gitRootOutput)) {
        $repositoryRoot = $gitRootOutput.Trim()
    }
}
if ([string]::IsNullOrWhiteSpace($repositoryRoot)) {
    $repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
}
if (-not (Test-Path -LiteralPath (Join-Path $repositoryRoot 'CMakeLists.txt') -PathType Leaf) -or
    -not (Test-Path -LiteralPath (Join-Path $repositoryRoot 'src') -PathType Container)) {
    throw 'Unable to locate the project root.'
}

$buildDirectory = Join-Path $repositoryRoot 'build-analysis'
$reportDirectory = Join-Path $repositoryRoot 'analysis-reports'
$compileCommands = Join-Path $buildDirectory 'compile_commands.json'
$toolSwitchSpecified = $Cppcheck -or $ClangTidy -or $Lizard -or $Iwyu
$runCppcheck = if ($toolSwitchSpecified) { [bool]$Cppcheck } else { $true }
$runClangTidy = if ($toolSwitchSpecified) { [bool]$ClangTidy } else { $true }
$runLizard = if ($toolSwitchSpecified) { [bool]$Lizard } else { $true }
$runIwyu = if ($toolSwitchSpecified) { [bool]$Iwyu } else { $true }
$analysisFailures = 0
$buildReady = $false

if ($FixFile) {
    $runCppcheck = $false
    $runClangTidy = $true
    $runLizard = $false
    $runIwyu = $false
}

New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
Set-Location $repositoryRoot

function Write-SkippedReport {
    param([string]$Path, [string]$Message)

    Set-Content -LiteralPath $Path -Value $Message -Encoding utf8
    Write-Host $Message
}

function Configure-AnalysisBuild {
    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
    if (-not $cmake) {
        Write-SkippedReport (Join-Path $reportDirectory 'configuration.txt') 'CMake was not found; compilation-database tools were skipped.'
        $script:analysisFailures++
        return
    }

    Write-Host '==> Configuring analysis build'
    $configurationReport = Join-Path $reportDirectory 'configuration.txt'
    & $cmake.Source -S $repositoryRoot -B $buildDirectory `
        '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON' `
        '-DCMAKE_BUILD_TYPE=RelWithDebInfo' `
        '-DENABLE_UNITY_BUILD=OFF' `
        '-DENABLE_NATIVE_OPTIMIZATIONS=OFF' `
        '-DSKIP_GIT=ON' `
        '-DHTTP=ON' `
        '-DDISABLE_STATS=1' `
        '-DENABLE_SLOW_TASK_DETECTION=OFF' `
        '-DUSE_MIMALLOC=ON' `
        '-DENABLE_ASAN=OFF' `
        '-DBUILD_TESTING=OFF' `
        '-DBUILD_BENCHMARKING=OFF' *> $configurationReport

    if ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $compileCommands)) {
        $script:buildReady = $true
        return
    }

    Add-Content -LiteralPath $configurationReport -Value 'CMake configuration failed or did not produce compile_commands.json.'
    Write-Warning 'CMake configuration failed; inspect analysis-reports/configuration.txt.'
    $script:analysisFailures++
}

function Invoke-Cppcheck {
    $textReport = Join-Path $reportDirectory 'cppcheck.txt'
    $xmlReport = Join-Path $reportDirectory 'cppcheck.xml'
    $cppcheck = Get-Command cppcheck -ErrorAction SilentlyContinue
    if (-not $cppcheck) {
        Write-SkippedReport $textReport 'Cppcheck is not installed; install it to generate this report.'
        Write-SkippedReport $xmlReport 'Cppcheck is not installed; XML report was not generated.'
        return
    }
    if (-not $script:buildReady) {
        Write-SkippedReport $textReport 'Cppcheck was skipped because compile_commands.json is unavailable.'
        Write-SkippedReport $xmlReport 'Cppcheck was skipped because compile_commands.json is unavailable.'
        return
    }

    $jobs = [Environment]::ProcessorCount
    if ($jobs -lt 1) {
        $jobs = 2
    }
    $commonArguments = @(
        "-j$jobs",
        "--project=$compileCommands",
        '--std=c++23',
        '--enable=warning,style,performance,portability',
        '--inline-suppr',
        '--suppress=missingIncludeSystem',
        "--suppressions-list=$(Join-Path $repositoryRoot '.cppcheck-suppressions.txt')",
        "-i$(Join-Path $repositoryRoot 'build-analysis')",
        "-i$(Join-Path $repositoryRoot 'vcpkg_installed')",
        "-i$(Join-Path $repositoryRoot 'build')",
        "-i$(Join-Path $repositoryRoot '.git')",
        "-i$(Join-Path $repositoryRoot 'data')",
        "-i$(Join-Path $repositoryRoot 'modules')",
        "-i$(Join-Path $repositoryRoot 'screenshots')",
        "-i$(Join-Path $repositoryRoot 'docs')"
    )
    if ($Deep) {
        $commonArguments += '--inconclusive'
    }

    Write-Host '==> Running Cppcheck'
    & $cppcheck.Source @commonArguments '--template={file}:{line}:{column}: {severity}: {message} [{id}]' "--output-file=$textReport"
    $textStatus = $LASTEXITCODE
    & $cppcheck.Source @commonArguments '--xml' '--xml-version=2' "--output-file=$xmlReport"
    $xmlStatus = $LASTEXITCODE
    if ($textStatus -ne 0 -or $xmlStatus -ne 0) {
        $script:analysisFailures++
    }
}

function Resolve-FixFile {
    param([string]$Path)

    $resolved = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path
    $sourceRoot = (Join-Path $repositoryRoot 'src') + [IO.Path]::DirectorySeparatorChar
    $validExtension = [IO.Path]::GetExtension($resolved).ToLowerInvariant() -in '.cpp', '.cc', '.cxx'
    if (-not $resolved.StartsWith($sourceRoot, [StringComparison]::OrdinalIgnoreCase) -or -not $validExtension) {
        throw '-FixFile must name an existing .cpp, .cc, or .cxx file below src/.'
    }
    return $resolved
}

function Invoke-ClangTidy {
    $report = Join-Path $reportDirectory 'clang-tidy.txt'
    if (-not $script:buildReady) {
        Write-SkippedReport $report 'clang-tidy was skipped because compile_commands.json is unavailable.'
        return
    }

    if ($FixFile) {
        $sourceFile = Resolve-FixFile $FixFile
        $clangTidy = Get-Command clang-tidy -ErrorAction SilentlyContinue
        if (-not $clangTidy) {
            Write-SkippedReport $report 'clang-tidy is not installed; the requested fix was not applied.'
            $script:analysisFailures++
            return
        }
        Write-Host "==> Running clang-tidy fix for $sourceFile"
        & $clangTidy.Source -p $buildDirectory --fix $sourceFile *> $report
        if ($LASTEXITCODE -ne 0) {
            $script:analysisFailures++
        }
        return
    }

    Write-Host '==> Running clang-tidy'
    $runner = Get-Command run-clang-tidy -ErrorAction SilentlyContinue
    if (-not $runner) {
        $runner = Get-Command run-clang-tidy.py -ErrorAction SilentlyContinue
    }
    if ($runner) {
        & $runner.Source -p $buildDirectory *> $report
        if ($LASTEXITCODE -ne 0) {
            $script:analysisFailures++
        }
        return
    }

    $clangTidy = Get-Command clang-tidy -ErrorAction SilentlyContinue
    if (-not $clangTidy) {
        Write-SkippedReport $report 'clang-tidy is not installed; install clang-tidy to generate this report.'
        return
    }

    Set-Content -LiteralPath $report -Value '' -Encoding utf8
    $sourceFiles = @(Get-ChildItem -LiteralPath (Join-Path $repositoryRoot 'src') -Recurse -File |
        Where-Object { $_.Extension -in '.cpp', '.cc', '.cxx' })
    $status = 0
    if ($sourceFiles.Count -gt 0) {
        & $clangTidy.Source -p $buildDirectory @($sourceFiles.FullName) *>> $report
        $status = $LASTEXITCODE
    }
    if ($status -ne 0) {
        $script:analysisFailures++
    }
}

function Invoke-Lizard {
    $report = Join-Path $reportDirectory 'lizard.txt'
    $rawReport = Join-Path $reportDirectory 'lizard-raw.txt'
    $lizard = Get-Command lizard -ErrorAction SilentlyContinue
    if (-not $lizard) {
        Write-SkippedReport $report 'Lizard is not installed; install it to generate this report.'
        return
    }

    Write-Host '==> Running Lizard'
    & $lizard.Source -V -C 20 -L 180 -a 8 (Join-Path $repositoryRoot 'src') *> $rawReport
    $status = $LASTEXITCODE
    Get-Content -LiteralPath $rawReport | Set-Content -LiteralPath $report -Encoding utf8
    Add-Content -LiteralPath $report -Value "`nTop 30 functions by cyclomatic complexity:"
    $seenLines = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
    Select-String -LiteralPath $rawReport -Pattern '^\s*(\d+)\s+(\d+)\s+\d+\s+\d+\s+\d+\s+.+' |
        Sort-Object { [int]$_.Matches[0].Groups[2].Value } -Descending |
        Where-Object { $seenLines.Add($_.Line) } |
        Select-Object -First 30 |
        ForEach-Object { Add-Content -LiteralPath $report -Value $_.Line }
    Remove-Item -LiteralPath $rawReport -Force

    if ($status -ne 0 -and $Strict) {
        Write-Warning 'Lizard thresholds were exceeded in strict mode.'
        $script:analysisFailures++
    }
}

function Invoke-Iwyu {
    $report = Join-Path $reportDirectory 'iwyu.txt'
    if (-not $script:buildReady) {
        Write-SkippedReport $report 'IWYU was skipped because compile_commands.json is unavailable.'
        return
    }

    $iwyuTool = Get-Command iwyu_tool.py -ErrorAction SilentlyContinue
    if (-not $iwyuTool) {
        $iwyuTool = Get-Command iwyu_tool -ErrorAction SilentlyContinue
    }
    if ($iwyuTool) {
        Write-Host '==> Running IWYU'
        & $iwyuTool.Source -p $buildDirectory *> $report
        if ($LASTEXITCODE -ne 0) {
            $script:analysisFailures++
        }
        return
    }
    if (Get-Command include-what-you-use -ErrorAction SilentlyContinue) {
        Write-SkippedReport $report 'include-what-you-use is installed, but no iwyu_tool driver was found for compile_commands.json. Install the IWYU tools package.'
        return
    }
    Write-SkippedReport $report 'IWYU is not installed; this optional report was skipped.'
}

if ($runCppcheck -or $runClangTidy -or $runIwyu) {
    Configure-AnalysisBuild
}
if ($runCppcheck) { Invoke-Cppcheck }
if ($runClangTidy) { Invoke-ClangTidy }
if ($runLizard) { Invoke-Lizard }
if ($runIwyu) { Invoke-Iwyu }

Write-Host ''
Write-Host "Static-analysis reports: $reportDirectory"
Write-Host 'Cppcheck: cppcheck.txt and cppcheck.xml'
Write-Host 'clang-tidy: clang-tidy.txt'
Write-Host 'Lizard: lizard.txt'
Write-Host 'IWYU: iwyu.txt'

if ($analysisFailures -ne 0) {
    Write-Error "Static analysis completed with $analysisFailures tool/configuration failure(s)."
}
