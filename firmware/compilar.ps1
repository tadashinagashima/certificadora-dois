param(
    [string]$ArduinoCli = 'C:\Program Files\Arduino CLI\arduino-cli.exe'
)

$ErrorActionPreference = 'Stop'
$firmwareRoot = $PSScriptRoot
$buildPath = Join-Path $firmwareRoot 'build\diagnostico'
$validationPath = Join-Path $firmwareRoot 'validation'
$sketchPath = Join-Path $firmwareRoot 'diagnostico'
New-Item -ItemType Directory -Force -Path $validationPath | Out-Null

$cliVersion = (& $ArduinoCli version | Out-String).Trim()
$cores = (& $ArduinoCli core list | Out-String).Trim()
$compileArgs = @('compile', '--fqbn', 'esp32:esp32:esp32', '--warnings', 'all',
    '--build-path', $buildPath, $sketchPath)
$compileOutput = & $ArduinoCli @compileArgs 2>&1
$compileExit = $LASTEXITCODE
$compileText = ($compileOutput | ForEach-Object { $_.ToString() }) -join "`n"
$compileText | Set-Content -LiteralPath (Join-Path $validationPath 'compile.log') -Encoding utf8

$sourceFiles = @('diagnostico.ino', 'board_config.h')
$sourceHashes = [ordered]@{}
foreach ($sourceFile in $sourceFiles) {
    $sourceHashes[$sourceFile] = (Get-FileHash -LiteralPath (Join-Path $sketchPath $sourceFile) -Algorithm SHA256).Hash.ToLowerInvariant()
}
$result = [ordered]@{
    performedAtUtc = [DateTime]::UtcNow.ToString('o')
    cliVersion = $cliVersion
    installedCores = $cores
    fqbn = 'esp32:esp32:esp32'
    exitCode = $compileExit
    success = ($compileExit -eq 0)
    warnings = 'all'
    sourceSha256 = $sourceHashes
    compileTimeChecks = @('64 square coordinate round trips', 'a1=0 and h8=63',
        '16 MCP pins unique', 'GPA7/GPB7 never used as row inputs',
        'one output column per selection', 'all row direction bits remain inputs')
    uploadPerformed = $false
    physicalTestsPerformed = $false
    notes = 'Compilation and constexpr assertions only; no hardware execution or SD data-integrity validation.'
}
$result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $validationPath 'resultado.json') -Encoding utf8
Write-Output $compileText
exit $compileExit
