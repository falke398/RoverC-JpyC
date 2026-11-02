param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$')]
    [string]$ControllerMac,
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$')]
    [string]$RoverMac
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$target = Join-Path $root 'include\LocalConfig.h'
if (Test-Path $target) { throw 'LocalConfig.h already exists; no keys were changed.' }
if ($ControllerMac -eq $RoverMac) { throw 'The two MAC addresses must differ.' }
foreach ($mac in @($ControllerMac, $RoverMac)) {
    if (([Convert]::ToByte($mac.Split(':')[0], 16) -band 1) -ne 0 -or
        $mac -eq '00:00:00:00:00:00') {
        throw 'Each device must have a nonzero unicast MAC address.'
    }
}

$config = [IO.File]::ReadAllText((Join-Path $root 'include\LocalConfig.example.h'))
$rng = [Security.Cryptography.RandomNumberGenerator]::Create()
try {
    $replacements = @{
        'constexpr bool Provisioned = false;' = 'constexpr bool Provisioned = true;'
    }
    foreach ($role in @('Controller', 'Rover')) {
        $mac = if ($role -eq 'Controller') { $ControllerMac } else { $RoverMac }
        $values = ($mac.Split(':') | ForEach-Object { '0x' + $_.ToUpperInvariant() }) -join ', '
        $replacements["constexpr uint8_t ${role}Mac[6] = {0, 0, 0, 0, 0, 0};"] =
            "constexpr uint8_t ${role}Mac[6] = {$values};"
    }
    $previousKey = ''
    foreach ($name in @('PrimaryKey', 'LinkKey')) {
        do {
            $bytes = New-Object byte[] 16
            $rng.GetBytes($bytes)
            $values = ($bytes | ForEach-Object { '0x{0:X2}' -f $_ }) -join ', '
        } while (($bytes | Select-Object -Unique).Count -lt 2 -or $values -eq $previousKey)
        $previousKey = $values
        $replacements["constexpr uint8_t ${name}[16] = {};"] =
            "constexpr uint8_t ${name}[16] = {$values};"
    }
    foreach ($original in $replacements.Keys) {
        if (-not $config.Contains($original)) { throw 'Provisioning template does not match.' }
        $config = $config.Replace($original, $replacements[$original])
    }
    foreach ($gate in @('MotionCommissioned', 'GripperCommissioned')) {
        if (-not $config.Contains("constexpr bool $gate = false;")) {
            throw 'Commissioning must remain disabled.'
        }
    }
    $stream = [IO.File]::Open($target, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write)
    try {
        $encoded = [Text.Encoding]::ASCII.GetBytes($config)
        $stream.Write($encoded, 0, $encoded.Length)
    } finally { $stream.Dispose() }
} finally { $rng.Dispose() }
Write-Output 'LocalConfig.h generated. Radio provisioned; motion and gripper locked. Keys not displayed.'