param(
    [string]$CMake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    [string]$ArduinoCli = 'C:\Program Files\Arduino CLI\arduino-cli.exe'
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$moduleRoot = $PSScriptRoot
$nativeBuild = Join-Path $moduleRoot 'build\native'
$esp32Build = Join-Path $moduleRoot 'build\esp32'
$validationPath = Join-Path $moduleRoot 'validation'
$steps = [System.Collections.Generic.List[object]]::new()
$success = $false
$failure = $null
$cmakeVersion = $null
$cliVersion = $null
$cores = $null
New-Item -ItemType Directory -Force -Path $validationPath | Out-Null

function Invoke-ValidationStep {
    param([string]$Name, [string]$Executable, [string[]]$Arguments)
    $output = & $Executable @Arguments 2>&1
    $stepExit = $LASTEXITCODE
    $stepText = ($output | ForEach-Object { $_.ToString() }) -join "`n"
    $stepText | Set-Content -LiteralPath (Join-Path $validationPath "$Name.log") -Encoding utf8
    $steps.Add([ordered]@{ name = $Name; exitCode = $stepExit; log = "$Name.log" })
    Write-Host $stepText
    if ($stepExit -ne 0) { throw "Etapa $Name falhou com codigo $stepExit." }
}

try {
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
    Invoke-ValidationStep 'native-build' $cmakeExe @(
        '--build', $nativeBuild, '--config', 'Release')
    Invoke-ValidationStep 'native-tests' $ctestExe @(
        '--test-dir', $nativeBuild, '-C', 'Release', '--output-on-failure', '--no-tests=error',
        '--output-junit', (Join-Path $validationPath 'native-tests.xml'))
    Invoke-ValidationStep 'esp32-compile' $ArduinoCli @(
        'compile', '--fqbn', 'esp32:esp32:esp32', '--warnings', 'all',
        '--build-property', 'compiler.cpp.extra_flags=-Werror',
        '--library', $moduleRoot, '--build-path', $esp32Build,
        (Join-Path $moduleRoot 'tests\esp32_compile'))
    $success = $true
} catch {
    $failure = $_.Exception.Message
    throw
} finally {
    $sourceHashes = [ordered]@{}
    foreach ($source in @('src/MoveTracker.h', 'src/MoveTracker.cpp',
            'tests/test_move_tracker.cpp', 'tests/esp32_compile/esp32_compile.ino',
            'CMakeLists.txt', 'library.properties', 'validar.ps1')) {
        $sourceHashes[$source] = (Get-FileHash -LiteralPath (Join-Path $moduleRoot $source) -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    [ordered]@{
        performedAtUtc = [DateTime]::UtcNow.ToString('o')
        success = $success
        failure = $failure
        cmakeVersion = $cmakeVersion
        arduinoCliVersion = $cliVersion
        installedCores = $cores
        nativeConfiguration = 'MSVC x64 Release, C++14, /W4 /WX'
        fqbn = 'esp32:esp32:esp32'
        steps = @($steps.ToArray())
        sourceSha256 = $sourceHashes
        uploadPerformed = $false
        physicalTestsPerformed = $false
        notes = 'Native tests use synthetic observations. ESP32 compilation only; no sensor acquisition, chess interpretation or physical validation.'
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $validationPath 'resultado.json') -Encoding utf8
}
