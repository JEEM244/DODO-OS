[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$IsoPath,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ChecksumPath
)

$ErrorActionPreference = 'Stop'

try {
    $resolvedIso = (Resolve-Path -LiteralPath $IsoPath -ErrorAction Stop).Path
    $resolvedChecksum = (Resolve-Path -LiteralPath $ChecksumPath -ErrorAction Stop).Path
    $checksumLine = [System.IO.File]::ReadAllText($resolvedChecksum).Trim()

    if ([string]::IsNullOrWhiteSpace($checksumLine)) {
        throw 'Checksum file is empty.'
    }

    $expected = ($checksumLine -split '\s+')[0].ToLowerInvariant()
    if ($expected -notmatch '^[0-9a-f]{64}$') {
        throw 'Checksum file must begin with a 64-character SHA-256 digest.'
    }

    $actual = (Get-FileHash -LiteralPath $resolvedIso -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -cne $expected) {
        [Console]::Error.WriteLine("SHA-256 mismatch. Expected $expected, got $actual.")
        exit 1
    }

    Write-Host "SHA-256 OK: $actual"
    exit 0
}
catch {
    [Console]::Error.WriteLine("SHA-256 verification failed: $($_.Exception.Message)")
    exit 2
}
