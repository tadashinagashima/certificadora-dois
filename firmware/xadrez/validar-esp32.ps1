param(
    [string]$Port,
    [ValidateRange(10, 3600)][int]$DurationSeconds = 180,
    [string]$ArduinoCli = 'C:\Program Files\Arduino CLI\arduino-cli.exe',
    [switch]$CompileOnly
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$moduleRoot = $PSScriptRoot
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
    scope = 'ChessAdapter and THC on ESP32. Functional tests, heap, stack margin, timings and bounded stability; no peripherals or firmware integration.'
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
    foreach ($source in @('src/ChessAdapter.h', 'src/ChessAdapter.cpp', 'src/thc/thc.h',
            'src/thc/thc.cpp', 'src/thc/LICENSE', 'tests/adapter_tests.h', 'tests/adapter_tests.cpp',
            'tests/esp32_runtime/esp32_runtime.ino', 'tests/esp32_runtime/runtime_probe.cpp',
            'library.properties', 'upstream.json', 'validar-esp32.ps1')) {
        $sourceHashes[$source] = (Get-FileHash -LiteralPath (Join-Path $moduleRoot $source) -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $upstream = Get-Content -Raw (Join-Path $moduleRoot 'upstream.json') | ConvertFrom-Json
    $result.upstreamCommit = $upstream.commit
    foreach ($entry in $upstream.files.PSObject.Properties) {
        if ($sourceHashes[$entry.Name] -ne $entry.Value) { throw "Fonte THC alterada: $($entry.Name)" }
    }
    $result.arduinoCliVersion = Invoke-ValidationStep 'arduino-version' @('version')
    $result.installedCores = Invoke-ValidationStep 'arduino-cores' @('core', 'list')
    foreach ($source in @('tests/esp32_runtime/esp32_runtime.ino', 'tests/esp32_runtime/runtime_probe.cpp',
            'tests/adapter_tests.h', 'tests/adapter_tests.cpp')) {
        Copy-Item -LiteralPath (Join-Path $moduleRoot $source) -Destination $probePath -Force
    }
    $result.compileStarted = $true
    Invoke-ValidationStep 'esp32-compile' @(
        'compile', '--fqbn', $result.fqbn, '--warnings', 'all', '--jobs', '2',
        '--library', $moduleRoot, '--build-path', $buildPath, $probePath) | Out-Null
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
                if ($line -match 'Guru Meditation|Backtrace:|[Ss]tack canary|stack smashing|abort\(\) was called|assert failed|Brownout detector') {
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
        if ($cases.Count -ne 36 -or $caseGroups.Count -ne 18 -or
            @($caseGroups | Where-Object Count -ne 2).Count -ne 0 -or
            @($cases | Where-Object passed -ne $true).Count -ne 0) {
            throw 'A sonda nao confirmou os 18 casos aprovados antes e depois da estabilidade.'
        }
        if ($benchmarks.Count -ne 24 -or $benchmarkGroups.Count -ne 24 -or
            @($benchmarks | Where-Object { $_.failures -ne 0 -or $_.samples -ne 200 }).Count -ne 0) {
            throw 'Benchmarks incompletos ou com falhas.'
        }
        if ($stability.Count -ne 1 -or $stability[0].failures -ne 0 -or
            $stability[0].elapsed_ms -lt ($DurationSeconds * 1000) -or $stability[0].batches -lt 1) {
            throw 'Ensaio de estabilidade incompleto ou com falhas.'
        }
        if ($end.Count -ne 1 -or $end[0].failures -ne 0 -or $end[0].reported_cases -ne 36 -or
            $memory.Count -lt 5 -or @($memory | Where-Object heap_intact -ne $true).Count -ne 0) {
            throw 'Conclusao da sonda ou integridade do heap nao confirmada.'
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
