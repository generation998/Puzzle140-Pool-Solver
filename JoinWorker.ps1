# Join the puzzle-140 pool on every visible NVIDIA GPU.
#   .\JoinWorker.ps1
#   .\JoinWorker.ps1 72.62.76.118
#   .\JoinWorker.ps1 72.62.76.118 2
param(
    [Parameter(Position = 0)]
    [string]$HostName = "72.62.76.118",
    [Parameter(Position = 1)]
    [string]$GpuId = "",
    [string]$Exe = ""
)

$ErrorActionPreference = "Stop"
$here = $PSScriptRoot

function Find-WorkerExe {
    if ($Exe -and (Test-Path -LiteralPath $Exe)) { return (Resolve-Path $Exe).Path }
    $candidates = @(
        (Join-Path $here "tools\VanitySearchKang3.exe"),
        (Join-Path $here "..\VanitySearch-Bitcrack-kangaroo\x64\Release\VanitySearchKang3.exe"),
        (Join-Path $here "..\VanitySearch-Bitcrack-kangaroo\x64\Release\VanitySearch.exe")
    )
    foreach ($c in $candidates) {
        if (Test-Path -LiteralPath $c) { return (Resolve-Path $c).Path }
    }
    return $null
}

function Get-GpuIds {
    if ($env:GPUS) {
        return @($env:GPUS -split "," | ForEach-Object { $_.Trim() } | Where-Object { $_ -match '^\d+$' })
    }
    if ($GpuId -ne "") {
        return @([string]$GpuId)
    }
    if ($env:CUDA_VISIBLE_DEVICES) {
        $n = @($env:CUDA_VISIBLE_DEVICES -split "," | ForEach-Object { $_.Trim() } | Where-Object { $_ }).Count
        if ($n -gt 0) { return @(0..($n - 1)) }
    }
    $smi = $null
    $cmd = Get-Command nvidia-smi -ErrorAction SilentlyContinue
    if ($cmd) { $smi = $cmd.Source }
    if (-not $smi) {
        foreach ($p in @(
            "$env:SystemRoot\System32\nvidia-smi.exe",
            "${env:ProgramFiles}\NVIDIA Corporation\NVSMI\nvidia-smi.exe"
        )) {
            if (Test-Path -LiteralPath $p) { $smi = $p; break }
        }
    }
    if ($smi) {
        $lines = & $smi --query-gpu=index --format=csv,noheader,nounits 2>$null
        if ($LASTEXITCODE -eq 0 -and $lines) {
            $ids = @($lines | ForEach-Object { $_.ToString().Trim() } | Where-Object { $_ -match '^\d+$' })
            if ($ids.Count -gt 0) { return $ids }
        }
    }
    return @("0")
}

$exePath = Find-WorkerExe
if (-not $exePath) {
    Write-Host "Rebuild the kangaroo tree with -pool support:"
    Write-Host "  msbuild ..\VanitySearch-Bitcrack-kangaroo\VanitySearch.vcxproj /p:Configuration=Release /p:Platform=x64 /p:TargetName=VanitySearchKang3"
    Write-Host "Then copy VanitySearchKang3.exe into tools\ or leave it in x64\Release."
    exit 1
}

$gpuIds = @(Get-GpuIds)
$hostShort = $env:COMPUTERNAME
if (-not $hostShort) { $hostShort = "windows" }

Write-Host "Worker: $exePath"
Write-Host "Pool:   ${HostName}:17403"
Write-Host "GPUs:   $($gpuIds -join ' ')  ($($gpuIds.Count) process(es))"

$procs = @()
try {
    foreach ($g in $gpuIds) {
        $wname = "$hostShort-gpu$g"
        Write-Host "Starting $wname"
        $procs += Start-Process -FilePath $exePath -ArgumentList @(
            "-pool", "${HostName}:17403",
            "-gpuId", "$g",
            "-worker", $wname
        ) -NoNewWindow -PassThru
    }
    Write-Host "Ctrl+C stops all GPU workers."
    Wait-Process -Id ($procs.Id)
} finally {
    foreach ($p in $procs) {
        if ($p -and -not $p.HasExited) {
            Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        }
    }
}
