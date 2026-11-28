# Firmware

O [firmware da placa](../firmware/principal/src/main.cpp) e o [simulador](../firmware/simulador/main.cpp) compartilham `SerialGame`, `BoardApp`, o filtro de sensores e os cinco módulos C++14. O [terminal Python](../firmware/terminal.py) usa o mesmo protocolo para controlar os dois e gravar PGNs no computador.

## Compilar o simulador

Siga os [comandos de compilação e demonstração](../README.md#demonstração-sem-placa). O executável C++14 também aceita o protocolo bruto em stdin/stdout. Use o terminal Python para gravar os PGNs.

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

Na abertura de uma partida nova, Enter seleciona dois jogadores; 4 seleciona a máquina. No modo local, digite primeiro o nome das brancas e depois o das pretas. Enter pula o nome e a tela usa a cor. Use 6 ou `nomes` para editar durante a partida. Cada nome aceita até 24 caracteres; o terminal simplifica acentos para a fonte do OLED. Os nomes informados também seguem nos campos White e Black do PGN.

O painel reúne a posição e a próxima ação lado a lado. **4** (`maquina`) ativa o adversário de pretas e **5** (`local`) volta ao modo de dois jogadores, preservando a partida. A seleção e a sugestão ficam no ESP32 durante a conexão e após reabrir o terminal. O boot começa no modo local.

No modo máquina, o adaptador usa a lista de lances legais ordenada por `ChessEvaluation::GenLegalMoveListSorted`, da biblioteca THC já incluída. Ela pontua o resultado imediato por material, posição e mate. A sugestão fica no OLED da partida e no terminal até o tabuleiro reconhecer a execução. Se pousar na casa errada, leve a peça diretamente à casa indicada. No roque, mova primeiro o rei e depois a torre. Uma torre movida sozinha pode constituir um lance legal. Em promoção da máquina, use a peça indicada depois de `=`. Pelo protocolo bruto, use `mode cpu`, `mode local` e `status`.

Na janela interativa, digite um atalho e pressione Enter: `1` consulta a posição, `3` exibe o PGN e `?` abre a ajuda. O painel mostra a vez, o último lance e as casas que diferem da posição esperada. Para ver FEN e as respostas originais da placa, use `tecnico`.

O painel desenha o tabuleiro na abertura, na consulta de posição e automaticamente após cada jogada. A1 fica no canto inferior esquerdo. As letras R, D, T, B, C e P representam rei, dama, torre, bispo, cavalo e peão. Maiúsculas indicam as brancas e minúsculas, as pretas. Um lance rejeitado preserva o desenho da última posição aceita.

Na vez da máquina, procure a peça entre colchetes azuis e o destino entre parênteses amarelos. Um destino vazio aparece como `(+)`; em uma captura comum, os parênteses contêm a peça a retirar. As coordenadas também aparecem ao lado do desenho.

No CMD, o ciano destaca comandos e leituras dos sensores. O verde indica sucesso, o amarelo pede atenção e o vermelho indica erro ou lance ilegal. Ao redirecionar a saída para um arquivo, o terminal grava texto simples.

A conexão recupera o estado atual da placa. Posicione as peças iniciais e consulte `status`. Em `Tracking`, o ESP32 aceita o lance após 450 ms de ocupação estável. Se um ímã acionar duas casas, centralize a peça no destino. O firmware continua acompanhando o mesmo gesto. Uma pausa de 450 ms sobre outra casa que também complete um lance legal pode registrar esse lance.

Em captura, retire a peça adversária e pouse a sua no destino. Se a casa ficou ocupada durante toda a troca ou se duas capturas parecem possíveis, levante e recoloque **apenas a peça no destino**. O terminal mostra as casas que diferem da posição oficial. A vez e o PGN só avançam quando a leitura identifica o lance.

| Comando | Efeito |
| --- | --- |
| `promote Q` / `promote R` / `promote B` / `promote N` | Seleciona dama, torre, bispo ou cavalo. A escolha conclui a promoção. |
| `status` / `pgn` | Mostra estado, FEN, ocupações, sinais, último SAN e xeque / exibe o PGN em RAM. |
| `finish` / `resign white` / `resign black` | Encerra com resultado `*` / registra abandono da cor indicada. |
| `new` | Descarta a sessão ativa e reinicia. Um PGN encerrado pendente de gravação bloqueia o comando. |
| `save` | Repete a transferência pendente. Após sucesso, a placa responde `OK saved`. |
| `pasta CAMINHO` | Seleciona no computador o destino da gravação. Aceita espaços no caminho. |
| `ajuda` / `sair` | Lista os comandos / fecha a conexão. |
| `metrics` | Somente na placa: heap livre, mínimo de heap e menor folga de pilha do loop em bytes. |

Ao montar a posição, o OLED conta as casas esperadas que já estão ocupadas e avisa se houver peças extras. Durante a partida, a faixa superior indica a vez e o último lance aparece em tamanho duplo. A tela também indica xeque, rejeição, resultado e gravação do PGN. Cada atualização transmite até quatro blocos de 8×8 pixels para intercalar a escrita com a leitura da matriz.

Ao levantar uma peça, a primeira tela mostra a casa de origem. Ao pousar em uma casa antes vazia, mostra a prévia (por exemplo, E2>E4). Alterações complexas podem mostrar REVISAR. Use `status` no terminal para consultar as diferenças em relação à posição oficial; o ESP32 atualiza o registro e a vez ao reconhecer o lance completo.

A segunda tela (0x3D) mostra o nome de quem joga em letras de 32 pixels de altura. O nome entra pela direita, faz uma pausa para leitura e segue para a esquerda. Se o nome estiver em branco, aparece a cor. Na vez da máquina, o letreiro indica as casas de origem e destino.

Quando a placa aceita um lance, a tela exibe uma animação curta. No lance comum, um peão salta sobre a notação do lance. A captura acende faíscas, o xeque mostra um raio e o mate mantém uma coroa com fogos. Se uma captura também der xeque ou mate, prevalece o aviso de xeque ou mate.

O comando `status` informa a disponibilidade de cada tela em `OLED` (partida, 0x3C) e `OLED_TABULEIRO` (animação, 0x3D). Uma tela offline pode manter a última imagem. Com a matriz operacional, a placa volta a consultar cada endereço offline a cada três segundos e redesenha a partida atual quando a tela responde. O comando `tecnico` mostra também o último código de comunicação de cada tela em `I2C_OLED` durante a falha. [Ligação e endereço](hardware.md#oled-de-096)

Verde contínuo indica acompanhamento estável. Vermelho indica espera ou falha e o buzzer acompanha a rejeição. Na promoção, 1/2/3/4 pulsos verdes indicam Q/R/B/N. Após o encerramento, vermelho piscando indica PGN pendente e verde piscando indica gravação confirmada.

## Aquisição e gravação

A [pinagem e os tempos](../firmware/board_config.h) são compartilhados com o diagnóstico. A aplicação tenta uma varredura completa a cada **25 ms**, I²C a 100 kHz, MCP `0x20`, uma coluna LOW e demais em alta impedância. Acomodação inicial de **300 µs**; `OLAT=0`, `GPPU=0` e pull-ups externos de 47 kΩ. Falha I²C do MCP descarta a varredura e mantém RESET LOW; há tentativa de reinicialização a cada segundo.

`BoardApp::sample()` aplica debounce de **25 ms por casa**, preservando quadros com alterações estáveis. A referência inicial exige estabilização das 64 casas. O registro automático exige leitura disponível, recente e sem mudanças pendentes no filtro. Um intervalo **maior que 100 ms** sem amostra completa invalida a tentativa, inclusive quando provocado por processamento ou saída Serial prolongada. Recuperar exige nova leitura estável igual à ocupação oficial. Esses valores são iniciais e devem ser ajustados com a montagem.

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

No terminal Python, `lift`, `place`, `observe` e `promote` também avançam o relógio em 500 ms para simular a pausa física. No protocolo bruto, use `wait 500` após o pouso. O relógio virtual só avança por comando. Há ticks de até 5 ms e amostras a cada 25 ms de tempo virtual. O terminal grava arquivos reais.

Cenários: `start`; `castle_w`/`castle_b`; `ep_w`/`ep_b`; `promotion_w`/`promotion_b`; `promotion_capture_w`/`promotion_capture_b`; `ambiguous`; `mate`; `stalemate`; `insufficient`; `busy`. As FENs estão em `SerialGame.cpp`; `status` mostra a posição. Não há parser público de FEN arbitrária. Cenários já encerrados também geram PGN automaticamente.

Para demonstrar mate, envie `new` e `sync`. Execute f2–f3, e7–e5, g2–g4 e d8–h4 usando `lift ORIGEM`, `place DESTINO` no terminal Python. O terminal deve salvar o PGN com `1. f3 e5 2. g4 Qh4# 0-1`. Confira `Finished`, `Checkmate` e `saved=1` em `status`.

Para experimentar ruído na casa e2, iniciar com `new`, `sync`; enviar `raw ffff00000000efff`, `wait 25`, `raw ffff00000000ffff`, `wait 50`. A retirada curta é filtrada; `status` deve mostrar zero quadros de alterações. Para uma captura, manter cada alteração com `lift`/`place` ou oferecer leituras brutas suficientemente longas.

O receptor limita linhas a **96 caracteres**. Linha excedente, NUL, controles ou CR fora de CRLF são descartados até LF, sem executar sufixos. No ESP32, pausa maior que dois segundos numa linha parcial também provoca descarte. Erros no fluxo simulado invalidam sua tentativa; erros de console na placa não invalidam as leituras físicas independentes. Nenhum parser detecta toda corrupção que resulte em outro comando válido.

## Do sensor ao PGN

- **Acompanhamento.** Uma ocupação usa 64 bits (a1=0, h8=63). A primeira leitura cria a referência. Cada mudança estável registra casas retiradas e adicionadas, em ordem de observação. O limite é de 64 quadros de mudanças. Uma interrupção ou estouro invalida a tentativa até a reposição.
- **Interpretação.** O interpretador compara a tentativa com os movimentos legais da posição oficial. A captura exige observar o destino vazio antes de reocupá-lo. No roque, mova o rei primeiro. Em en passant, o peão capturado pode sair antes ou depois de o outro chegar ao destino. A ocupação final precisa corresponder ao movimento completo.
- **Regras.** A [biblioteca THC](../firmware/xadrez/src/thc/LICENSE) é a cópia MIT do commit `aa217cb9b4d759cd7c07e11c98526b0187b0a64b` de `billforsternz/thc-chess-library`. O adaptador fornece FEN, lances legais, SAN e estado da posição. O controle encerra por mate, afogamento ou material insuficiente reconhecido. Tripla repetição e 50 lances são indicadores consultáveis.
- **Registro.** O registro gera o SAN na posição anterior, confere a FEN resultante e só acrescenta o lance se o contexto e o eventual resultado estiverem corretos. Lado e número do lance vêm da posição. O PGN aceita até 8192 bytes de texto de lances; ultrapassar o limite conserva o registro anterior e sinaliza falha. Partidas iniciadas em posição especial incluem FEN e SetUp. O PGN ativo termina em `*`.

| Estado do Controle | Como prosseguir |
| --- | --- |
| `AwaitingPosition` | Repor a ocupação oficial e aguardar uma leitura estável. |
| `Tracking` | Mover as peças. A mudança de vez indica que o lance foi aceito. |
| `AwaitingPromotion` | Escolher a peça com `promote`. Alterar a ocupação cancela a seleção. |
| `Finished` | Aguardar a gravação e usar `new` para jogar de novo. |

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
| `o` | Consulta 0x3C e 0x3D e informa ACK ou ausência de resposta em cada endereço. |
| `t` / `?` | Testa LEDs/buzzer por aproximadamente 120 ms / ajuda. |

O diagnóstico usa o [mesmo driver MCP23017 da aplicação](../firmware/BoardHardware.h) e exibe leituras brutas. Em falha, o driver descarta o quadro e mantém o MCP em RESET. Use `r` para retomar. Para testar texto no OLED, grave a aplicação principal.

## Verificar antes da entrega física

Antes de gravar a placa, compile o simulador e o firmware e execute a demonstração acima. Experimente captura, roque, en passant, as quatro promoções, uma jogada ilegal e uma interrupção seguida de reposição. Levantar uma peça e devolvê-la à origem deve preservar a vez e o PGN.

Para conferir recuperação, inicie o terminal com `--destino` apontando para um arquivo comum e encerre uma partida. Verifique a mensagem de erro e consulte `pgn`. Envie `pasta build/partidas`, depois `save`, e confira o conteúdo do novo arquivo. Outro `save` deve preservar a quantidade de arquivos. Repita a partida e confira que o primeiro PGN continua intacto.

Na bancada, execute o diagnóstico e os [ensaios de hardware](hardware.md). Confira o MCP e os dois endereços dos OLEDs, os sinais de promoção, rejeição e xeque, o encerramento pelo terminal e a operação com o display ausente. Os tempos de varredura e a integridade elétrica precisam ser medidos no conjunto real.
