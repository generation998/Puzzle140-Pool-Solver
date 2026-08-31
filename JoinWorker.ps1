# PowerShell:  .\JoinWorker.ps1 127.0.0.1
#              .\JoinWorker.ps1 192.168.1.10 0
param(
    [Parameter(Position = 0)]
    [string]$HostName = "72.62.76.118",
    [Parameter(Position = 1)]
    [int]$GpuId = 0
)
& "$PSScriptRoot\JoinWorker.bat" $HostName $GpuId
