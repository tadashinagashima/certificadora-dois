param(
    [string]$CMake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    [string]$ArduinoCli = 'C:\Program Files\Arduino CLI\arduino-cli.exe'
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$moduleRoot = $PSScriptRoot
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
New-Item -ItemType Directory -Force -Path $validationPath, $probePath | Out-Null
$upstream = Get-Content -Raw (Join-Path $moduleRoot 'upstream.json') | ConvertFrom-Json

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
        $actual = (Get-FileHash -LiteralPath (Join-Path $moduleRoot $entry.Name) -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne $entry.Value) { throw "Fonte THC alterada: $($entry.Name)" }
    }
    $cmakeExe = (Get-Command $CMake -ErrorAction Stop).Source
    $ctestExe = Join-Path (Split-Path -Parent $cmakeExe) 'ctest.exe'
    $cmakeVersion = (& $cmakeExe --version | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Falha ao consultar CMake.' }
    $cliVersion = (& $ArduinoCli version | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Falha ao consultar Arduino CLI.' }
    $cores = (& $ArduinoCli core list | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Falha ao consultar os cores Arduino.' }

    Invoke-ValidationStep 'native-configure' $cmakeExe @(
        '-S', $moduleRoot, '-B', $nativeBuild, '-G', 'Visual Studio 17 2022', '-A', 'x64')
    Invoke-ValidationStep 'native-build' $cmakeExe @('--build', $nativeBuild, '--config', 'Release')
    Invoke-ValidationStep 'native-tests' $ctestExe @(
        '--test-dir', $nativeBuild, '-C', 'Release', '--output-on-failure', '--no-tests=error',
        '--output-junit', (Join-Path $validationPath 'native-tests.xml'))

    foreach ($file in @('tests/esp32_compile/esp32_compile.ino', 'tests/adapter_tests.h', 'tests/adapter_tests.cpp')) {
        Copy-Item -LiteralPath (Join-Path $moduleRoot $file) -Destination $probePath -Force
    }
    Invoke-ValidationStep 'esp32-compile' $ArduinoCli @(
        'compile', '--fqbn', 'esp32:esp32:esp32', '--warnings', 'all', '--jobs', '2',
        '--build-property', 'compiler.cpp.extra_flags=-fstack-usage',
        '--library', $moduleRoot, '--build-path', $esp32Build, $probePath)
    $success = $true
} catch {
    $failure = $_.Exception.Message
    throw
} finally {
    $sourceHashes = [ordered]@{}
    foreach ($source in @('src/ChessAdapter.h', 'src/ChessAdapter.cpp', 'src/thc/thc.h', 'src/thc/thc.cpp',
            'src/thc/LICENSE', 'tests/adapter_tests.h', 'tests/adapter_tests.cpp', 'tests/native_main.cpp',
            'tests/esp32_compile/esp32_compile.ino', 'CMakeLists.txt', 'library.properties', 'upstream.json', 'validar.ps1')) {
        $sourceHashes[$source] = (Get-FileHash -LiteralPath (Join-Path $moduleRoot $source) -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    [ordered]@{
        performedAtUtc = [DateTime]::UtcNow.ToString('o')
        success = $success; failure = $failure
        upstreamCommit = $upstream.commit
        cmakeVersion = $cmakeVersion; arduinoCliVersion = $cliVersion; installedCores = $cores
        nativeConfiguration = 'MSVC x64 Release C++14; adapter and tests /W4 /WX; upstream unchanged'
        fqbn = 'esp32:esp32:esp32'; steps = @($steps.ToArray()); sourceSha256 = $sourceHashes
        uploadPerformed = $false; hardwareExecutionPerformed = $false
        scope = 'THC adapter only. Native contract tests and ESP32 compile/link. No interpretation, game control or PGN.'
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $validationPath 'resultado.json') -Encoding utf8
}
