param(
    [string]$BuildName = "build"
)

$ErrorActionPreference = "Stop"

# Sandbox/terminal tertentu memasukkan Path dan PATH sekaligus. MSBuild memakai
# Hashtable case-insensitive dan gagal menjalankan cl.exe bila keduanya ada.
$CanonicalPath = $env:Path
[Environment]::SetEnvironmentVariable(
    "PATH", $null, [EnvironmentVariableTarget]::Process
)
[Environment]::SetEnvironmentVariable(
    "Path", $CanonicalPath, [EnvironmentVariableTarget]::Process
)

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $VsWhere)) {
    throw "Visual Studio Build Tools belum ditemukan."
}

$VisualStudio = & $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $VisualStudio) {
    throw "MSVC C++ workload belum ditemukan."
}

$CMake = Join-Path $VisualStudio "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path -LiteralPath $CMake)) {
    throw "CMake bawaan Visual Studio belum ditemukan."
}

$BuildDirectory = Join-Path $ProjectRoot $BuildName
& $CMake --fresh -S $ProjectRoot -B $BuildDirectory -A x64
if ($LASTEXITCODE -ne 0) { throw "Konfigurasi CMake gagal ($LASTEXITCODE)." }

& $CMake --build $BuildDirectory --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "Build C++ gagal ($LASTEXITCODE)." }

& $CMake --build $BuildDirectory --config Release --target RUN_TESTS
if ($LASTEXITCODE -ne 0) { throw "Unit test C++ gagal ($LASTEXITCODE)." }

Write-Host "Executable: $BuildDirectory\Release\sack_rtsp.exe"
