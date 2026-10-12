param(
    [string]$CMake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    [string]$ArduinoCli = 'C:\Program Files\Arduino CLI\arduino-cli.exe'
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$evaluationRoot = $PSScriptRoot
$nativeBuild = Join-Path $evaluationRoot 'build\native'
$probePath = Join-Path $evaluationRoot 'build\esp32_probe'
$esp32Build = Join-Path $evaluationRoot 'build\esp32'
$validationPath = Join-Path $evaluationRoot 'validation'
New-Item -ItemType Directory -Force -Path $validationPath, $probePath | Out-Null
$upstream = Get-Content -Raw (Join-Path $evaluationRoot 'upstream.json') | ConvertFrom-Json
foreach ($entry in $upstream.files.PSObject.Properties) {
    $actual = (Get-FileHash -LiteralPath (Join-Path $evaluationRoot $entry.Name) -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $entry.Value) { throw "Fonte upstream alterada: $($entry.Name)" }
}
$steps = [System.Collections.Generic.List[object]]::new()
function Run-Step {
    param([string]$Name, [string]$Executable, [string[]]$Arguments)
    $output = & $Executable @Arguments 2>&1
    $stepExit = $LASTEXITCODE
    $outputText = ($output | ForEach-Object { $_.ToString() }) -join "`n"
    $outputText | Set-Content -LiteralPath (Join-Path $validationPath "$Name.log") -Encoding utf8
    $steps.Add([ordered]@{name=$Name; exitCode=$stepExit; log="$Name.log"})
    Write-Host $outputText
    if ($stepExit -ne 0) { throw "Falha em $Name ($stepExit)." }
}
$success = $false
$failure = $null
$cmakeVersion = (& $CMake --version | Out-String).Trim()
$cliVersion = (& $ArduinoCli version | Out-String).Trim()
$cores = (& $ArduinoCli core list | Out-String).Trim()
try {
    Run-Step 'native-configure' $CMake @('-S', $evaluationRoot, '-B', $nativeBuild,
        '-G', 'Visual Studio 17 2022', '-A', 'x64')
    Run-Step 'native-build' $CMake @('--build', $nativeBuild, '--config', 'Release')
    $ctest = Join-Path (Split-Path -Parent (Get-Command $CMake).Source) 'ctest.exe'
    Run-Step 'native-tests' $ctest @('--test-dir', $nativeBuild, '-C', 'Release',
        '--output-on-failure', '--no-tests=error', '--output-junit', (Join-Path $validationPath 'native-tests.xml'))
    foreach ($file in @('vendor/src/thc.h', 'vendor/src/thc.cpp', 'tests/essential_tests.h',
            'tests/essential_tests.cpp', 'tests/esp32_probe.ino')) {
        Copy-Item -LiteralPath (Join-Path $evaluationRoot $file) -Destination $probePath -Force
    }
    Run-Step 'esp32-compile' $ArduinoCli @('compile', '--fqbn', 'esp32:esp32:esp32',
        '--warnings', 'all', '--jobs', '2', '--build-property', 'compiler.cpp.extra_flags=-fstack-usage',
        '--build-path', $esp32Build, $probePath)
    $success = $true
} catch {
    $failure = $_.Exception.Message
    throw
} finally {
    $hashes = [ordered]@{}
    foreach ($file in @('upstream.json', 'vendor/src/thc.h', 'vendor/src/thc.cpp', 'vendor/LICENSE',
            'tests/essential_tests.h', 'tests/essential_tests.cpp', 'tests/native_main.cpp',
            'tests/esp32_probe.ino', 'CMakeLists.txt', 'validar.ps1')) {
        $hashes[$file] = (Get-FileHash -LiteralPath (Join-Path $evaluationRoot $file) -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    [ordered]@{
        performedAtUtc=[DateTime]::UtcNow.ToString('o'); success=$success; failure=$failure
        upstreamCommit=$upstream.commit; cmakeVersion=$cmakeVersion; arduinoCliVersion=$cliVersion
        installedCores=$cores; nativeConfiguration='MSVC x64 Release C++14; test harness /W4 /WX; upstream unchanged'
        fqbn='esp32:esp32:esp32'; steps=@($steps.ToArray()); sourceSha256=$hashes
        uploadPerformed=$false; hardwareExecutionPerformed=$false
        scope='Isolated library evaluation. Native functional tests plus ESP32 compile/link only. No MoveTracker or main firmware integration.'
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $validationPath 'resultado.json') -Encoding utf8
}
