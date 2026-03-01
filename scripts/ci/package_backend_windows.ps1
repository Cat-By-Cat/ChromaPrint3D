param(
    [Parameter(Mandatory = $true)]
    [string]$VcpkgInstalledDir
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Path pkg -Force | Out-Null
Copy-Item build\bin\chromaprint3d_server.exe pkg\ -Force

# vcpkg applocal copies most runtime DLLs next to the executable.
Get-ChildItem build\bin -Filter "*.dll" -ErrorAction SilentlyContinue |
    Copy-Item -Destination pkg\ -Force

# Keep a fallback copy from the vcpkg install root in case applocal misses some DLLs.
$vcpkgBin = Join-Path $VcpkgInstalledDir "x64-windows-release\bin"
if (Test-Path $vcpkgBin) {
    Get-ChildItem $vcpkgBin -Filter "*.dll" -ErrorAction SilentlyContinue |
        Copy-Item -Destination pkg\ -Force
}

# ONNX Runtime is not managed by vcpkg applocal.
Get-ChildItem build\_deps\onnxruntime-src\lib -Filter "onnxruntime*.dll" -ErrorAction SilentlyContinue |
    Copy-Item -Destination pkg\ -Force

# Drogon may resolve to system OpenSSL on GitHub Windows runners.
$opensslBin = "C:\Program Files\OpenSSL\bin"
if (Test-Path $opensslBin) {
    Get-ChildItem $opensslBin -Filter "libssl*.dll" -ErrorAction SilentlyContinue |
        Copy-Item -Destination pkg\ -Force
    Get-ChildItem $opensslBin -Filter "libcrypto*.dll" -ErrorAction SilentlyContinue |
        Copy-Item -Destination pkg\ -Force
}

# Bundle MSVC runtime/OpenMP redistributables for hosts without VC redist preinstalled.
if ($env:VCToolsRedistDir) {
    $crtDir = Join-Path $env:VCToolsRedistDir "x64\Microsoft.VC143.CRT"
    $ompDir = Join-Path $env:VCToolsRedistDir "x64\Microsoft.VC143.OpenMP"
    foreach ($dir in @($crtDir, $ompDir)) {
        if (Test-Path $dir) {
            Get-ChildItem $dir -Filter "*.dll" -ErrorAction SilentlyContinue |
                Copy-Item -Destination pkg\ -Force
        }
    }
}

Write-Host "Packaged runtime files:"
Get-ChildItem pkg -File | Sort-Object Name | ForEach-Object { $_.Name }
