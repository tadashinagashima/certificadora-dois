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
$controllerRoot = Join-Path $firmwareRoot 'controle'
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
    scope = 'GameRecord on ESP32. Embedded functional tests, SAN/record/PGN/finish latency, heap, stack and prolonged games through GameController with simulated observations. No SD, sensors or integrated firmware.'
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
    foreach ($source in @('registro/src/GameRecord.h', 'registro/src/GameRecord.cpp',
            'registro/tests/record_tests.h', 'registro/tests/record_tests.cpp', 'registro/tests/native_main.cpp',
            'registro/tests/esp32_runtime/esp32_runtime.ino', 'registro/tests/esp32_runtime/runtime_probe.cpp',
            'registro/library.properties', 'registro/CMakeLists.txt', 'registro/validar-esp32.ps1',
            'controle/src/GameController.h', 'controle/src/GameController.cpp',
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
            'tests/record_tests.h', 'tests/record_tests.cpp')) {
        Copy-Item -LiteralPath (Join-Path $moduleRoot $source) -Destination $probePath -Force
    }
    $result.compileStarted = $true
    Invoke-ValidationStep 'esp32-compile' @(
        'compile', '--fqbn', $result.fqbn, '--warnings', 'all', '--jobs', '2',
        '--library', $moduleRoot, '--library', $controllerRoot,
        '--library', $interpreterRoot, '--library', $trackerRoot, '--library', $chessRoot,
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
        $deadline = [DateTime]::UtcNow.AddSeconds($DurationSeconds + 300)
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
        $expectedCases = @('empty_start', 'ordinary_pipeline', 'ignored_confirmations', 'rejected_pipeline',
            'promotion_pipeline', 'fools_mate', 'scholars_mate', 'castling', 'en_passant', 'promotion_choices',
            'promotion_captures', 'san_disambiguation', 'custom_numbering', 'context_guard', 'invalid_moves',
            'finish_results', 'terminal_positions', 'finish_validation', 'finish_idempotence', 'reset',
            'metadata', 'metadata_invalid', 'date_validation', 'wrapping', 'capacity', 'history_preservation')
        if ($cases.Count -ne (2 * $expectedCases.Count) -or $caseGroups.Count -ne $expectedCases.Count -or
            @($caseGroups | Where-Object Count -ne 2).Count -ne 0 -or
            @($cases | Where-Object passed -ne $true).Count -ne 0 -or
            @(Compare-Object $expectedCases @($caseGroups.Name)).Count -ne 0) {
            throw 'A sonda nao confirmou os 26 casos aprovados antes e depois da estabilidade.'
        }
        $expectedFixtures = @{
            record_initial = @('record',0,0,20)
            record_kiwipete = @('record',0,0,48)
            record_mobility = @('record',0,0,62)
            record_capture = @('record',0,0,7)
            record_en_passant = @('record',0,0,7)
            record_promotion = @('record',0,0,9)
            record_mate = @('record',0,0,30)
            record_near = @('record',1275,8181,22)
            record_overflow = @('record',1276,8185,20)
            san_initial = @('san',0,0,20)
            san_kiwipete = @('san',0,0,48)
            san_mobility = @('san',0,0,62)
            pgn_empty = @('pgn',0,0,20)
            pgn_100 = @('pgn',100,590,20)
            pgn_600 = @('pgn',600,3791,20)
            pgn_near_metadata = @('pgn',1276,8185,20)
            finish_manual = @('finish',100,590,20)
            finish_resignation = @('finish',100,590,20)
            finish_mate = @('finish',0,0,0)
            finish_stalemate = @('finish',0,0,0)
            finish_material = @('finish',0,0,5)
        }
        if ($benchmarks.Count -ne $expectedFixtures.Count -or $benchmarkGroups.Count -ne $expectedFixtures.Count -or
            @($benchmarks | Where-Object { $_.failures -ne 0 -or $_.samples -ne 200 -or $_.warmups -ne 10 -or
                $_.min_us -lt 0 -or $_.median_us -lt $_.min_us -or $_.max_us -lt $_.median_us -or
                $_.mean_us -lt $_.min_us -or $_.mean_us -gt $_.max_us -or
                $_.stack_min_free_bytes -le 0 -or $_.heap_min_free_bytes -le 0 }).Count -ne 0 -or
            @(Compare-Object @($expectedFixtures.Keys) @($benchmarks.fixture)).Count -ne 0) {
            throw 'Benchmarks incompletos ou com falhas.'
        }
        foreach ($benchmark in $benchmarks) {
            $expected = $expectedFixtures[$benchmark.fixture]
            if ($benchmark.operation -ne $expected[0] -or $benchmark.plies -ne $expected[1] -or
                $benchmark.movetext_bytes -ne $expected[2] -or $benchmark.legal_moves -ne $expected[3] -or
                $benchmark.full_metadata -ne ($benchmark.fixture -eq 'pgn_near_metadata')) {
                throw "Metadados divergentes no benchmark: $($benchmark.fixture)"
            }
        }
        $begin = @($records | Where-Object type -eq 'begin')
        $progress = @($records | Where-Object type -eq 'progress')
        $longGame = @($records | Where-Object type -eq 'long_game')
        $longCheckpoints = @($records | Where-Object type -eq 'long_checkpoint')
        if ($begin.Count -ne 1 -or $begin[0].fixture_count -ne $expectedFixtures.Count -or
            $begin[0].suite_cases -ne $expectedCases.Count -or $begin[0].stack_allocated_bytes -ne 49152 -or
            $begin[0].movetext_limit_bytes -ne 8192 -or $begin[0].watchdog_ms -ne 30000 -or
            $begin[0].loop_watchdog_active -ne $true -or
            @($records | Where-Object type -eq 'fixture_error').Count -ne 0 -or
            @($progress | Where-Object failures -ne 0).Count -ne 0) {
            throw 'Identificacao da sonda, watchdog ou checkpoints nao confirmados.'
        }
        if ($longGame.Count -ne 1 -or $longGame[0].accepted -ne 1276 -or $longGame[0].exports -ne 1277 -or
            $longGame[0].movetext_bytes -ne 8185 -or $longGame[0].remaining_bytes -ne 7 -or
            $longGame[0].rejected_addition_bytes -ne 9 -or !$longGame[0].full_metadata -or
            !$longGame[0].overflow_preserved -or !$longGame[0].finish_rejected -or
            $longCheckpoints.Count -lt 5 -or @($longCheckpoints | Where-Object overflow -eq $true).Count -ne 1) {
            throw 'Partida prolongada ou limite de armazenamento nao confirmados.'
        }
        if ($stability.Count -ne 1 -or $stability[0].failures -ne 0 -or
            $stability[0].elapsed_ms -lt ($DurationSeconds * 1000) -or $stability[0].games -lt 1 -or
            $stability[0].accepted -ne ($stability[0].games * 1276) -or
            $stability[0].exports -ne ($stability[0].games * 1277) -or $stability[0].games -ne $progress.Count) {
            throw 'Ensaio de estabilidade incompleto ou com falhas.'
        }
        for ($i = 0; $i -lt $progress.Count; $i++) {
            if ($progress[$i].games -ne ($i + 1) -or $progress[$i].accepted -ne (($i + 1) * 1276) -or
                $progress[$i].exports -ne (($i + 1) * 1277)) { throw 'Checkpoint da estabilidade divergente.' }
        }
        if ($end.Count -ne 1 -or $end[0].failures -ne 0 -or $end[0].reported_cases -ne (2 * $expectedCases.Count) -or
            $memory.Count -lt 10 -or @($memory | Where-Object heap_intact -ne $true).Count -ne 0) {
            throw 'Conclusao da sonda ou integridade do heap nao confirmada.'
        }
        $pgnExports = @($records | Where-Object type -eq 'pgn_export')
        $serialText = Get-Content -Raw -LiteralPath (Join-Path $validationPath 'serial.log')
        $pgnBlocks = [regex]::Matches($serialText, '#PGN_BEGIN\r?\n([\s\S]*?)#PGN_END')
        if ($pgnExports.Count -ne 1 -or $pgnBlocks.Count -ne 1) { throw 'PGN exportado pela placa nao foi recebido.' }
        $pgnText = $pgnBlocks[0].Groups[1].Value
        $pgnBytes = [System.Text.Encoding]::UTF8.GetBytes($pgnText)
        [uint64]$digest = 2166136261
        foreach ($byte in $pgnBytes) { $digest = (($digest -bxor [uint64]$byte) * [uint64]16777619) -band [uint64]4294967295 }
        if ($pgnBytes.Length -ne $pgnExports[0].bytes -or $digest -ne $pgnExports[0].digest) {
            throw 'PGN recebido difere do tamanho ou checksum informado pela placa.'
        }
        $pgnPath = Join-Path $validationPath 'long-game.pgn'
        [System.IO.File]::WriteAllBytes($pgnPath, $pgnBytes)
        $result['pgnExport'] = [ordered]@{ file = 'long-game.pgn'; bytes = $pgnBytes.Length; fnv1a32 = $digest;
            sha256 = (Get-FileHash -LiteralPath $pgnPath -Algorithm SHA256).Hash.ToLowerInvariant() }
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

