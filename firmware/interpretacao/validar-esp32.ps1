param(
    [string]$Port,
    [ValidateRange(10, 3600)][int]$DurationSeconds = 180,
    [string]$ArduinoCli = 'C:\Program Files\Arduino CLI\arduino-cli.exe',
    [switch]$CompileOnly
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$moduleRoot = $PSScriptRoot
$firmwareRoot = Split-Path -Parent $moduleRoot
$chessRoot = Join-Path $firmwareRoot 'xadrez'
$trackerRoot = Join-Path $firmwareRoot 'acompanhamento'
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
    scope = 'MoveInterpreter on ESP32 using MoveTracker and ChessAdapter. Functional tests, heap, stack margin, interpretation latency and bounded stability; no peripherals or firmware integration.'
}
New-Item -ItemType Directory -Force -Path $validationPath, $probePath, $buildPath | Out-Null

function Invoke-ValidationStep {
    param([string]$Name, [string[]]$Arguments)
    $step = [ordered]@{ name = $Name; exitCode = $null; log = "$Name.log"; arguments = $Arguments }
    $steps.Add($step)
    $output = & $ArduinoCli @Arguments 2>&1
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
    foreach ($source in @('interpretacao/src/MoveInterpreter.h', 'interpretacao/src/MoveInterpreter.cpp',
            'interpretacao/tests/interpreter_tests.h', 'interpretacao/tests/interpreter_tests.cpp',
            'interpretacao/tests/esp32_runtime/esp32_runtime.ino', 'interpretacao/tests/esp32_runtime/runtime_probe.cpp',
            'interpretacao/library.properties', 'interpretacao/validar-esp32.ps1',
            'acompanhamento/src/MoveTracker.h', 'acompanhamento/src/MoveTracker.cpp', 'acompanhamento/library.properties',
            'xadrez/src/ChessAdapter.h', 'xadrez/src/ChessAdapter.cpp', 'xadrez/src/thc/thc.h',
            'xadrez/src/thc/thc.cpp', 'xadrez/src/thc/LICENSE', 'xadrez/library.properties', 'xadrez/upstream.json')) {
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
    $result.arduinoCliVersion = Invoke-ValidationStep 'arduino-version' @('version')
    $result.installedCores = Invoke-ValidationStep 'arduino-cores' @('core', 'list')
    foreach ($source in @('tests/esp32_runtime/esp32_runtime.ino', 'tests/esp32_runtime/runtime_probe.cpp',
            'tests/interpreter_tests.h', 'tests/interpreter_tests.cpp')) {
        Copy-Item -LiteralPath (Join-Path $moduleRoot $source) -Destination $probePath -Force
    }
    $result.compileStarted = $true
    Invoke-ValidationStep 'esp32-compile' @(
        'compile', '--fqbn', $result.fqbn, '--warnings', 'all', '--jobs', '2',
        '--library', $moduleRoot, '--library', $trackerRoot, '--library', $chessRoot,
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
        $expectedCases = @('ordinary_frames', 'square_limits', 'capture_orders', 'capture_visibility',
            'capture_ambiguity', 'intermediate_placements', 'castling_orders', 'castling_constraints',
            'en_passant_orders', 'en_passant_constraints', 'promotion_choices', 'promotion_captures',
            'promotion_ambiguity', 'selection_constraints', 'no_move', 'unreliable_input',
            'malformed_history', 'position_mismatch', 'history_capacity', 'discard_stale_frames',
            'timestamps', 'preserves_inputs', 'legal_constraints', 'move_sequence')
        if ($cases.Count -ne 48 -or $caseGroups.Count -ne 24 -or
            @($caseGroups | Where-Object Count -ne 2).Count -ne 0 -or
            @($cases | Where-Object passed -ne $true).Count -ne 0 -or
            @(Compare-Object $expectedCases @($caseGroups.Name)).Count -ne 0) {
            throw 'A sonda nao confirmou os 24 casos aprovados antes e depois da estabilidade.'
        }
        $expectedFixtures = @{
            initial_e2e4 = @(20, 2, 'Identified', 0)
            kiwipete_castle = @(48, 3, 'Identified', 0)
            capture_e4d5 = @(7, 2, 'Identified', 0)
            castle_white_kingside = @(26, 3, 'Identified', 0)
            castle_white_queenside = @(26, 3, 'Identified', 0)
            castle_black_kingside = @(26, 3, 'Identified', 0)
            castle_black_queenside = @(26, 3, 'Identified', 0)
            en_passant_white = @(7, 3, 'Identified', 0)
            en_passant_black = @(7, 3, 'Identified', 0)
            promotion_required = @(9, 2, 'PromotionRequired', 0)
            promotion_queen = @(9, 2, 'Identified', 1)
            promotion_black_queen = @(9, 2, 'Identified', 1)
            promotion_capture_required = @(13, 2, 'PromotionRequired', 0)
            promotion_capture_queen = @(13, 2, 'Identified', 1)
            no_move = @(20, 2, 'NoMove', 0)
            illegal_e2e5 = @(20, 2, 'NoLegalMatch', 0)
            ambiguous_capture = @(13, 4, 'Ambiguous', 0)
            history_64_frames = @(20, 64, 'Identified', 0)
            interrupted_input = @(20, 2, 'UnreliableInput', 0)
            synthetic_high_mobility = @(62, 2, 'Identified', 0)
        }
        if ($benchmarks.Count -ne $expectedFixtures.Count -or $benchmarkGroups.Count -ne $expectedFixtures.Count -or
            @($benchmarks | Where-Object { $_.failures -ne 0 -or $_.samples -ne 200 -or $_.warmups -ne 10 -or
                $_.operation -ne 'interpret' }).Count -ne 0 -or
            @(Compare-Object @($expectedFixtures.Keys) @($benchmarks.fixture)).Count -ne 0) {
            throw 'Benchmarks incompletos ou com falhas.'
        }
        foreach ($benchmark in $benchmarks) {
            $expected = $expectedFixtures[$benchmark.fixture]
            if ($benchmark.legal_moves -ne $expected[0] -or $benchmark.frames -ne $expected[1] -or
                $benchmark.expected_status -ne $expected[2] -or $benchmark.selection -ne $expected[3]) {
                throw "Metadados divergentes no benchmark: $($benchmark.fixture)"
            }
        }
        $begin = @($records | Where-Object type -eq 'begin')
        $progress = @($records | Where-Object type -eq 'progress')
        if ($begin.Count -ne 1 -or $begin[0].fixture_count -ne $expectedFixtures.Count -or
            $begin[0].loop_watchdog_active -ne $true -or
            @($progress | Where-Object failures -ne 0).Count -ne 0) {
            throw 'Identificacao da sonda, watchdog ou checkpoints nao confirmados.'
        }
        if ($stability.Count -ne 1 -or $stability[0].failures -ne 0 -or
            $stability[0].elapsed_ms -lt ($DurationSeconds * 1000) -or $stability[0].batches -lt 1 -or
            $stability[0].operations -ne ($stability[0].batches * $expectedFixtures.Count) -or
            $stability[0].additional_suites -ne $progress.Count -or
            $progress.Count -lt ([Math]::Floor($DurationSeconds / 10) - 1)) {
            throw 'Ensaio de estabilidade incompleto ou com falhas.'
        }
        if ($end.Count -ne 1 -or $end[0].failures -ne 0 -or $end[0].reported_cases -ne 48 -or
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
