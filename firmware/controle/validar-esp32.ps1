param(
    [string]$Port,
    [ValidateRange(10, 3600)][int]$DurationSeconds = 180,
    [string]$ArduinoCli = 'C:\Program Files\Arduino CLI\arduino-cli.exe',
    [string]$CMake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    [switch]$CompileOnly
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$moduleRoot = $PSScriptRoot
$firmwareRoot = Split-Path -Parent $moduleRoot
$chessRoot = Join-Path $firmwareRoot 'xadrez'
$trackerRoot = Join-Path $firmwareRoot 'acompanhamento'
$interpreterRoot = Join-Path $firmwareRoot 'interpretacao'
$nativeBuild = Join-Path $moduleRoot 'build/native'
$probePath = Join-Path $moduleRoot 'build/esp32_runtime'
$buildPath = Join-Path $moduleRoot 'build/esp32-runtime-output'
$startedAt = [DateTime]::UtcNow
$validationPath = Join-Path $moduleRoot ('validation/hardware/' + $startedAt.ToString('yyyyMMddTHHmmssfffffffZ'))
$steps = [System.Collections.Generic.List[object]]::new()
$records = [System.Collections.Generic.List[object]]::new()
$serial = $null
$serialLog = $null
$sourceHashes = [ordered]@{}
$binaryHashes = [ordered]@{}
$result = [ordered]@{
    startedAtUtc = $startedAt.ToString('o')
    finishedAtUtc = $null
    success = $false
    failure = $null
    compileOnly = $CompileOnly.IsPresent
    compileStarted = $false
    compileSucceeded = $false
    uploadStarted = $false
    uploadSucceeded = $false
    uploadPerformed = $false
    hardwareExecutionStarted = $false
    hardwareExecutionPerformed = $false
    hardwareExecutionCompleted = $false
    nativeTestsSucceeded = $false
    cmakeVersion = $null
    fqbn = 'esp32:esp32:esp32'
    port = $Port
    baudRate = 115200
    requestedDurationSeconds = $DurationSeconds
    runId = [Guid]::NewGuid().ToString('N')
    commandSentAtUtc = $null
    upstreamCommit = $null
    arduinoCliVersion = $null
    installedCores = $null
    sourceSha256 = $sourceHashes
    binarySha256 = $binaryHashes
    steps = @()
    records = @()
    scope = 'GameController on ESP32 using simulated observations. Native regression and embedded functional tests, heap, stack margin, confirmation/full-flow latency and bounded stability; no peripherals or integrated firmware.'
}
New-Item -ItemType Directory -Force -Path $validationPath, $probePath, $buildPath | Out-Null

function Invoke-ValidationStep {
    param([string]$Name, [string[]]$Arguments, [string]$Executable = $ArduinoCli)
    $step = [ordered]@{ name = $Name; executable = $Executable; exitCode = $null; log = "$Name.log"; arguments = $Arguments }
    $steps.Add($step)
    $output = & $Executable @Arguments 2>&1
    $step.exitCode = $LASTEXITCODE
    $stepText = ($output | ForEach-Object { $_.ToString() }) -join "`n"
    $stepText | Set-Content -LiteralPath (Join-Path $validationPath "$Name.log") -Encoding utf8
    Write-Host $stepText
    if ($step.exitCode -ne 0) { throw "Etapa $Name falhou com codigo $($step.exitCode)." }
    return $stepText
}

try {
    if (!$CompileOnly -and [string]::IsNullOrWhiteSpace($Port)) {
        throw 'Informe -Port com a porta da placa ou use -CompileOnly para apenas compilar.'
    }
    foreach ($source in @('controle/src/GameController.h', 'controle/src/GameController.cpp',
            'controle/tests/controller_tests.h', 'controle/tests/controller_tests.cpp', 'controle/tests/native_main.cpp',
            'controle/tests/esp32_runtime/esp32_runtime.ino', 'controle/tests/esp32_runtime/runtime_probe.cpp',
            'controle/library.properties', 'controle/CMakeLists.txt', 'controle/validar-esp32.ps1',
            'interpretacao/src/MoveInterpreter.h', 'interpretacao/src/MoveInterpreter.cpp',
            'interpretacao/tests/interpreter_tests.h', 'interpretacao/tests/interpreter_tests.cpp',
            'interpretacao/tests/native_main.cpp', 'interpretacao/CMakeLists.txt', 'interpretacao/library.properties',
            'acompanhamento/src/MoveTracker.h', 'acompanhamento/src/MoveTracker.cpp', 'acompanhamento/library.properties',
            'acompanhamento/tests/test_move_tracker.cpp', 'acompanhamento/CMakeLists.txt',
            'xadrez/src/ChessAdapter.h', 'xadrez/src/ChessAdapter.cpp', 'xadrez/src/thc/thc.h',
            'xadrez/src/thc/thc.cpp', 'xadrez/src/thc/LICENSE', 'xadrez/library.properties', 'xadrez/upstream.json',
            'xadrez/tests/adapter_tests.h', 'xadrez/tests/adapter_tests.cpp', 'xadrez/tests/native_main.cpp', 'xadrez/CMakeLists.txt')) {
        $sourceHashes[$source] = (Get-FileHash -LiteralPath (Join-Path $firmwareRoot $source) -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $validationPath 'collector-used.ps1')
    Copy-Item -LiteralPath (Join-Path $moduleRoot 'tests/esp32_runtime/runtime_probe.cpp') -Destination $validationPath
    Copy-Item -LiteralPath (Join-Path $moduleRoot 'tests/esp32_runtime/esp32_runtime.ino') -Destination $validationPath
    $upstream = Get-Content -Raw (Join-Path $chessRoot 'upstream.json') | ConvertFrom-Json
    $result.upstreamCommit = $upstream.commit
    foreach ($entry in $upstream.files.PSObject.Properties) {
        if ($sourceHashes['xadrez/' + $entry.Name] -ne $entry.Value) { throw "Fonte THC alterada: $($entry.Name)" }
    }
    $cmakeExe = (Get-Command $CMake -ErrorAction Stop).Source
    $ctestExe = Join-Path (Split-Path -Parent $cmakeExe) 'ctest.exe'
    $result.cmakeVersion = Invoke-ValidationStep 'cmake-version' @('--version') $cmakeExe
    Invoke-ValidationStep 'native-configure' @(
        '-S', $moduleRoot, '-B', $nativeBuild, '-G', 'Visual Studio 17 2022', '-A', 'x64') $cmakeExe | Out-Null
    Invoke-ValidationStep 'native-build' @('--build', $nativeBuild, '--config', 'Release') $cmakeExe | Out-Null
    Invoke-ValidationStep 'native-tests' @(
        '--test-dir', $nativeBuild, '-C', 'Release', '--output-on-failure', '--no-tests=error',
        '--output-junit', (Join-Path $validationPath 'native-tests.xml')) $ctestExe | Out-Null
    $result.nativeTestsSucceeded = $true
    $result.arduinoCliVersion = Invoke-ValidationStep 'arduino-version' @('version')
    $result.installedCores = Invoke-ValidationStep 'arduino-cores' @('core', 'list')
    Invoke-ValidationStep 'serial-ports' @('board', 'list', '--format', 'json') | Out-Null
    foreach ($source in @('tests/esp32_runtime/esp32_runtime.ino', 'tests/esp32_runtime/runtime_probe.cpp',
            'tests/controller_tests.h', 'tests/controller_tests.cpp')) {
        Copy-Item -LiteralPath (Join-Path $moduleRoot $source) -Destination $probePath -Force
    }
    $result.compileStarted = $true
    Invoke-ValidationStep 'esp32-compile' @(
        'compile', '--fqbn', $result.fqbn, '--warnings', 'all', '--jobs', '2',
        '--library', $moduleRoot, '--library', $interpreterRoot, '--library', $trackerRoot, '--library', $chessRoot,
        '--build-path', $buildPath, $probePath) | Out-Null
    $result.compileSucceeded = $true
    # esptool rewrites this cache during upload; it is not a compiled image.
    foreach ($binary in Get-ChildItem -LiteralPath $buildPath -Filter '*.bin' -File | Where-Object Name -NotLike '*_flashed.bin') {
        $binaryHashes[$binary.Name] = (Get-FileHash -LiteralPath $binary.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    if (!$binaryHashes.Contains('esp32_runtime.ino.bin')) { throw 'Binario principal da sonda nao foi encontrado.' }

    if (!$CompileOnly) {
        $result.uploadStarted = $true
        Invoke-ValidationStep 'esp32-upload' @(
            'upload', '--fqbn', $result.fqbn, '-p', $Port, '--input-dir', $buildPath) | Out-Null
        $result.uploadSucceeded = $true
        $result.uploadPerformed = $true
        $serialLog = [System.IO.StreamWriter]::new(
            (Join-Path $validationPath 'serial.log'), $false, [System.Text.UTF8Encoding]::new($false))
        $serialLog.AutoFlush = $true
        $serial = [System.IO.Ports.SerialPort]::new($Port, 115200)
        $serial.DtrEnable = $false
        $serial.RtsEnable = $false
        $serial.ReadTimeout = 500
        $serial.WriteTimeout = 2000
        $serial.NewLine = "`n"
        $serial.Encoding = [System.Text.UTF8Encoding]::new($false)
        $serial.Open()
        $deadline = [DateTime]::UtcNow.AddSeconds($DurationSeconds + 120)
        $buffer = [char[]]::new(4096)
        $pending = ''
        $commandSent = $false
        while (!$result.hardwareExecutionCompleted) {
            if ([DateTime]::UtcNow -ge $deadline) { throw 'Tempo limite de captura excedido sem conclusao da sonda.' }
            try { $count = $serial.Read($buffer, 0, $buffer.Length) }
            catch [System.TimeoutException] { continue }
            $chunk = [string]::new($buffer, 0, $count)
            $serialLog.Write($chunk)
            $pending += $chunk
            while (($newline = $pending.IndexOf("`n")) -ge 0) {
                $line = $pending.Substring(0, $newline).TrimEnd("`r")
                $pending = $pending.Substring($newline + 1)
                Write-Host $line
                if ($line -match 'Guru Meditation|Backtrace:|[Ss]tack canary|stack smashing|abort\(\) was called|assert failed|Brownout detector|Task watchdog|task_wdt:|Interrupt wdt timeout') {
                    throw "Falha de execucao detectada na serial: $line"
                }
                if ($result.hardwareExecutionStarted -and $line -match 'rst:0x|ets Jun') {
                    throw "Reinicio detectado durante a execucao: $line"
                }
                if (!$line.StartsWith('@')) { continue }
                $record = $line.Substring(1) | ConvertFrom-Json -ErrorAction Stop
                $records.Add($record)
                switch ($record.type) {
                    'ready' {
                        if ($result.hardwareExecutionStarted) { throw 'Sonda voltou ao estado ready antes de concluir; possivel reinicio.' }
                        if ($record.protocol -ne 1) { throw "Protocolo serial desconhecido: $($record.protocol)" }
                        if (!$commandSent) {
                            $serial.WriteLine("RUN $($result.runId) $DurationSeconds")
                            $result.commandSentAtUtc = [DateTime]::UtcNow.ToString('o')
                            $commandSent = $true
                        }
                    }
                    'begin' {
                        if (!$commandSent -or $result.hardwareExecutionStarted -or $record.id -ne $result.runId) {
                            throw 'Registro begin inesperado ou com identificador divergente.'
                        }
                        $result.hardwareExecutionStarted = $true
                        $result.hardwareExecutionPerformed = $true
                    }
                    'end' {
                        if (!$result.hardwareExecutionStarted -or $record.id -ne $result.runId) {
                            throw 'Registro end sem inicio correspondente.'
                        }
                        $result.hardwareExecutionCompleted = $true
                    }
                    default {
                        if (!$result.hardwareExecutionStarted) { throw "Registro $($record.type) recebido antes de begin." }
                    }
                }
                if ($result.hardwareExecutionCompleted) { break }
            }
        }

        $cases = @($records | Where-Object type -eq 'case')
        $benchmarks = @($records | Where-Object type -eq 'benchmark')
        $stability = @($records | Where-Object type -eq 'stability')
        $end = @($records | Where-Object type -eq 'end')
        $memory = @($records | Where-Object type -eq 'memory')
        $caseGroups = @($cases | Group-Object name)
        $benchmarkGroups = @($benchmarks | Group-Object fixture, operation)
        $expectedCases = @('startup_baseline', 'position_gate', 'confirmation_only', 'duplicate_confirmation',
            'move_sequence', 'captures', 'capture_visibility', 'castling', 'en_passant', 'illegal_moves',
            'no_move', 'ambiguous', 'interruption', 'overflow', 'promotion_choices', 'promotion_captures',
            'promotion_selection', 'promotion_observations', 'promotion_interruption', 'reset', 'copy_history',
            'repetition', 'fifty_move', 'checkmate', 'stalemate', 'insufficient_material', 'manual_finish',
            'resignation', 'finished_immutable')
        if ($cases.Count -ne (2 * $expectedCases.Count) -or $caseGroups.Count -ne $expectedCases.Count -or
            @($caseGroups | Where-Object Count -ne 2).Count -ne 0 -or
            @($cases | Where-Object passed -ne $true).Count -ne 0 -or
            @(Compare-Object $expectedCases @($caseGroups.Name)).Count -ne 0) {
            throw 'A sonda nao confirmou os 29 casos aprovados antes e depois da estabilidade.'
        }
        $expectedFixtures = @{
            confirm_initial_e2e4 = @(20, 2, 'Accepted', 0, 'Tracking', 'None', 'Ongoing')
            confirm_kiwipete_castle = @(48, 3, 'Accepted', 0, 'Tracking', 'None', 'Ongoing')
            confirm_high_mobility = @(62, 2, 'Accepted', 0, 'Tracking', 'None', 'Ongoing')
            confirm_capture_e4d5 = @(7, 2, 'Accepted', 0, 'Tracking', 'None', 'Ongoing')
            confirm_castle_white = @(26, 3, 'Accepted', 0, 'Tracking', 'None', 'Ongoing')
            confirm_en_passant_white = @(7, 3, 'Accepted', 0, 'Tracking', 'None', 'Ongoing')
            confirm_promotion_required = @(9, 2, 'PromotionRequired', 0, 'AwaitingPromotion', 'None', 'Ongoing')
            confirm_promotion_queen = @(9, 2, 'Accepted', 1, 'Tracking', 'None', 'Ongoing')
            confirm_promotion_knight = @(9, 2, 'Accepted', 4, 'Finished', 'InsufficientMaterial', 'Draw')
            confirm_ambiguous_capture = @(13, 4, 'Ambiguous', 0, 'AwaitingPosition', 'None', 'Ongoing')
            confirm_history_64_frames = @(20, 64, 'Accepted', 0, 'Tracking', 'None', 'Ongoing')
            confirm_duplicate_confirmation = @(20, 0, 'NoMove', 0, 'Tracking', 'None', 'Ongoing')
            confirm_checkmate = @(30, 2, 'Accepted', 0, 'Finished', 'Checkmate', 'BlackWin')
            confirm_stalemate = @(29, 2, 'Accepted', 0, 'Finished', 'Stalemate', 'Draw')
            confirm_insufficient_material = @(7, 2, 'Accepted', 0, 'Finished', 'InsufficientMaterial', 'Draw')
            flow_initial_e2e4 = @(20, 2, 'Accepted', 0, 'Tracking', 'None', 'Ongoing')
            flow_capture_e4d5 = @(7, 2, 'Accepted', 0, 'Tracking', 'None', 'Ongoing')
            flow_castle_white = @(26, 3, 'Accepted', 0, 'Tracking', 'None', 'Ongoing')
            flow_en_passant_white = @(7, 3, 'Accepted', 0, 'Tracking', 'None', 'Ongoing')
            flow_promotion_queen = @(9, 2, 'Accepted', 1, 'Tracking', 'None', 'Ongoing')
            flow_promotion_knight = @(9, 2, 'Accepted', 4, 'Finished', 'InsufficientMaterial', 'Draw')
            flow_promotion_reselection = @(9, 2, 'Accepted', 4, 'Finished', 'InsufficientMaterial', 'Draw')
            flow_illegal_rearm = @(20, 2, 'Accepted', 0, 'Tracking', 'None', 'Ongoing')
            flow_interruption_rearm = @(20, 2, 'Accepted', 0, 'Tracking', 'None', 'Ongoing')
            flow_manual_pending = @(9, 2, 'GameFinished', 1, 'Finished', 'Manual', 'Unfinished')
            flow_resignation_pending = @(9, 2, 'GameFinished', 1, 'Finished', 'Resignation', 'BlackWin')
        }
        if ($benchmarks.Count -ne $expectedFixtures.Count -or $benchmarkGroups.Count -ne $expectedFixtures.Count -or
            @($benchmarks | Where-Object { $_.failures -ne 0 -or $_.samples -ne 200 -or $_.warmups -ne 10 -or
                $_.operation -notin @('confirm', 'flow') -or $_.min_us -lt 0 -or
                $_.median_us -lt $_.min_us -or $_.max_us -lt $_.median_us -or
                $_.mean_us -lt $_.min_us -or $_.mean_us -gt $_.max_us -or
                $_.stack_min_free_bytes -le 0 -or $_.heap_min_free_bytes -le 0 }).Count -ne 0 -or
            @(Compare-Object @($expectedFixtures.Keys) @($benchmarks.fixture)).Count -ne 0) {
            throw 'Benchmarks incompletos ou com falhas.'
        }
        foreach ($benchmark in $benchmarks) {
            $expected = $expectedFixtures[$benchmark.fixture]
            $operation = ($benchmark.fixture -split '_')[0]
            $scope = if ($operation -eq 'confirm') { 'confirm_only' } else { 'observations_commands_scalar_trace' }
            if ($benchmark.legal_moves -ne $expected[0] -or $benchmark.frames -ne $expected[1] -or
                $benchmark.expected_status -ne $expected[2] -or $benchmark.selection -ne $expected[3] -or
                $benchmark.expected_state -ne $expected[4] -or $benchmark.expected_reason -ne $expected[5] -or
                $benchmark.expected_result -ne $expected[6] -or $benchmark.operation -ne $operation -or
                $benchmark.timed_scope -ne $scope) {
                throw "Metadados divergentes no benchmark: $($benchmark.fixture)"
            }
        }
        $begin = @($records | Where-Object type -eq 'begin')
        $progress = @($records | Where-Object type -eq 'progress')
        if ($begin.Count -ne 1 -or $begin[0].fixture_count -ne $expectedFixtures.Count -or
            $begin[0].suite_cases -ne $expectedCases.Count -or $begin[0].stack_allocated_bytes -ne 49152 -or
            $begin[0].loop_watchdog_active -ne $true -or
            @($progress | Where-Object failures -ne 0).Count -ne 0) {
            throw 'Identificacao da sonda, watchdog ou checkpoints nao confirmados.'
        }
        if ($stability.Count -ne 1 -or $stability[0].failures -ne 0 -or
            $stability[0].operation_unit -ne 'fixture_runs' -or
            $stability[0].elapsed_ms -lt ($DurationSeconds * 1000) -or $stability[0].batches -lt 1 -or
            $stability[0].operations -ne ($stability[0].batches * $expectedFixtures.Count) -or
            $stability[0].additional_suites -ne $progress.Count -or
            $progress.Count -lt ([Math]::Floor($DurationSeconds / 10) - 1)) {
            throw 'Ensaio de estabilidade incompleto ou com falhas.'
        }
        if ($end.Count -ne 1 -or $end[0].failures -ne 0 -or $end[0].reported_cases -ne (2 * $expectedCases.Count) -or
            $memory.Count -lt 5 -or @($memory | Where-Object heap_intact -ne $true).Count -ne 0) {
            throw 'Conclusao da sonda ou integridade do heap nao confirmada.'
        }
        foreach ($entry in $sourceHashes.GetEnumerator()) {
            $actual = (Get-FileHash -LiteralPath (Join-Path $firmwareRoot $entry.Key) -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($actual -ne $entry.Value) { throw "Fonte mudou durante o ensaio: $($entry.Key)" }
        }
        foreach ($entry in $binaryHashes.GetEnumerator()) {
            $actual = (Get-FileHash -LiteralPath (Join-Path $buildPath $entry.Key) -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($actual -ne $entry.Value) { throw "Imagem compilada mudou durante o ensaio: $($entry.Key)" }
        }
        $result.success = $true
    }
} catch {
    $result.failure = $_.Exception.Message
    throw
} finally {
    try { if ($null -ne $serial) { $serial.Dispose() } }
    catch { Write-Warning "Falha ao fechar a porta serial: $($_.Exception.Message)" }
    try { if ($null -ne $serialLog) { $serialLog.Dispose() } }
    catch { Write-Warning "Falha ao fechar o log serial: $($_.Exception.Message)" }
    $result.finishedAtUtc = [DateTime]::UtcNow.ToString('o')
    $result.steps = @($steps.ToArray())
    $result.records = @($records.ToArray())
    $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $validationPath 'resultado.json') -Encoding utf8
    Write-Host "Evidencias: $validationPath"
}
Write-Output $validationPath
