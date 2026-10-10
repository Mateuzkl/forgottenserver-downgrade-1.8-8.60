$ErrorActionPreference = 'Stop'
$baseline = (Get-Content -LiteralPath vcpkg.json -Raw | ConvertFrom-Json).'builtin-baseline'
if ($baseline -notmatch '^[0-9a-f]{40}$') { throw 'vcpkg.json must pin builtin-baseline to a full SHA.' }
$vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'MSVC was not found.' }
$toolset = (Get-Content (Join-Path $vs 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt') -Raw).Trim()
"commit=$baseline" >> $env:GITHUB_OUTPUT
"toolset=$toolset" >> $env:GITHUB_OUTPUT
New-Item -ItemType Directory -Force build/diagnostics, $env:VCPKG_DEFAULT_BINARY_CACHE, $env:VCPKG_DOWNLOADS | Out-Null
@{commit=$env:GITHUB_SHA; runnerImage=$env:ImageVersion; compilerToolset=$toolset; vcpkgBaseline=$baseline; triplet='x64-windows-static'} | ConvertTo-Json | Set-Content build/diagnostics/environment.json
@('### Windows build stages','| Stage | Exit code | Seconds |','| --- | --- | --- |') | Add-Content $env:GITHUB_STEP_SUMMARY
