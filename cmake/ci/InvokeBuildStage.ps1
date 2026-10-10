param([Parameter(Mandatory)][string]$Name, [string]$Executable, [string[]]$Arguments, [string]$CmdScript)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$directory = Join-Path $env:GITHUB_WORKSPACE 'build/diagnostics'
New-Item -ItemType Directory -Force $directory | Out-Null
if ($CmdScript) {
    $vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'MSVC was not found.' }
    $script = Join-Path $directory "$Name.cmd"
    $setup = 'call "{0}\VC\Auxiliary\Build\vcvars64.bat"' -f $vs
    @('@echo off', $setup, 'if errorlevel 1 exit /b 1', $CmdScript) | Set-Content -LiteralPath $script -Encoding ascii
    $Executable = 'cmd.exe'
    $Arguments = @('/d', '/c', $script)
}
if (-not $Executable) { throw 'Specify Executable or CmdScript.' }
$timer = [Diagnostics.Stopwatch]::StartNew()
& $Executable @Arguments 2>&1 | Tee-Object -FilePath (Join-Path $directory "$Name.log") | Out-Host
$code = $LASTEXITCODE
$timer.Stop()
if ($env:GITHUB_STEP_SUMMARY) {
    "| $Name | $code | $([math]::Round($timer.Elapsed.TotalSeconds, 1)) |" >> $env:GITHUB_STEP_SUMMARY
}
if ($code -ne 0) { throw "Stage '$Name' failed with exit code $code. See diagnostics/$Name.log." }
