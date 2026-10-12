param(
    [string]$CMake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    [string]$ArduinoCli = 'C:\Program Files\Arduino CLI\arduino-cli.exe',
    [string]$Esp32Nm = 'C:\Users\danie\AppData\Local\Arduino15\packages\esp32\tools\esp-x32\2601\bin\xtensa-esp32-elf-nm.exe'
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$root = $PSScriptRoot
$validation = Join-Path $root 'validation'
$upstream = Join-Path $root 'build/upstream'
$library = Join-Path $root 'build/library'
$native = Join-Path $root 'build/native'
$esp32 = Join-Path $root 'build/esp32'
New-Item -ItemType Directory -Force $validation, $upstream, (Join-Path $library 'src') | Out-Null
$manifest = Get-Content -LiteralPath (Join-Path $root 'upstream.json') -Raw | ConvertFrom-Json
$steps = [System.Collections.Generic.List[object]]::new()

function Invoke-EvaluationStep {
    param([string]$Name, [string]$Executable, [string[]]$Arguments)
    $output = & $Executable @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    $message = ($output | ForEach-Object { $_.ToString() }) -join "`n"
    $message | Set-Content -LiteralPath (Join-Path $validation "$Name.log") -Encoding utf8
    $steps.Add([ordered]@{ name = $Name; exitCode = $exitCode; log = "$Name.log"; arguments = $Arguments })
    Write-Host "$Name exit=$exitCode"
    return $exitCode
}

foreach ($file in $manifest.files) {
    $destination = Join-Path $upstream $file.local
    if (!(Test-Path -LiteralPath $destination)) {
        Invoke-WebRequest -Uri "https://raw.githubusercontent.com/Disservin/chess-library/$($manifest.commit)/$($file.path)" -OutFile $destination
    }
    $actual = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $file.sha256) { throw "Hash incorreto: $destination" }
}
Copy-Item -LiteralPath (Join-Path $upstream 'chess.hpp') -Destination (Join-Path $library 'src/chess.hpp')
Copy-Item -LiteralPath (Join-Path $upstream 'LICENSE') -Destination (Join-Path $library 'LICENSE')
@'
name=DisservinEvaluation
version=0.9.4
author=disservin
maintainer=Evaluation only
sentence=Unmodified pinned upstream for isolated evaluation.
paragraph=Not integrated with the application firmware.
category=Other
architectures=*
'@ | Set-Content -LiteralPath (Join-Path $library 'library.properties') -Encoding ascii

$cmakeVersion = (& $CMake --version | Out-String).Trim()
$arduinoVersion = (& $ArduinoCli version | Out-String).Trim()
$cores = (& $ArduinoCli core list | Out-String).Trim()
if ($cores -notmatch 'esp32:esp32\s+3\.3\.12\s') { throw 'Esta avaliacao requer Arduino-ESP32 3.3.12 instalado.' }
if ((Invoke-EvaluationStep 'native-configure' $CMake @('-S', $root, '-B', $native, '-G', 'Visual Studio 17 2022', '-A', 'x64')) -ne 0) { throw 'Native configure failed.' }
if ((Invoke-EvaluationStep 'native-build' $CMake @('--build', $native, '--config', 'Release')) -ne 0) { throw 'Native build failed.' }
$nativeExit = Invoke-EvaluationStep 'native-tests' (Join-Path $native 'Release/disservin_tests.exe') @()
$ctest = Join-Path (Split-Path -Parent $CMake) 'ctest.exe'
$ctestExit = Invoke-EvaluationStep 'native-ctest' $ctest @('--test-dir', $native, '-C', 'Release', '--output-on-failure', '--no-tests=error', '--output-junit', (Join-Path $validation 'native-tests.xml'))
$esp32Exit = Invoke-EvaluationStep 'esp32-compile' $ArduinoCli @('compile', '--fqbn', 'esp32:esp32:esp32', '--warnings', 'all', '--library', $library, '--build-path', $esp32, (Join-Path $root 'esp32_probe'))
$isolatedExit = Invoke-EvaluationStep 'esp32-isolated-compile' $ArduinoCli @('compile', '--fqbn', 'esp32:esp32:esp32', '--warnings', 'all', '--library', $library, '--build-path', (Join-Path $root 'build/esp32-isolated'), (Join-Path $root 'esp32_isolated'))
$object = Join-Path $root 'build/esp32-isolated/sketch/probe.cpp.o'
if ((Test-Path -LiteralPath $Esp32Nm) -and (Test-Path -LiteralPath $object)) {
    $symbols = & $Esp32Nm -S --size-sort --radix=d --demangle $object
    if ($LASTEXITCODE -ne 0) { throw 'Falha ao inspecionar simbolos Xtensa.' }
    $symbols | Select-String 'chess::attacks::(RookAttacks|BishopAttacks|RookTable|BishopTable)' |
        ForEach-Object { $_.Line } | Set-Content -LiteralPath (Join-Path $validation 'esp32-table-symbols.log') -Encoding utf8
}
$sourceHashes = [ordered]@{}
foreach ($file in @('CMakeLists.txt', 'tests.cpp', 'esp32_probe/esp32_probe.ino', 'esp32_isolated/esp32_isolated.ino', 'esp32_isolated/probe.cpp', 'avaliar.ps1', 'upstream.json')) {
    $sourceHashes[$file] = (Get-FileHash -LiteralPath (Join-Path $root $file) -Algorithm SHA256).Hash.ToLowerInvariant()
}
[ordered]@{
    performedAtUtc = [DateTime]::UtcNow.ToString('o')
    upstreamCommit = $manifest.commit
    upstreamFiles = $manifest.files
    cmakeVersion = $cmakeVersion
    arduinoCliVersion = $arduinoVersion
    installedCores = $cores
    nativeConfiguration = 'MSVC x64 Release, C++17, /W4 /EHsc; explicit checks remain active in Release'
    esp32Configuration = 'Arduino-ESP32 3.3.12 default C++ flags; no upstream source modification'
    fqbn = 'esp32:esp32:esp32'
    nativeTestsPassed = ($nativeExit -eq 0 -and $ctestExit -eq 0)
    esp32CompilePassed = ($esp32Exit -eq 0)
    esp32IsolatedCompilePassed = ($isolatedExit -eq 0)
    slidingAttackTablesBytes = 861184
    steps = @($steps.ToArray())
    sourceSha256 = $sourceHashes
    uploadPerformed = $false
    physicalTestsPerformed = $false
    notes = 'Isolated compatibility evaluation of unmodified upstream. Failed target compilation is recorded as evidence, not hidden by a source patch.'
} | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $validation 'resultado.json') -Encoding utf8
if ($nativeExit -ne 0 -or $ctestExit -ne 0) { throw 'Native checks failed; see validation logs.' }
