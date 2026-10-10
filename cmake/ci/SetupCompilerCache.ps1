$ErrorActionPreference = 'Stop'
$directory = Join-Path $env:RUNNER_TEMP 'sccache-0.16.0'
New-Item -ItemType Directory -Force $directory | Out-Null
$archive = Join-Path $directory 'sccache.tar.gz'
Invoke-WebRequest -Uri 'https://github.com/mozilla/sccache/releases/download/v0.16.0/sccache-v0.16.0-x86_64-pc-windows-msvc.tar.gz' -OutFile $archive -MaximumRetryCount 3 -RetryIntervalSec 2
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne 'c82319cc392b693e9fc72e6567cce47e218308f00fbf1df942e39b62e30f98da') { throw 'sccache archive checksum mismatch.' }
tar -xzf $archive -C $directory
if ($LASTEXITCODE -ne 0) { throw 'sccache extraction failed.' }
$executable = Join-Path $directory 'sccache-v0.16.0-x86_64-pc-windows-msvc/sccache.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw 'sccache executable missing.' }
& $executable --start-server
if ($LASTEXITCODE -ne 0) { throw 'sccache server startup failed; compiler fallback remains enabled.' }
Split-Path $executable >> $env:GITHUB_PATH
