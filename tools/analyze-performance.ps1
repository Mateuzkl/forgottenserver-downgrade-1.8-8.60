# Read-only analysis of the existing [Perf] log format. No server/config/DB changes.
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Path)

$ErrorActionPreference = 'Stop'
$source = Get-Item -LiteralPath $Path
$windows = [System.Collections.Generic.List[object]]::new()
$current = $null
$incomplete = 0
foreach ($line in [System.IO.File]::ReadLines($source.FullName)) {
    if ($line -match '^\[(?<time>[^\]]+)\].*\[Perf\] TaskReactor::runOnce calls=') {
        if ($null -ne $current) { $incomplete++ }
        $current = [pscustomobject]@{
            Time = $Matches.time; Metrics = @{}; Reactor = @{}; Area = @{}; Path = @{}; Network = @{}
        }
    }
    if ($null -eq $current) { continue }
    if ($line -match '\[Perf\] (?<metric>[^ ]+) calls=(?<calls>\d+) total_ms=(?<total>[\d.]+) avg_us=[\d.]+ max_us=(?<max>[\d.]+).*p95_us=(?<p95>[\d.]+) p99_us=(?<p99>[\d.]+)') {
        $culture = [Globalization.CultureInfo]::InvariantCulture
        $current.Metrics[$Matches.metric] = [pscustomobject]@{
            Calls = [long]$Matches.calls
            TotalMs = [double]::Parse($Matches.total, $culture)
            MaxUs = [double]::Parse($Matches.max, $culture)
            P95Us = [double]::Parse($Matches.p95, $culture)
            P99Us = [double]::Parse($Matches.p99, $culture)
        }
    } elseif ($line -match '\[Perf\] (?<kind>reactor queue=|area_combat casts=|path requests=|network accept_started=)') {
        $kind = switch -Regex ($Matches.kind) {
            '^reactor' { 'Reactor' }
            '^area_combat' { 'Area' }
            '^path' { 'Path' }
            '^network' { 'Network' }
        }
        foreach ($pair in [regex]::Matches($line, '(?<key>\w+)=(?<value>\d+)')) {
            $current.$kind[$pair.Groups['key'].Value] = [long]$pair.Groups['value'].Value
        }
        if ($kind -eq 'Network') {
            $windows.Add($current)
            $current = $null
        }
    }
}
if ($null -ne $current) { $incomplete++ }

$totals = @{}
foreach ($window in $windows) {
    foreach ($entry in $window.Metrics.GetEnumerator()) {
        if (-not $totals.ContainsKey($entry.Key)) {
            $totals[$entry.Key] = [pscustomobject]@{ Metric = $entry.Key; Calls = [long]0; TotalMs = 0.0; MaxUs = 0.0 }
        }
        $total = $totals[$entry.Key]
        $total.Calls += $entry.Value.Calls
        $total.TotalMs += $entry.Value.TotalMs
        $total.MaxUs = [math]::Max($total.MaxUs, $entry.Value.MaxUs)
    }
}

[pscustomobject]@{
    SourceSha256 = (Get-FileHash -LiteralPath $source.FullName -Algorithm SHA256).Hash
    CompleteWindows = $windows.Count
    IncompleteWindowsExcluded = $incomplete
    Warning = 'Inclusive durations overlap; queue latency is waiting, not CPU. Percentiles are per-window histogram bounds, not exact global percentiles.'
    Totals = @($totals.Values | Sort-Object TotalMs -Descending)
    SlowestCreatureWindows = @($windows | Where-Object { $_.Metrics.ContainsKey('Game::checkCreatures') } |
        Sort-Object { $_.Metrics['Game::checkCreatures'].TotalMs } -Descending | Select-Object -First 5 |
        ForEach-Object { [pscustomobject]@{
            Time = $_.Time; Creatures = $_.Metrics['Game::checkCreatures']
            AreaApply = $_.Metrics['Combat::area.applyTargets']; PathTiming = $_.Metrics['Map::getPathMatching']
            QueueLatency = $_.Metrics['TaskReactor::queueLatency']; Reactor = $_.Reactor
            Area = $_.Area; Path = $_.Path; Network = $_.Network
        } })
} | ConvertTo-Json -Depth 7
