# Firmware

O projeto [principal](../firmware/principal/platformio.ini) integra cinco bibliotecas locais e implementa partidas por ocupações simuladas na Serial. O [diagnóstico](../firmware/diagnostico/diagnostico.ino) é independente e acessa os periféricos. A pinagem reside em [board_config.h](../firmware/diagnostico/board_config.h) e está descrita em [hardware.md](hardware.md).

## Compilar e usar

Alvo: ESP32 clássico/ESP-WROOM-32, DevKit V1 de 30 pinos; CPU 240 MHz, flash 4 MiB QIO/80 MHz, partições `default.csv`, sem PSRAM. PlatformIO Core **6.2.0** e pioarduino **55.03.312** fixam Arduino-ESP32 **3.3.12** / IDF **5.5.5**. O cabeçalho DIO produzido para o bootloader QIO é esperado. Não instalar as cinco bibliotecas globalmente: o projeto usa `symlink://` para os diretórios irmãos.

Na raiz, com Python 3.13 (preparar venv/pacotes uma vez):

```powershell
python -m venv --without-scm-ignore-files firmware/principal/build/venv
$python = '.\firmware\principal\build\venv\Scripts\python.exe'
$env:PLATFORMIO_CORE_DIR = Join-Path (Get-Location) 'firmware/principal/build/platformio-core'
$env:PYTHONIOENCODING = 'utf-8'
& $python -m pip install 'platformio==6.2.0'
& $python -m platformio run --project-dir firmware/principal --environment esp32
& $python -m serial.tools.list_ports -v
```

Em novas sessões, redefinir `$python` e as variáveis; não recriar o ambiente. A primeira compilação baixa o toolchain e o core fixados. Substituir COM7 pela porta encontrada; upload substitui o programa da placa:

```powershell
& $python -m platformio run --project-dir firmware/principal --environment esp32 --target upload --upload-port COM7
& $python -m platformio device monitor --project-dir firmware/principal --port COM7
```

`main.cpp` inicia a Serial e informa versão, placa e hash ELF. Reserva 32 KiB de pilha para `loopTask`, habilita seu watchdog e cede tempo ao runtime. `SerialGame` coordena as chamadas C++ e o protocolo; o callback UART apenas sinaliza erro por variável atômica. Partida e PGN ficam em RAM: exportar antes de reiniciar ou desligar. Metadados desconhecidos permanecem `?`.

## Protocolo Serial v1

115200 baud, 8N1. Comandos ASCII minúsculos, um por linha LF ou CRLF, sem espaços adicionais; casas `a1`–`h8`. Esperar `DONE` antes de enviar o próximo comando. Todas as respostas de comando terminam em uma linha `DONE`; o boot termina em `READY protocol=1`. `ERROR` informa entrada rejeitada ou bloqueio, e `CONFIRM` conserva o resultado público do Controle.

| Comando | Efeito |
| --- | --- |
| `help` | Lista comandos e cenários. |
| `new` / `new start` | Descarta a sessão e inicia a posição padrão. |
| `new CENARIO` | Inicia uma das posições conhecidas da tabela abaixo. |
| `observe HEX16` | Entrega uma ocupação completa de exatamente 16 dígitos hexadecimais; bit 0 = a1, bit 63 = h8. |
| `sync` | Simula uma nova observação igual à ocupação oficial, inclusive para retomar uma tentativa. |
| `lift e2` / `place e4` | Retira/coloca ocupação na última leitura disponível e entrega o quadro completo. Exige leitura anterior. |
| `confirm` | Interpreta e confirma a tentativa. Repetir uma confirmação aceita não duplica o lance. |
| `promote Q` / `R` / `B` / `N` | Seleciona dama, torre, bispo ou cavalo durante promoção pendente; exige nova confirmação. |
| `interrupt` | Simula perda de continuidade da aquisição. |
| `finish` | Encerra manualmente, com resultado `*`. |
| `resign white` / `resign black` | Abandono da cor indicada; registra a vitória da outra cor. |
| `status` | Estado, falha, lances registrados, promoção, confiabilidade, FEN, ocupações, encerramento e indicadores de empate. |
| `pgn` | Documento entre `PGN_BEGIN` e `PGN_END`. Não encerra a partida. |
| `metrics` | Tempo ativo, comandos, erros de entrada, maior duração de comando, heap, maior bloco livre, pilha, integridade e watchdog. |

`sync`, `lift` e `place` são recursos exclusivos da simulação: não leem nem movem peças reais. Capturas exigem observar a casa capturada vazia. No roque, entregar também as alterações da torre; no en passant, retirar o peão capturado. Concluir a substituição física simulada antes de escolher a promoção. Os módulos continuam sendo a única fonte das regras de xadrez.

| Cenário | Posição preparada |
| --- | --- |
| `start` | Posição inicial. |
| `castle_w`, `castle_b` | Reis e torres com os quatro direitos de roque; lado indicado a jogar. |
| `ep_w`, `ep_b` | Captura en passant e5–d6 ou e4–d3. |
| `promotion_w`, `promotion_b` | Peão a7–a8 ou a2–a1. |
| `promotion_capture_w`, `promotion_capture_b` | Promoção capturando torre em b8 ou b1. |
| `ambiguous` | Torre a1 pode capturar a8 ou b1; permite testar evidências conflitantes. |
| `mate`, `stalemate`, `insufficient` | Posições já encerradas, sem lances. |
| `busy` | Posição com muitos movimentos legais, incluindo Bxa6. |

As FENs estão definidas apenas em `SerialGame.cpp`; `status` permite consultá-las, e o PGN não padrão inclui `FEN`/`SetUp`. Não se aceita FEN arbitrária pela Serial, respeitando o escopo do parser THC.

Exemplo de uma partida completa, enviando uma linha e esperando `DONE` a cada passo:

```text
new
sync
lift f2
place f3
confirm
lift e7
place e5
confirm
lift g2
place g4
confirm
lift d8
place h4
confirm
pgn
```

O corpo do PGN será `1. f3 e5 2. g4 Qh4# 0-1` e o estado será `Finished`.

O receptor aceita até **96 caracteres** por linha. Linha excedente, NUL, controles ou CR fora do CRLF são descartados até LF, sem executar sufixos. Uma pausa de mais de dois segundos no meio da linha também descarta o restante até LF. Erros UART detectados e comandos malformados invalidam a tentativa quando há perda potencial de entrada. Receber novos quadros divergentes não restaura essa confiabilidade; usar `sync` ou uma observação oficial completa. Não se promete detectar corrupção que produza outro comando sintaticamente válido.

## Contratos dos módulos

Os cabeçalhos em `firmware/*/src/` são a referência das assinaturas e enums. Os módulos são C++14 independentes de Arduino. Consultas são `const` ou cópias; chamadas à mesma instância devem ser serializadas, fora de interrupções.

### Ocupação e acompanhamento

`Occupancy` é `uint64_t`: bit 1 = ocupada, a1=0, h1=7, a8=56, h8=63. `MoveTracker::observe(occupancy, timestampMs)` recebe somente quadros completos e válidos. A primeira leitura cria a referência; cada mudança posterior ocupa um quadro com máscaras disjuntas `removed` e `added`. Quadros idênticos não gastam histórico. A ordem é a do vetor, não a do timestamp; tempos iguais e wrap de 32 bits são permitidos.

`snapshot()` devolve cópia independente. Somente `frames[0..frameCount)` é válido. Há limite fixo de **64 quadros com alterações**, não de casas. `reliable()` exige referência inicial, leitura disponível e ausência de estouro/interrupção. `markInputInterrupted()` informa perda sem fabricar quadro vazio; o prefixo fica congelado, mas novas leituras continuam atualizando a ocupação. `discardAttempt()` usa a última leitura como referência e limpa as causas apenas se houver leitura disponível. A recuperação da partida cabe ao Controle.

### Regras e interpretação

`ChessAdapter` fornece `reset`, `fen`, `loadFen`, `pieceAt`, `occupancy`, `sideToMove`, `legalMoves`, `isLegal`, `san`, `apply` e `status`. `Move` contém origem/destino (0–63) e promoção; 64 é casa inválida. `legalMoves()` comporta até 218 movimentos. `apply()` revalida antes de modificar; `san()` opera na posição anterior. Cópias preservam o histórico de repetição. `loadFen()` limpa esse histórico e usa o dialeto permissivo da THC: somente posições de teste conhecidas, não validação estrita ou recuperação por sensores.

A THC reside somente em [xadrez/src/thc](../firmware/xadrez/src/thc/LICENSE). É a cópia sem alterações do repositório `billforsternz/thc-chess-library`, commit `aa217cb9b4d759cd7c07e11c98526b0187b0a64b`, sob MIT. Não alterar os fontes vendorizados sem revisar a integração.

`interpretMove(snapshot, position, selection)` preserva as entradas e retorna `Identified`, `NoMove`, `NoLegalMatch`, `Ambiguous`, `PromotionRequired`, `UnreliableInput` ou `PositionMismatch`. Apenas `Identified` preenche `move`; os demais mantêm casas inválidas. A função verifica confiabilidade e consistência dos quadros antes de comparar movimentos legais. Capturas exigem evidência de destino vazio; promoção não escolhe dama automaticamente. Roque e en passant podem ter ordens distintas de retirada/colocação, mas o quadro final precisa corresponder ao lance completo.

### Controle e encerramento

| Estado | Contrato |
| --- | --- |
| `AwaitingPosition` | Exige nova observação igual à ocupação oficial. `confirm()` não cria a referência. |
| `Tracking` | Acumula alterações e aceita confirmação. |
| `AwaitingPromotion` | Aguarda Q/R/B/N e nova confirmação; posição oficial ainda anterior. Mudança de ocupação cancela a seleção. |
| `Finished` | Observações/interrupções ignoradas, comandos de partida bloqueados até reiniciar. |

O construtor padrão e `reset()` estabelecem a posição inicial. O construtor com adaptador copia integralmente a posição preparada, inclusive repetição. `position()` fornece referência const, `attempt()` uma cópia, e `ending()` informa motivo/resultado.

`confirm()` retorna `Accepted`, `NoMove`, `NoLegalMatch`, `Ambiguous`, `PromotionRequired`, `UnreliableInput`, `PositionMismatch`, `ApplyFailed` ou `GameFinished`. Apenas `Accepted` preenche o lance e aplica a posição. `NoMove` limpa a tentativa sem registro; rejeições preservam a posição/histórico e aguardam reposição. A primeira confirmação de promoção apenas solicita a escolha; a segunda reinterpreta as observações atuais. Repetir confirmação após aceitação não reaplica o lance.

`resign(cor)` atribui vitória à outra cor. `finish()` encerra com `Unfinished` (`*`). Mate encerra com vitória; afogamento e o material insuficiente reconhecido pela THC encerram como empate. Os indicadores de 50 lances e tripla repetição são consultáveis, sem adjudicação automática. Não há novas regras de 75 lances, cinco repetições ou prova de todas as posições mortas.

### Registro e consistência

`GameRecord::begin(initial, metadata)` inicia/substitui o registro. `record(before, confirmation, after)` aceita apenas `Accepted`; os demais retornam `Ignored`. Gera SAN no contexto anterior, aplica o lance numa cópia e confere a FEN posterior/esperada. Duplicatas, saltos e contexto incorreto são rejeitados. `finish(official, ending)` confere contexto/resultado; repetição do mesmo encerramento é idempotente. `pgn()` exporta o documento; antes de iniciar retorna vazio.

Retornos: `Ok`, `Ignored`, `NotStarted`, `AlreadyFinished`, `InvalidMetadata`, `InvalidContext`, `InvalidMove`, `CapacityExceeded`, `InvalidResult`. Erros preservam o registro. O chamador deve preservar a posição anterior antes de confirmar. `SerialGame` faz isso confirmando numa cópia do Controle; só publica Controle e Registro após ambos aceitarem e, quando aplicável, encerra o Registro na mesma operação. Falha ativa `fault`, bloqueando comandos até `new`; o PGN anterior continua exportável.

Limites: movetext de **8192 bytes**, sem truncamento; valores de metadados até **128 bytes**, sem controles ASCII/NUL/DEL e com linha de cabeçalho escapada menor que 256 bytes. Data `YYYY.MM.DD`, permitindo partes inteiramente `?`; verifica faixas, não calendário. Aspas e barras são escapadas; UTF-8 é preservado. PGN inclui sete tags padrão, SAN numerada, resultado consistente e linhas de até 79 caracteres. Início não padrão inclui FEN/SetUp e respeita número/lado inicial. Partida ativa exporta `*` sem encerrar.

Esse limite textual não é orçamento total de RAM; cópias, strings e exportação consomem heap adicional. Falha geral do alocador não é convertida em `CapacityExceeded`. Medições de uma partida não garantem estabilidade indefinida.

## Diagnóstico de montagem

Requer Arduino CLI e core `esp32:esp32@3.3.12`; apenas Wire/SPI/FS/SD do core. Instalar o core uma vez e compilar na raiz:

```powershell
arduino-cli core install esp32:esp32@3.3.12 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli compile --fqbn esp32:esp32:esp32 --warnings all --build-path firmware/diagnostico/build firmware/diagnostico
arduino-cli upload --fqbn esp32:esp32:esp32 --port COM7 --build-path firmware/diagnostico/build firmware/diagnostico
arduino-cli monitor --port COM7 --config baudrate=115200
```

| Comando | Efeito |
| --- | --- |
| `m` | Última matriz válida (`X` ocupada, `.` vazia). |
| `r` | Reinicializa e verifica MCP23017. |
| `v` | Alterna COL_a LOW estática para medição; suspende varredura. |
| `s` | Monta SD, informa capacidade, lê diretório raiz e desmonta. |
| `w` | Cria `/DIAG000.TST`…`/DIAG999.TST` no primeiro nome livre, grava/relê 16 KiB e verifica conteúdo. |
| `t` / `?` | Testa LEDs/buzzer por aproximadamente 120 ms / ajuda. |

O arquivo de teste é preservado; não há formatação. Não retirar SD durante `w`; desligar modo `v` antes. Durante o teste há varreduras e carga dos LEDs/buzzer, mas botões não são processados. Chamadas SD podem prolongar os pulsos.

A varredura usa I²C 100 kHz, MCP `0x20`, `OLAT=0`, `GPPU=0`, somente uma coluna LOW e demais em alta impedância. Espera inicial de 300 µs e período de 25 ms, sujeitos à fiação real. Falha I²C descarta amostra, acende vermelho e mantém MCP em RESET; `r` tenta recuperar. Sensores não têm debounce neste diagnóstico; botões usam 30 ms. Ele não preserva todas as transições nem interpreta xadrez. Wi-Fi, Bluetooth e OLED não são inicializados.

## Verificações de manutenção

Um executável por módulo verifica seus contratos (109 casos); dez grupos de integração exercitam o protocolo real, partidas completas, especiais, rejeições, retomada, comandos inválidos, capacidade e repetição. O CTest executa os cinco conjuntos de contratos e os dez grupos de integração; cada caso unitário é registrado apenas no respectivo fonte de teste. Compilar em C++14 Release com CMake 3.20+, compilador C++ e Python 3:

```powershell
cmake -S firmware -B firmware/build
cmake --build firmware/build --config Release
ctest --test-dir firmware/build -C Release --output-on-failure --no-tests=error
```

No Windows, instalar CMake/MSVC ou usar os executáveis distribuídos pelo Visual Studio. O [CMake único](../firmware/CMakeLists.txt) mantém dependências e opções comuns. Cada executável de contratos aceita o nome de um caso para execução isolada; `ctest -R controller` seleciona um conjunto. Saídas de build/testes ficam locais.

Para verificar a aplicação já gravada no ESP32, fechar outros monitores e usar o mesmo ELF do upload:

```powershell
& $python firmware/principal/tests/integration.py --port COM7 --elf firmware/principal/build/pio/build/esp32/firmware.elf
```

O teste reinicia via RTS, confere versão/hash, envia comandos e substitui partidas em RAM. Asserções verificam PGN, estado, confiabilidade, limite de registro, heap e pilha. Para diagnóstico opcional, `--output firmware/principal/build/test-run` salva saídas numa pasta nova. Não se mantêm históricos no repositório.
