# Firmware

O [firmware da placa](../firmware/principal/src/main.cpp) e o [simulador](../firmware/simulador/main.cpp) compartilham `SerialGame`, `BoardApp`, o filtro de sensores e os cinco módulos C++14. O [terminal Python](../firmware/terminal.py) usa o mesmo protocolo para controlar os dois e gravar PGNs no computador.

## Compilar o simulador

Requer CMake 3.20+ e compilador C++14. Para executar a demonstração, use Python 3.9+; a comunicação com o simulador usa apenas a biblioteca padrão. Na raiz:

```powershell
cmake -S firmware -B firmware/build
cmake --build firmware/build --config Release
python firmware/terminal.py --sim firmware/build/Release/tabuleiro_sim.exe
```

Com gerador de configuração única no Linux/macOS, use `python3 firmware/terminal.py --sim firmware/build/tabuleiro_sim`. O executável também aceita o protocolo bruto em stdin/stdout. O terminal é o componente que grava os arquivos.

## Compilar e gravar o ESP32

Alvo: ESP32 clássico/ESP-WROOM-32, DevKit V1 de 30 pinos; CPU 240 MHz, flash 4 MiB QIO/80 MHz, partições `default.csv`, sem PSRAM. PlatformIO Core **6.2.0**, pioarduino **55.03.312**, Arduino-ESP32 **3.3.12** / IDF **5.5.5**. O cabeçalho DIO do bootloader QIO é esperado. O [platformio.ini](../firmware/principal/platformio.ini) usa as cinco bibliotecas locais por `symlink://` e baixa U8g2 2.36.18 para o OLED SSD1315.

Na raiz, com Python 3.13; preparar o ambiente uma vez:

```powershell
python -m venv --without-scm-ignore-files firmware/principal/build/venv
$python = '.\firmware\principal\build\venv\Scripts\python.exe'
$env:PLATFORMIO_CORE_DIR = Join-Path (Get-Location) 'firmware/principal/build/platformio-core'
$env:PYTHONIOENCODING = 'utf-8'
& $python -m pip install 'platformio==6.2.0'
& $python -m platformio run --project-dir firmware/principal --environment esp32
& $python -m serial.tools.list_ports -v
```

Em sessões seguintes, redefinir as variáveis sem recriar o ambiente. A primeira compilação baixa o toolchain/core. Substituir COM7 pela porta encontrada; upload substitui o programa da placa:

```powershell
& $python -m platformio run --project-dir firmware/principal --environment esp32 --target upload --upload-port COM7
& $python firmware/terminal.py --port COM7
```

Feche outros monitores que ocupem a porta. O ambiente PlatformIO já contém pySerial. Para rodar o terminal em outro ambiente Python, instale `python -m pip install pyserial==3.5`. A aplicação física preserva a pilha de 32 KiB do `loopTask` e o watchdog.

## Operar pelo terminal

A conexão recupera o estado atual da placa. Posicione as peças iniciais e consulte `status`: `Tracking` indica que a matriz reconheceu a ocupação. Levante as peças para mover; em captura, deixe o sistema observar a vacância do destino antes de ocupá-lo. Depois envie `confirm`. Se a jogada for rejeitada, reponha a última posição aceita e refaça o lance. Os reeds detectam somente presença.

| Comando | Efeito |
| --- | --- |
| `confirm` | Confirma a movimentação. A resposta informa aceitação, rejeição ou promoção pendente. |
| `promote Q` / `promote R` / `promote B` / `promote N` | Seleciona dama, torre, bispo ou cavalo. Envie `confirm` novamente para aplicar. |
| `status` / `pgn` | Mostra estado, FEN, ocupações, sinais, último SAN e xeque / exibe o PGN em RAM. |
| `finish` / `resign white` / `resign black` | Encerra com resultado `*` / registra abandono da cor indicada. |
| `new` | Descarta a sessão ativa e reinicia. Um PGN encerrado pendente de gravação bloqueia o comando. |
| `save` | Repete a transferência pendente. Após sucesso, a placa responde `OK saved`. |
| `pasta CAMINHO` | Seleciona no computador o destino da gravação. Aceita espaços no caminho. |
| `ajuda` / `sair` | Lista os comandos / fecha a conexão. |
| `metrics` | Somente na placa: heap livre, mínimo de heap e menor folga de pilha do loop em bytes. |

O OLED tem quatro linhas de informação: vez ou resultado em faixa invertida, último SAN aceito, estado da partida e situação do PGN. Na rejeição, exibe um aviso temporário; na promoção, orienta a escolha pelo PC. `status` também informa `OLED online` ou `OLED offline` na placa. O perfil SSD1315 do U8x8 atualiza grupos de até quatro caracteres que mudaram, sem redesenhar a tela inteira em cada varredura. A primeira falha I²C do display desativa novas transferências para ele até o próximo boot. Veja [ligação, endereço e limites do barramento](hardware.md#oled-de-096).

O LED verde contínuo indica acompanhamento estável. O vermelho indica espera, reposição ou falha; rejeição também aciona brevemente o buzzer. Na promoção, o LED verde pulsa 1/2/3/4 vezes para Q/R/B/N. Após encerrar, vermelho piscando indica PGN pendente e verde piscando indica que o computador confirmou a gravação.

## Aquisição e gravação

A [pinagem e os tempos](../firmware/board_config.h) são compartilhados com o diagnóstico. A aplicação tenta uma varredura completa a cada **25 ms**, I²C a 100 kHz, MCP `0x20`, uma coluna LOW e demais em alta impedância. Acomodação inicial de **300 µs**; `OLAT=0`, `GPPU=0` e pull-ups externos de 47 kΩ. Falha I²C do MCP descarta a varredura e mantém RESET LOW; há tentativa de reinicialização a cada segundo.

`BoardApp::sample()` aplica debounce de **25 ms por casa**, preservando quadros com alterações estáveis. A referência inicial exige estabilização das 64 casas. Uma confirmação exige leitura disponível, recente e sem mudanças pendentes no filtro. Um intervalo **maior que 100 ms** sem amostra completa invalida a tentativa, inclusive quando provocado por processamento ou saída Serial prolongada. Recuperar exige nova leitura estável igual à ocupação oficial. Esses valores são iniciais e devem ser ajustados com a montagem.

A varredura não é instantânea: mudanças muito rápidas, entre leituras ou mais curtas que o filtro, podem não ser observadas. Isso também vale para a vacância de uma captura. Software não recupera transições que os sensores não entregaram.

O terminal salva automaticamente no encerramento manual, abandono, mate, afogamento ou material insuficiente reconhecido. Por padrão, cria `build/partidas/` no diretório de execução. Use `--destino CAMINHO` na abertura para escolher outra pasta. O nome `partida-<identificador>.pgn` combina um identificador do boot com o número da sessão. Duas partidas com os mesmos lances recebem nomes distintos.

O computador verifica tamanho e CRC-32 do PGN, grava um temporário na pasta de destino, sincroniza e fecha o arquivo. Publica por renomeação exclusiva no Windows e por hard link em Linux/macOS (escolha um volume com suporte a links físicos). Se uma confirmação se perder, o terminal reconhece o arquivo idêntico na mesma pasta. Um arquivo preexistente com conteúdo diferente é preservado e a falha é informada. Use uma pasta local gravável e com espaço livre.

Em erro de escrita, mantenha o ESP32 ligado, corrija a pasta e envie `save`. Se houver queda de comunicação, reabra o terminal usando a mesma pasta. O `status` inicial recupera a pendência se o adaptador mantiver a placa ligada e sem reset. Partidas ativas e arquivos ainda pendentes ficam na RAM. Um reset, a retirada do cabo que fornece energia ou o encerramento do processo do simulador descarta essa memória.

## Protocolo e simulação

USB serial a 115200 baud, 8N1, ASCII nos comandos e LF/CRLF. Cada comando recebe uma resposta encerrada por uma linha `DONE`. O ESP32 emite `READY protocol=2` no boot. Uma conexão aberta depois do boot pode enviar `hello` e verificar `PROTOCOL 2`. O simulador usa stdin/stdout e responde ao mesmo `hello`.

Ao encerrar, ou em resposta a `status` / `save` durante a pendência, o firmware inclui `SAVE_PENDING ID BYTES CRC32`. Tamanho e CRC-32/ISO-HDLC (decimal) referem-se ao PGN UTF-8 com quebras LF. O terminal pede `pgn ID`, confere o bloco entre `PGN_BEGIN` e `PGN_END`, inclusive as linhas em branco, e só transmite `saved ID` após a publicação. A placa rejeita identificadores de outra sessão. `pgn` sem argumento continua disponível para leitura em qualquer fase.

| Comando exclusivo do simulador | Efeito |
| --- | --- |
| `sync` | Oferece a ocupação oficial ao filtro e avança 75 ms. |
| `observe HEX16` | Oferece ocupação de 16 dígitos hexadecimais ao filtro e avança 75 ms; bit 0=a1, bit 63=h8. |
| `lift e2` / `place e4` | Retira/coloca uma ocupação na última leitura estável e avança 75 ms. Exige leitura anterior. |
| `raw HEX16` | Altera somente a ocupação bruta, sem avançar tempo. |
| `wait MS` | Avança o relógio virtual em 0…60000 ms. |
| `interrupt` | Interrompe a aquisição simulada; `raw`, `observe` ou `sync` voltam a oferecer amostras. |
| `new CENARIO` | Inicia uma posição conhecida, somente para experimentar situações especiais. |

O relógio virtual só avança nos comandos descritos: esperar no teclado não simula perda de leitura. Há ticks de até 5 ms e amostras a cada 25 ms de tempo virtual. O terminal grava arquivos reais. O simulador não reproduz a velocidade de execução do ESP32.

Cenários: `start`; `castle_w`/`castle_b`; `ep_w`/`ep_b`; `promotion_w`/`promotion_b`; `promotion_capture_w`/`promotion_capture_b`; `ambiguous`; `mate`; `stalemate`; `insufficient`; `busy`. As FENs estão em `SerialGame.cpp`; `status` mostra a posição. Não há parser público de FEN arbitrária. Cenários já encerrados também geram PGN automaticamente.

Demonstração completa de mate no terminal com `--sim`:

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
status
pgn
```

Após a última confirmação, o terminal informa o caminho do PGN. Confira `Finished`, `Checkmate`, `saved=1` e o corpo `1. f3 e5 2. g4 Qh4# 0-1` no arquivo.

Para experimentar ruído na casa e2, iniciar com `new`, `sync`; enviar `raw ffff00000000efff`, `wait 25`, `raw ffff00000000ffff`, `wait 50`. A retirada curta é filtrada; `status` deve mostrar zero quadros de alterações. Para uma captura, manter cada alteração com `lift`/`place` ou oferecer leituras brutas suficientemente longas.

O receptor limita linhas a **96 caracteres**. Linha excedente, NUL, controles ou CR fora de CRLF são descartados até LF, sem executar sufixos. No ESP32, pausa maior que dois segundos numa linha parcial também provoca descarte. Erros no fluxo simulado invalidam sua tentativa; erros de console na placa não invalidam as leituras físicas independentes. Nenhum parser detecta toda corrupção que resulte em outro comando válido.

## Contratos dos módulos

Os cabeçalhos em `firmware/*/src/` são a referência das assinaturas e enums. Os módulos são C++14 independentes de Arduino. Consultas são `const` ou cópias; chamadas à mesma instância devem ser serializadas, fora de interrupções.

### Ocupação e acompanhamento

`Occupancy` é `uint64_t`: bit 1 = ocupada, a1=0, h1=7, a8=56, h8=63. `MoveTracker::observe(occupancy, timestampMs)` recebe somente quadros completos e válidos. A primeira leitura cria a referência; cada mudança posterior ocupa um quadro com máscaras disjuntas `removed` e `added`. Quadros idênticos não gastam histórico. A ordem é a do vetor, não a do timestamp; tempos iguais e wrap de 32 bits são permitidos.

`snapshot()` devolve cópia independente. Somente `frames[0..frameCount)` é válido. Há limite fixo de **64 quadros com alterações**, não de casas. `reliable()` exige referência inicial, leitura disponível e ausência de estouro/interrupção. `markInputInterrupted()` informa perda sem fabricar quadro vazio; o prefixo fica congelado, mas novas leituras continuam atualizando a ocupação. `discardAttempt()` usa a última leitura como referência e limpa as causas apenas se houver leitura disponível. A recuperação da partida cabe ao Controle.

### Regras e interpretação

`ChessAdapter` fornece `reset`, `fen`, `loadFen`, `pieceAt`, `occupancy`, `sideToMove`, `fullmoveNumber`, `legalMoves`, `isLegal`, `san`, `apply` e `status`. `Move` contém origem/destino (0–63) e promoção; 64 é casa inválida. `legalMoves()` comporta até 218 movimentos. `apply()` revalida antes de modificar; `san()` opera na posição anterior. `fullmoveNumber()` fornece diretamente o número da jogada mantido pela biblioteca. Cópias preservam o histórico de repetição. `loadFen()` limpa esse histórico e usa o dialeto permissivo da THC: somente posições de teste conhecidas, não validação estrita ou recuperação por sensores.

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

Retornos: `Ok`, `Ignored`, `NotStarted`, `AlreadyFinished`, `InvalidMetadata`, `InvalidContext`, `InvalidMove`, `CapacityExceeded`, `InvalidResult`. Erros preservam o registro. O chamador deve preservar a posição anterior antes de confirmar. `BoardApp` faz isso confirmando numa cópia do Controle; só publica Controle e Registro após ambos aceitarem e, quando aplicável, encerra o Registro na mesma operação. Falha ativa `fault`, bloqueando comandos até `new`; o PGN anterior continua exportável.

Limites: movetext de **8192 bytes**, sem truncamento; valores de metadados até **128 bytes**, sem controles ASCII/NUL/DEL e com linha de cabeçalho escapada menor que 256 bytes. Data `YYYY.MM.DD`, permitindo partes inteiramente `?`; verifica faixas, não calendário. Aspas e barras são escapadas; UTF-8 é preservado. PGN inclui sete tags padrão, SAN numerada, resultado consistente e linhas de até 79 caracteres. Início não padrão inclui FEN/SetUp e respeita número/lado inicial. Partida ativa exporta `*` sem encerrar.

Esse limite textual não é orçamento total de RAM; cópias, strings e exportação consomem heap adicional. Falha geral do alocador não é convertida em `CapacityExceeded`. Medições de uma partida não garantem estabilidade indefinida.

## Diagnóstico de montagem

Requer Arduino CLI e core `esp32:esp32@3.3.12`; usa Wire do core. Instalar o core uma vez e compilar na raiz:

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
| `o` | Consulta o endereço configurado para o OLED e informa ACK ou ausência de resposta. |
| `t` / `?` | Testa LEDs/buzzer por aproximadamente 120 ms / ajuda. |

A varredura usa I²C 100 kHz, MCP `0x20`, `OLAT=0`, `GPPU=0`, somente uma coluna LOW e demais em alta impedância. Espera inicial de 300 µs e período de 25 ms, sujeitos à fiação real. Falha I²C descarta amostra, acende vermelho e mantém MCP em RESET; `r` tenta recuperar. O diagnóstico exibe a matriz bruta, sem debounce. Ele não preserva todas as transições nem interpreta xadrez. O comando `o` apenas consulta o endereço. O teste de texto usa a aplicação principal.

## Verificar antes da entrega física

Antes de gravar a placa, compilar o simulador e o firmware e executar a demonstração acima. Experimentar uma captura, roque, en passant, as quatro promoções, confirmação repetida, jogada ilegal, `interrupt` e reposição. Conferir que posição e PGN não avançam em rejeições.

Para conferir recuperação, inicie o terminal com `--destino` apontando para um arquivo comum e encerre uma partida. Verifique a mensagem de erro e consulte `pgn`. Envie `pasta build/partidas`, depois `save`, e confira o conteúdo do novo arquivo. Outro `save` deve preservar a quantidade de arquivos. Repita a partida e confira que o primeiro PGN continua intacto.

Na bancada, execute o diagnóstico e os [ensaios de hardware](hardware.md). Confira os dois endereços, os sinais de promoção, rejeição e xeque, o encerramento pelo terminal e a operação com o display ausente. Os tempos de varredura e a integridade elétrica precisam ser medidos no conjunto real.
