$ErrorActionPreference = 'Stop'
$oldCulture = [Threading.Thread]::CurrentThread.CurrentCulture
try {
    [Threading.Thread]::CurrentThread.CurrentCulture = [Globalization.CultureInfo]::GetCultureInfo('pt-BR')
    $result = & "$PSScriptRoot/../analyze-performance.ps1" -Path "$PSScriptRoot/performance-log-fixture.txt" | ConvertFrom-Json
    if ($result.CompleteWindows -ne 1 -or $result.IncompleteWindowsExcluded -ne 1) {
        throw 'Parser did not exclude the truncated window.'
    }
    $creatures = $result.Totals | Where-Object Metric -eq 'Game::checkCreatures'
    if ($creatures.Calls -ne 2 -or $creatures.TotalMs -ne 1.25 -or $creatures.MaxUs -ne 1000) {
        throw 'Parser changed units, decimal culture or inclusive totals.'
    }
    $window = $result.SlowestCreatureWindows[0]
    if ($window.Reactor.backlog_max -ne 7 -or $window.Path.failure -ne 1 -or
        $window.Area.recipients -ne 400 -or $window.Network.active -ne 400) {
        throw 'Parser lost the window counters.'
    }
    if ($result.SourceSha256.Length -ne 64) { throw 'Missing input fingerprint.' }
    Write-Output 'Performance log parser: all checks passed (including pt-BR and truncated input).'
} finally {
    [Threading.Thread]::CurrentThread.CurrentCulture = $oldCulture
}
