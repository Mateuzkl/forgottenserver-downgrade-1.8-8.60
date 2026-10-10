param([Parameter(Mandatory)][string]$Executable)
$ErrorActionPreference = 'Stop'
$exe = Get-Item -LiteralPath $Executable
$directory = $exe.DirectoryName
$binaries = @($exe) + @(Get-ChildItem -LiteralPath $directory -Filter '*.dll' -File)
foreach ($binary in $binaries) {
    $bytes = [IO.File]::ReadAllBytes($binary.FullName)
    if ($bytes.Length -lt 64) { throw "Invalid PE file: $($binary.Name)" }
    $offset = [BitConverter]::ToInt32($bytes, 0x3c)
    if ($offset -lt 0 -or $offset + 26 -gt $bytes.Length -or [Text.Encoding]::ASCII.GetString($bytes, $offset, 4) -ne "PE" + [char]0 + [char]0 -or [BitConverter]::ToUInt16($bytes, $offset + 4) -ne 0x8664 -or [BitConverter]::ToUInt16($bytes, $offset + 24) -ne 0x20b) { throw "Expected x64 PE32+: $($binary.Name)" }
    $output = & dumpbin /dependents $binary.FullName
    if ($LASTEXITCODE -ne 0) { throw "dumpbin failed: $($binary.Name)" }
    $output | Out-Host
    foreach ($line in $output) {
        if ($line -match '^\s+([A-Za-z0-9_.-]+\.dll)\s*$') {
            $dependency = $Matches[1]
            if ($dependency -match '^(api-ms-win-|ext-ms-win-)') { continue }
            if (-not (Test-Path -LiteralPath (Join-Path $directory $dependency)) -and -not (Test-Path -LiteralPath (Join-Path "$env:SystemRoot/System32" $dependency))) { throw "Missing runtime dependency: $dependency (required by $($binary.Name))" }
        }
    }
}
Write-Host "Validated x64 executable and $($binaries.Count - 1) packaged DLLs."
