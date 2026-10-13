param(
    [string]$CMake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    [string]$ArduinoCli = 'C:\Program Files\Arduino CLI\arduino-cli.exe',
    [switch]$NativeOnly
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$moduleRoot = $PSScriptRoot
$firmwareRoot = Split-Path -Parent $moduleRoot
$chessRoot = Join-Path $firmwareRoot 'xadrez'
$trackerRoot = Join-Path $firmwareRoot 'acompanhamento'
$interpreterRoot = Join-Path $firmwareRoot 'interpretacao'
$nativeBuild = Join-Path $moduleRoot 'build/native'
$esp32Build = Join-Path $moduleRoot 'build/esp32'
$probePath = Join-Path $moduleRoot 'build/esp32_compile'
$validationPath = Join-Path $moduleRoot 'validation'
$steps = [System.Collections.Generic.List[object]]::new()
$success = $false
$failure = $null
$cmakeVersion = $null
$cliVersion = $null
$cores = $null
$esp32Compiled = $false
New-Item -ItemType Directory -Force -Path $validationPath, $probePath | Out-Null
$upstream = Get-Content -Raw (Join-Path $chessRoot 'upstream.json') | ConvertFrom-Json

function Invoke-ValidationStep {
    param([string]$Name, [string]$Executable, [string[]]$Arguments)
    $output = & $Executable @Arguments 2>&1
    $stepExit = $LASTEXITCODE
    $stepText = ($output | ForEach-Object { $_.ToString() }) -join "`n"
    $stepText | Set-Content -LiteralPath (Join-Path $validationPath "$Name.log") -Encoding utf8
    $steps.Add([ordered]@{ name = $Name; exitCode = $stepExit; log = "$Name.log"; arguments = $Arguments })
    Write-Host $stepText
    if ($stepExit -ne 0) { throw "Etapa $Name falhou com codigo $stepExit." }
}

try {
    foreach ($entry in $upstream.files.PSObject.Properties) {
        $actual = (Get-FileHash -LiteralPath (Join-Path $chessRoot $entry.Name) -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne $entry.Value) { throw "Fonte THC alterada: $($entry.Name)" }
    }
    $cmakeExe = (Get-Command $CMake -ErrorAction Stop).Source
    $ctestExe = Join-Path (Split-Path -Parent $cmakeExe) 'ctest.exe'
    $cmakeVersion = (& $cmakeExe --version | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Falha ao consultar CMake.' }

    Invoke-ValidationStep 'native-configure' $cmakeExe @(
        '-S', $moduleRoot, '-B', $nativeBuild, '-G', 'Visual Studio 17 2022', '-A', 'x64')
    Invoke-ValidationStep 'native-build' $cmakeExe @('--build', $nativeBuild, '--config', 'Release')
    Invoke-ValidationStep 'native-tests' $ctestExe @(
        '--test-dir', $nativeBuild, '-C', 'Release', '--output-on-failure', '--no-tests=error',
        '--output-junit', (Join-Path $validationPath 'native-tests.xml'))

    if (!$NativeOnly) {
        $cliVersion = (& $ArduinoCli version | Out-String).Trim()
        if ($LASTEXITCODE -ne 0) { throw 'Falha ao consultar Arduino CLI.' }
        $cores = (& $ArduinoCli core list | Out-String).Trim()
        if ($LASTEXITCODE -ne 0) { throw 'Falha ao consultar os cores Arduino.' }
        foreach ($file in @('tests/esp32_compile/esp32_compile.ino', 'tests/controller_tests.h', 'tests/controller_tests.cpp')) {
            Copy-Item -LiteralPath (Join-Path $moduleRoot $file) -Destination $probePath -Force
        }
        Invoke-ValidationStep 'esp32-compile' $ArduinoCli @(
            'compile', '--fqbn', 'esp32:esp32:esp32', '--warnings', 'all', '--jobs', '2',
            '--build-property', 'compiler.cpp.extra_flags=-fstack-usage',
            '--library', $moduleRoot, '--library', $interpreterRoot,
            '--library', $trackerRoot, '--library', $chessRoot,
            '--build-path', $esp32Build, $probePath)
        $esp32Compiled = $true
    }
    $success = $true
} catch {
    $failure = $_.Exception.Message
    throw
} finally {
    $sourceHashes = [ordered]@{}
    $sources = @(
        'controle/src/GameController.h', 'controle/src/GameController.cpp',
        'controle/tests/controller_tests.h', 'controle/tests/controller_tests.cpp',
        'controle/tests/native_main.cpp', 'controle/tests/esp32_compile/esp32_compile.ino',
        'controle/CMakeLists.txt', 'controle/library.properties', 'controle/validar.ps1',
        'interpretacao/src/MoveInterpreter.h', 'interpretacao/src/MoveInterpreter.cpp',
        'interpretacao/tests/interpreter_tests.h', 'interpretacao/tests/interpreter_tests.cpp',
        'interpretacao/tests/native_main.cpp', 'interpretacao/CMakeLists.txt', 'interpretacao/library.properties',
        'acompanhamento/src/MoveTracker.h', 'acompanhamento/src/MoveTracker.cpp',
        'acompanhamento/tests/test_move_tracker.cpp', 'acompanhamento/CMakeLists.txt',
        'acompanhamento/library.properties', 'xadrez/src/ChessAdapter.h', 'xadrez/src/ChessAdapter.cpp',
        'xadrez/src/thc/thc.h', 'xadrez/src/thc/thc.cpp', 'xadrez/src/thc/LICENSE', 'xadrez/upstream.json',
        'xadrez/tests/adapter_tests.h', 'xadrez/tests/adapter_tests.cpp', 'xadrez/tests/native_main.cpp',
        'xadrez/CMakeLists.txt', 'xadrez/library.properties')
    foreach ($source in $sources) {
        $sourceHashes[$source] = (Get-FileHash -LiteralPath (Join-Path $firmwareRoot $source) -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    [ordered]@{
        performedAtUtc = [DateTime]::UtcNow.ToString('o')
        success = $success; failure = $failure; nativeOnly = [bool]$NativeOnly
        upstreamCommit = $upstream.commit
        cmakeVersion = $cmakeVersion; arduinoCliVersion = $cliVersion; installedCores = $cores
        nativeConfiguration = 'MSVC x64 Release C++14; controller and tests /W4 /WX /permissive-; dependency regression suites included'
        fqbn = 'esp32:esp32:esp32'; steps = @($steps.ToArray()); sourceSha256 = $sourceHashes
        esp32CompileSucceeded = $esp32Compiled
        uploadPerformed = $false; hardwareExecutionPerformed = $false
        scope = 'Hardware-independent game control. Native contract/regression tests and optional ESP32 compile/link. No UI, sensors or PGN.'
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $validationPath 'resultado.json') -Encoding utf8
}
