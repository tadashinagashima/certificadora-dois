# Firmware

Há duas entradas para a mesma aplicação: [main.cpp](../firmware/principal/src/main.cpp) no ESP32 e [simulador/main.cpp](../firmware/simulador/main.cpp) no computador. Ambas usam `BoardApp`, os cinco módulos C++ e `PgnStorage`. `Hardware` implementa Wire/GPIO/SD; o simulador fornece ocupações, botões e um diretório local. Não há emulação de CPU, magnetismo ou sinais elétricos.

## Compilar o simulador

Requer CMake 3.20+ e compilador C++17. Os módulos e a aplicação compartilhada continuam em C++14; somente o acesso a arquivos do simulador usa C++17. Na raiz:

```powershell
cmake -S firmware -B firmware/build
cmake --build firmware/build --config Release
.\firmware\build\Release\tabuleiro_sim.exe --sd firmware/build/sd
```

No Windows, usar CMake no PATH ou o fornecido pelo Visual Studio com ferramentas C++. No Linux/macOS, executar `./firmware/build/tabuleiro_sim --sd firmware/build/sd`. O diretório é criado na primeira gravação; sem argumento, o padrão é `firmware/build/sd`, relativo ao diretório de execução. Usar uma instância por diretório de cartão.

## Compilar e gravar o ESP32

Alvo: ESP32 clássico/ESP-WROOM-32, DevKit V1 de 30 pinos; CPU 240 MHz, flash 4 MiB QIO/80 MHz, partições `default.csv`, sem PSRAM. PlatformIO Core **6.2.0**, pioarduino **55.03.312**, Arduino-ESP32 **3.3.12** / IDF **5.5.5**. O cabeçalho DIO do bootloader QIO é esperado. O [platformio.ini](../firmware/principal/platformio.ini) usa as cinco bibliotecas locais por `symlink://`.

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
& $python -m platformio device monitor --project-dir firmware/principal --port COM7
```

A configuração `esp32` lê o hardware real. O boot informa `mode=physical` e o estado inicial da partida. Há pilha de 32 KiB para `loopTask` e watchdog. Wi-Fi, Bluetooth e OLED não são inicializados. O console continua disponível para diagnóstico e operação assistida.

## Operação com três botões

O sistema inicia aguardando a posição padrão. Colocar todas as peças corretamente; verde contínuo indica aquisição estável e partida pronta. Reeds identificam presença, não identidade das peças.

| Ação | Gesto |
| --- | --- |
| Confirmar uma jogada | Apertar e soltar Confirmar depois de concluir a movimentação. |
| Escolher promoção | Após a primeira confirmação, apertar Promover: dama → torre → bispo → cavalo → dama. Confirmar novamente para aplicar. |
| Encerrar sem atribuir vencedor | Apertar e soltar Encerrar antes de dois segundos; resultado `*`. |
| Abandonar | Segurar Encerrar por pelo menos dois segundos e soltar; perde o lado que tem a vez. |
| Repetir gravação após falha | Com partida encerrada, apertar Encerrar. |
| Iniciar outra partida | Depois da gravação bem-sucedida, segurar Confirmar por dois segundos e soltar; repor a posição inicial. |

Os gestos são processados ao soltar, com debounce de 30 ms. Não apertar botões simultaneamente: a combinação é cancelada até todos serem soltos. Uma jogada rejeitada mantém a posição oficial; repor as peças, aguardar a retomada e refazer o lance. Levantar as peças, evitando arraste. Na captura, retirar a peça capturada de modo que o destino vazio seja observado antes de ocupá-lo.

| Sinal | Significado |
| --- | --- |
| Verde contínuo | Partida em acompanhamento, leitura estabilizada. |
| Vermelho contínuo | Aguardando reposição, aquisição instável/indisponível ou falha do registro; `status` distingue. |
| Vermelho por 400 ms e buzzer por 120 ms | Confirmação rejeitada. |
| Vermelho + verde lento | Promoção aguardando seleção. |
| Vermelho + 1/2/3/4 pulsos verdes a cada 2 s | Dama/torre/bispo/cavalo selecionado. |
| Vermelho durante gravação; depois vermelho piscando | Gravação em andamento ou ainda sem sucesso; verificar `status`. |
| Verde piscando | Partida encerrada e PGN gravado. Pode retirar o cartão. |

As indicações usam temporizadores, sem `delay` para pulsos. Partidas em andamento ficam em RAM; desligar ou reiniciar antes de salvar perde o registro. `new` no console descarta explicitamente a sessão, inclusive um PGN ainda não salvo. Exportar `pgn` antes se necessário.

## Aquisição e gravação

A [pinagem e os tempos](../firmware/board_config.h) são compartilhados com o diagnóstico. A aplicação tenta uma varredura completa a cada **25 ms**, I²C a 100 kHz, MCP `0x20`, uma coluna LOW e demais em alta impedância. Acomodação inicial de **300 µs**; `OLAT=0`, `GPPU=0` e pull-ups externos de 47 kΩ. Falha I²C descarta a varredura e mantém RESET LOW; há tentativa de reinicialização a cada segundo.

`BoardApp::sample()` aplica debounce de **25 ms por casa**, preservando quadros com alterações estáveis. A referência inicial exige estabilização das 64 casas. Uma confirmação exige leitura disponível, recente e sem mudanças pendentes no filtro. Um intervalo **maior que 100 ms** sem amostra completa invalida a tentativa, inclusive quando provocado por processamento ou saída Serial prolongada. Recuperar exige nova leitura estável igual à ocupação oficial. Esses valores são iniciais e devem ser ajustados com a montagem.

A varredura não é instantânea: mudanças muito rápidas, entre leituras ou mais curtas que o filtro, podem não ser observadas. Isso também vale para a vacância de uma captura. Software não recupera transições que os sensores não entregaram.

O PGN é salvo **somente ao encerrar** (mate, empate automático suportado, encerramento manual ou abandono), evitando acesso ao cartão durante jogadas. SPI inicia em 1 MHz. `PgnStorage` procura `GAME0001`…`GAME9999` livres, grava `.TMP`, fecha, compara todo o conteúdo e só então renomeia para `.PGN`. Arquivos existentes, inclusive `.TMP` de tentativas anteriores, não são sobrescritos. Não há formatação automática.

Falha mantém o PGN em RAM e sinaliza erro; inserir/verificar o cartão e usar Encerrar ou `save`. Repetir `save` após sucesso não cria duplicata. A leitura de verificação não garante integridade contra falta de energia no FAT/cartão; aguardar verde piscando antes de retirar. Se todos os nomes estiverem ocupados, arquivar os arquivos num computador antes de tentar novamente.

## Console e simulação

Serial do ESP32: 115200 baud, 8N1; ASCII, um comando por linha LF/CRLF. Esperar `DONE` antes de enviar outro. O simulador usa o mesmo protocolo em stdin/stdout, sem mensagem de boot. O ESP32 termina a inicialização com `READY protocol=1`.

| Comando comum | Efeito |
| --- | --- |
| `help` | Comandos disponíveis no ambiente atual. |
| `new` | Descarta a sessão e inicia a posição padrão. |
| `confirm` | Confirma pela aplicação compartilhada; não duplica lances aceitos. |
| `promote Q` / `R` / `B` / `N` | Seleciona promoção pendente; exige nova confirmação. |
| `finish` | Encerra com resultado `*` e solicita gravação. |
| `resign white` / `resign black` | Registra abandono da cor indicada e solicita gravação. |
| `save` | Repete gravação de uma partida encerrada ainda não salva. |
| `status` | Estado, FEN, ocupações, confiabilidade, encerramento, sinais e caminho salvo/erro SD. |
| `pgn` | Exporta o PGN em RAM entre `PGN_BEGIN` e `PGN_END`. |

`metrics` existe somente no ESP32 e informa, em bytes, o heap livre atual, o mínimo de heap livre desde o boot e a menor folga de pilha do `loopTask`. Serve para acompanhar o consumo de memória durante os ensaios.

| Comando exclusivo do simulador | Efeito |
| --- | --- |
| `sync` | Oferece a ocupação oficial ao filtro e avança 75 ms. |
| `observe HEX16` | Oferece ocupação de 16 dígitos hexadecimais ao filtro e avança 75 ms; bit 0=a1, bit 63=h8. |
| `lift e2` / `place e4` | Retira/coloca uma ocupação na última leitura estável e avança 75 ms. Exige leitura anterior. |
| `raw HEX16` | Altera somente a ocupação bruta, sem avançar tempo. |
| `wait MS` | Avança o relógio virtual em 0…60000 ms. |
| `buttons MASK` | Define botões brutos: Confirmar=1, Promover=2, Encerrar=4; 0 solta todos. Combinações somam bits. |
| `press confirm` / `promote` / `end` | Pressiona 75 ms e solta por 75 ms. |
| `hold confirm` / `promote` / `end` | Pressiona 2100 ms e solta por 75 ms. |
| `interrupt` | Interrompe a aquisição simulada; `raw`, `observe` ou `sync` voltam a oferecer amostras. |
| `new CENARIO` | Inicia uma posição conhecida, somente para experimentar situações especiais. |

O relógio virtual só avança nos comandos descritos: esperar no teclado não simula perda de leitura. Há ticks de até 5 ms e amostras a cada 25 ms de tempo virtual. Gravação no diretório usa arquivos reais. O simulador não reproduz a velocidade de execução do ESP32.

Cenários: `start`; `castle_w`/`castle_b`; `ep_w`/`ep_b`; `promotion_w`/`promotion_b`; `promotion_capture_w`/`promotion_capture_b`; `ambiguous`; `mate`; `stalemate`; `insufficient`; `busy`. As FENs estão em `SerialGame.cpp`; `status` mostra a posição. Não há parser público de FEN arbitrária. Cenários já encerrados também geram PGN automaticamente.

Demonstração completa, usando os botões simulados:

```text
new
sync
lift f2
place f3
press confirm
lift e7
place e5
press confirm
lift g2
place g4
press confirm
lift d8
place h4
press confirm
status
pgn
```

Esperado: estado `Finished`, caminho `saved=/GAME....PGN` e corpo `1. f3 e5 2. g4 Qh4# 0-1`. O arquivo no diretório de cartão contém o mesmo PGN mostrado no console.

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

## Verificar antes da entrega física

Antes de gravar a placa, compilar o simulador e o firmware e executar a demonstração acima. Experimentar uma captura, roque, en passant, as quatro promoções, confirmação repetida, jogada ilegal, `interrupt` e reposição. Conferir que posição e PGN não avançam em rejeições.

Para conferir falha de armazenamento, iniciar o simulador com `--sd` apontando para um arquivo comum, encerrar uma partida e verificar `sd_error=1`, `saved=-` e PGN preservado. Corrigir o destino para um diretório e enviar `save`; o PGN deve ser gravado sem reiniciar a partida. Conferir que arquivos anteriores permanecem intactos.

Na montagem, executar primeiro o diagnóstico e os ensaios de [hardware](hardware.md). Depois verificar os botões, promoção por pulsos, partidas completas, cartão ausente/removido/cheio, captura rápida, interrupção I²C e recuperação. Ler os PGNs num computador. Compilação e simulação não validam magnetismo, alimentação, fiação, temporização real ou estabilidade prolongada.
