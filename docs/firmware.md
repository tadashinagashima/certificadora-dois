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

O OLED mostra vez ou resultado, último SAN, estado da partida e situação do PGN. Atualiza até quatro caracteres por vez. Uma falha de transferência desativa a tela até o próximo boot. `status` informa `OLED online` ou `OLED offline`. [Ligação e endereço](hardware.md#oled-de-096)

Verde contínuo indica acompanhamento estável. Vermelho indica espera ou falha e o buzzer acompanha a rejeição. Na promoção, 1/2/3/4 pulsos verdes indicam Q/R/B/N. Após o encerramento, vermelho piscando indica PGN pendente e verde piscando indica gravação confirmada.

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

Para demonstrar mate, envie `new` e `sync`. Execute f2–f3, e7–e5, g2–g4 e d8–h4 usando `lift ORIGEM`, `place DESTINO` e `confirm` a cada lance. O terminal deve salvar o PGN com `1. f3 e5 2. g4 Qh4# 0-1`. Confira `Finished`, `Checkmate` e `saved=1` em `status`.

Para experimentar ruído na casa e2, iniciar com `new`, `sync`; enviar `raw ffff00000000efff`, `wait 25`, `raw ffff00000000ffff`, `wait 50`. A retirada curta é filtrada; `status` deve mostrar zero quadros de alterações. Para uma captura, manter cada alteração com `lift`/`place` ou oferecer leituras brutas suficientemente longas.

O receptor limita linhas a **96 caracteres**. Linha excedente, NUL, controles ou CR fora de CRLF são descartados até LF, sem executar sufixos. No ESP32, pausa maior que dois segundos numa linha parcial também provoca descarte. Erros no fluxo simulado invalidam sua tentativa; erros de console na placa não invalidam as leituras físicas independentes. Nenhum parser detecta toda corrupção que resulte em outro comando válido.

## Do sensor ao PGN

- **Acompanhamento.** Uma ocupação usa 64 bits (a1=0, h8=63). A primeira leitura cria a referência. Cada mudança estável registra casas retiradas e adicionadas, em ordem de observação. O limite é de 64 quadros de mudanças. Uma interrupção ou estouro invalida a tentativa até a reposição.
- **Interpretação.** O interpretador compara a tentativa com os movimentos legais da posição oficial. A captura exige observar o destino vazio antes de reocupá-lo. Em roque e en passant, as peças podem ser levantadas em ordens diferentes. A ocupação final precisa corresponder ao movimento completo.
- **Regras.** A [biblioteca THC](../firmware/xadrez/src/thc/LICENSE) é a cópia MIT do commit `aa217cb9b4d759cd7c07e11c98526b0187b0a64b` de `billforsternz/thc-chess-library`. O adaptador fornece FEN, lances legais, SAN e estado da posição. O controle encerra por mate, afogamento ou material insuficiente reconhecido. Tripla repetição e 50 lances são indicadores consultáveis.
- **Registro.** O registro gera o SAN na posição anterior, confere a FEN resultante e só acrescenta o lance se o contexto e o eventual resultado estiverem corretos. Lado e número do lance vêm da posição. O PGN aceita até 8192 bytes de texto de lances; ultrapassar o limite conserva o registro anterior e sinaliza falha. Partidas iniciadas em posição especial incluem FEN e SetUp. O PGN ativo termina em `*`.

| Estado do Controle | Como prosseguir |
| --- | --- |
| `AwaitingPosition` | Repor a ocupação oficial e aguardar uma leitura estável. |
| `Tracking` | Mover as peças e confirmar. |
| `AwaitingPromotion` | Escolher a peça e confirmar novamente. Alterar a ocupação cancela a seleção. |
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
| `o` | Consulta o endereço configurado para o OLED e informa ACK ou ausência de resposta. |
| `t` / `?` | Testa LEDs/buzzer por aproximadamente 120 ms / ajuda. |

O diagnóstico usa o [mesmo driver MCP23017 da aplicação](../firmware/BoardHardware.h) e exibe leituras brutas. Em falha, o driver descarta o quadro e mantém o MCP em RESET. Use `r` para retomar. Para testar texto no OLED, grave a aplicação principal.

## Verificar antes da entrega física

Antes de gravar a placa, compilar o simulador e o firmware e executar a demonstração acima. Experimentar uma captura, roque, en passant, as quatro promoções, confirmação repetida, jogada ilegal, `interrupt` e reposição. Conferir que posição e PGN não avançam em rejeições.

Para conferir recuperação, inicie o terminal com `--destino` apontando para um arquivo comum e encerre uma partida. Verifique a mensagem de erro e consulte `pgn`. Envie `pasta build/partidas`, depois `save`, e confira o conteúdo do novo arquivo. Outro `save` deve preservar a quantidade de arquivos. Repita a partida e confira que o primeiro PGN continua intacto.

Na bancada, execute o diagnóstico e os [ensaios de hardware](hardware.md). Confira os dois endereços, os sinais de promoção, rejeição e xeque, o encerramento pelo terminal e a operação com o display ausente. Os tempos de varredura e a integridade elétrica precisam ser medidos no conjunto real.
