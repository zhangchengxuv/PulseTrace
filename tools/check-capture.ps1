param(
    [Parameter(Mandatory = $true)][string]$Report,
    [Parameter(Mandatory = $true)][string]$Expected
)
# Expected is an independent hardware-generator count summary, never a replay output.
$ErrorActionPreference = 'Stop'
$capture = Get-Content -LiteralPath $Report -Raw -Encoding UTF8 | ConvertFrom-Json
$expectedCounts = Get-Content -LiteralPath $Expected -Raw -Encoding UTF8 | ConvertFrom-Json
if ($capture.mode -ne 'raw_input_capture') { throw 'A real raw_input_capture report is required' }
if ($capture.metrics.mouse_packets -eq 0) { throw 'No mouse packets captured; accuracy cannot be evaluated' }
if ($capture.metrics.read_errors -ne 0 -or $capture.metrics.message_errors -ne 0) { throw 'Raw Input read/message errors occurred' }
$names = @('left', 'right', 'middle', 'mouse4', 'mouse5')
$rows = @()
foreach ($name in $names) {
    [decimal]$observed = 0
    foreach ($source in $capture.lifetime_by_source.PSObject.Properties) { $observed += [decimal]$source.Value.clicks.$name }
    if ($null -eq $expectedCounts.clicks.$name) { throw "Expected click count missing: $name" }
    [decimal]$wanted = $expectedCounts.clicks.$name
    if ($wanted -lt 0) { throw 'Expected counts cannot be negative' }
    $rows += [pscustomobject]@{
        Metric = $name; Expected = $wanted; Observed = $observed
        Missing = [Math]::Max([decimal]0, $wanted - $observed)
        Extra = [Math]::Max([decimal]0, $observed - $wanted)
        LossPercent = if ($wanted -gt 0) { [Math]::Max([decimal]0, $wanted - $observed) / $wanted * 100 } else { $null }
    }
}
foreach ($metric in @('wheel_up_events', 'wheel_down_events', 'wheel_up_delta', 'wheel_down_delta')) {
    if ($null -eq $expectedCounts.$metric) { throw "Expected wheel count missing: $metric" }
    [decimal]$observed = 0
    foreach ($source in $capture.lifetime_by_source.PSObject.Properties) { $observed += [decimal]$source.Value.$metric }
    [decimal]$wanted = $expectedCounts.$metric
    if ($wanted -lt 0) { throw 'Expected wheel values cannot be negative' }
    $rows += [pscustomobject]@{
        Metric = $metric; Expected = $wanted; Observed = $observed
        Missing = [Math]::Max([decimal]0, $wanted - $observed)
        Extra = [Math]::Max([decimal]0, $observed - $wanted)
        LossPercent = if ($wanted -gt 0) { [Math]::Max([decimal]0, $wanted - $observed) / $wanted * 100 } else { $null }
    }
}
$rows | Format-Table -AutoSize
if ($rows | Where-Object { $_.Missing -gt 0 -or $_.Extra -gt 0 }) { throw 'Counts differ from independent generator totals' }
Write-Output 'Counts match. This validates the stated generator run, not universal hardware losslessness.'
