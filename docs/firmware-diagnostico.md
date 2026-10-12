# Firmware de diagnóstico

O [sketch de diagnóstico](../firmware/diagnostico/diagnostico.ino) permite inspecionar a matriz, os botões, os indicadores e a leitura/escrita do cartão SD. Não implementa interpretação de jogadas, regras de xadrez, histórico, SAN/PGN ou inicialização do OLED.

## 1. Preparação e compilação

O alvo é o **ESP32 clássico / ESP-WROOM-32**, com FQBN `esp32:esp32:esp32`, na DevKit V1 de 30 pinos representada no esquemático. Conferir flash e parâmetros de upload; a pinagem não se aplica automaticamente às variantes S2/S3/C3. Alimentar somente pelo USB do computador, conforme a [documentação elétrica](eletronica.md).

Dependências: Arduino CLI e núcleo **Arduino-ESP32 3.3.12**. O código usa somente `Wire`, `SPI`, `FS` e `SD`, incluídas no núcleo. No PowerShell, a partir da raiz do repositório:

```powershell
$cli = 'arduino-cli' # No PATH; ou informe o caminho completo do executável.
$indice = 'https://espressif.github.io/arduino-esp32/package_esp32_index.json'
& $cli core update-index --additional-urls $indice
& $cli core install esp32:esp32@3.3.12 --additional-urls $indice
& .\firmware\compilar.ps1 -ArduinoCli $cli
```

Os dois comandos `core` preparam uma instalação que ainda não tenha essa versão. O [índice oficial da Espressif](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html) também pode ser usado no Gerenciador de Placas da Arduino IDE.

O script compila com `--warnings all`, grava os intermediários em `firmware/build/diagnostico/` e atualiza o [log](../firmware/validation/compile.log) e o [resultado com hashes dos fontes](../firmware/validation/resultado.json). Os intermediários não são versionados.

## 2. Upload e comandos

Após conferir alimentação e conexões, identificar a porta e substituir `COM3` pela porta real. Caso a placa não apareça, identificar seu conversor USB antes de instalar um driver.

```powershell
& $cli board list
& $cli upload --fqbn esp32:esp32:esp32 --port COM3 `
    --build-path .\firmware\build\diagnostico .\firmware\diagnostico
& $cli monitor --port COM3 --config baudrate=115200
```

O upload exige compilação concluída. Esses comandos são instruções de reprodução; upload e execução na placa ainda não foram verificados.

| Comando serial | Ação |
| --- | --- |
| `m` | Exibe a última matriz válida: `X` ocupada, `.` vazia |
| `r` | Reinicializa e verifica o MCP23017 |
| `v` | Alterna medição estática: COL_a LOW, demais colunas em alta impedância; suspende a varredura |
| `s` | Monta o SD, informa capacidade, abre o diretório raiz para leitura e desmonta o cartão |
| `w` | Grava um arquivo de teste de 16 KiB, relê e confere seu conteúdo, com varreduras e cargas de sinalização |
| `t` | Acende os dois LEDs e ativa o buzzer por aproximadamente 120 ms |
| `?` | Exibe ajuda |

O primeiro quadro válido, as mudanças de ocupação e os eventos dos botões aparecem automaticamente. Use `v` para medir ROW com multímetro sem obter a média da varredura; `v` novamente ou `r` libera a coluna e reinicia a leitura.

O comando `s` apenas lê. O comando `w` escolhe o primeiro nome inexistente entre `/DIAG000.TST` e `/DIAG999.TST`, grava 16 KiB em blocos de 512 bytes com `flush` e verifica tamanho e conteúdo por releitura. Se todos os nomes estiverem ocupados, cancela. O arquivo é preservado, inclusive em caso de falha; nenhum comando formata o cartão. Não retirar o cartão durante o ensaio.

Durante `w`, os dois LEDs ficam acesos e o buzzer atua no primeiro bloco; chamadas ao SD podem prolongar esse pulso. Varreduras são intercaladas com a escrita e a leitura, mas os botões não são processados durante o comando. O resultado distingue falha do SD de indisponibilidade da matriz. O ensaio não mede tensão, não testa PGN e não garante integridade de todo o cartão. Encerrar `v` antes de executar `w`.

## 3. Varredura e configuração

A configuração está em [board_config.h](../firmware/diagnostico/board_config.h); a pinagem e as conexões são documentadas em [eletronica.md](eletronica.md). O diagnóstico usa MCP23017 em `0x20`, I²C a 100 kHz e SD por SPI a 1 MHz.

Vista de cima, com as brancas próximas ao observador, `a1` ocupa o canto inferior esquerdo. O índice é `8 × (linha − 1) + coluna`, com `a=0`: a1=0, h1=7, a8=56 e h8=63. Não espelhar a identificação ao trabalhar pela face inferior do tabuleiro.

Na inicialização, o firmware reinicia o MCP e verifica IODIR/OLAT. Na varredura:

1. Desabilita todas as colunas antes de selecionar uma única saída LOW. As demais permanecem em alta impedância.
2. Mantém `OLATA=OLATB=0` e os pull-ups internos desligados (`GPPU=0`). Nenhuma coluna é comandada HIGH.
3. Aguarda 300 µs, lê as oito linhas ativas LOW e libera as colunas ao fim da varredura. O intervalo programado entre inícios é 25 ms.

Esses tempos são valores iniciais, sujeitos a medição com os oito pull-ups externos de 47 kΩ e a fiação real. `COLUMN_SETTLE_US` centraliza o tempo de acomodação. A leitura direta adotada precisa confirmar os níveis HIGH/LOW em bancada, conforme as [decisões elétricas](revisao-componentes-adicionais.md). Wi-Fi e Bluetooth não são inicializados.

Uma falha I²C descarta a amostra incompleta, acende o LED vermelho e mantém o MCP em RESET por GPIO14, independentemente do barramento. O comando `r` tenta a recuperação. Esse comportamento depende do RESET físico acessível e conectado.

Os sensores não recebem debounce, para permitir observar instabilidade dos contatos; os botões recebem 30 ms. Impressões seriais e testes de SD/indicadores podem atrasar a amostragem. O diagnóstico não preserva todas as transições e não valida RF02 ou a interpretação de capturas e *en passant*.

## 4. Verificações e limites

A compilação registrada passou com **Arduino CLI 1.5.2-rc.1 e Arduino-ESP32 3.3.12**, sem upload. As verificações `static_assert` cobrem as 64 conversões de coordenadas, os extremos a1/h8, a unicidade dos 16 pinos do MCP, a exclusão de GPA7/GPB7 das entradas e a seleção de uma única coluna sem tornar linhas saídas.

Essas verificações não executam o código no microcontrolador nem validam sinais, alimentação, detecção magnética ou periféricos. Os ensaios de bancada e os critérios de aceitação estão no [plano de testes físicos](plano-testes-fisicos.md).
