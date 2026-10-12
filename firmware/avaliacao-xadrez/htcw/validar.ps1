param(
    [string]$CMake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    [string]$ArduinoCli = 'C:\Program Files\Arduino CLI\arduino-cli.exe',
    [switch]$NativeOnly
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$candidateRoot = $PSScriptRoot
$validation = Join-Path $candidateRoot 'validation'
$nativeBuild = Join-Path $candidateRoot 'build/native'
$steps = [System.Collections.Generic.List[object]]::new()
New-Item -ItemType Directory -Force -Path $validation | Out-Null
function Run-Step {
    param([string]$Name, [string]$Executable, [string[]]$Arguments)
    $lines = & $Executable @Arguments 2>&1
    $code = $LASTEXITCODE
    $body = ($lines | ForEach-Object { $_.ToString() }) -join "`n"
    $body | Set-Content -LiteralPath (Join-Path $validation "$Name.log") -Encoding utf8
    $steps.Add([ordered]@{ name = $Name; exitCode = $code; log = "$Name.log" })
    Write-Host $body
    return $code
}
$configure = Run-Step 'native-configure' $CMake @('-S', $candidateRoot, '-B', $nativeBuild, '-G', 'Visual Studio 17 2022', '-A', 'x64')
if ($configure -ne 0) { throw 'Native configuration failed.' }
$build = Run-Step 'native-build' $CMake @('--build', $nativeBuild, '--config', 'Release')
if ($build -ne 0) { throw 'Native build failed.' }
$nativeTests = Run-Step 'native-tests' (Join-Path (Split-Path -Parent $CMake) 'ctest.exe') @('--test-dir', $nativeBuild, '-C', 'Release', '--output-on-failure', '--no-tests=error', '--output-junit', (Join-Path $validation 'native-tests.xml'))
$esp32 = $null
if (-not $NativeOnly) {
    $esp32 = Run-Step 'esp32-compile' $ArduinoCli @('compile', '--fqbn', 'esp32:esp32:esp32', '--jobs', '2', '--warnings', 'all', '--library', (Join-Path $candidateRoot 'vendor/htcw_chess'), '--build-path', (Join-Path $candidateRoot 'build/esp32'), (Join-Path $candidateRoot 'tests/esp32_compile'))
}
$sourceHashes = [ordered]@{}
foreach ($source in @('vendor/htcw_chess/src/chess.h', 'vendor/htcw_chess/src/source/chess.c', 'vendor/htcw_chess/LICENSE', 'vendor/htcw_chess/library.properties', 'tests/test_htcw.cpp', 'tests/esp32_compile/esp32_compile.ino', 'CMakeLists.txt', 'validar.ps1')) {
    $sourceHashes[$source] = (Get-FileHash -LiteralPath (Join-Path $candidateRoot $source) -Algorithm SHA256).Hash.ToLowerInvariant()
}
[ordered]@{
    performedAtUtc = [DateTime]::UtcNow.ToString('o')
    library = 'htcw_chess'; version = '0.5.3'; commit = '5bdd175c88e2d18202bf136544813b11256a2bb0'
    upstream = 'https://github.com/codewitch-honey-crisis/htcw_chess'; license = 'MIT'
    evaluationCompleted = $true; nativeBuildPassed = ($build -eq 0); nativeTestsPassed = ($nativeTests -eq 0)
    esp32BuildPassed = $(if ($null -eq $esp32) { $null } else { $esp32 -eq 0 })
    cmakeVersion = (& $CMake --version | Out-String).Trim()
    arduinoCliVersion = (& $ArduinoCli version | Out-String).Trim()
    installedCores = (& $ArduinoCli core list | Out-String).Trim()
    nativeConfiguration = 'MSVC x64 Release; upstream C99 /W4; tests C++14 /W4 /WX; _CRT_SECURE_NO_WARNINGS for standard sprintf'
    fqbn = 'esp32:esp32:esp32'; steps = @($steps.ToArray()); sourceSha256 = $sourceHashes
    uploadPerformed = $false; physicalTestsPerformed = $false
    notes = 'Failed rule tests are evidence against the candidate, not expected-success tests. No upstream source changes; no integration.'
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $validation 'resultado.json') -Encoding utf8
if ($nativeTests -ne 0 -or ($null -ne $esp32 -and $esp32 -ne 0)) { exit 1 }
