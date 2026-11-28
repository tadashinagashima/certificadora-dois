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

Na abertura, Enter escolhe dois jogadores, 4 escolhe máquina e 2 abre a lista de tutoriais. No modo local, informe os nomes das brancas e pretas ou pressione Enter para usar as cores. Os nomes seguem para o PGN e o letreiro. A fonte do OLED aceita até 24 caracteres ASCII. O terminal simplifica acentos.

Monte a posição desenhada com A1 embaixo à esquerda. R, D, T, B, C e P representam rei, dama, torre, bispo, cavalo e peão. Maiúsculas são brancas. A placa reconhece a ocupação inicial e acende o verde. A cor e a identidade das peças devem ser conferidas por quem as monta.

**Mova primeiro, confirme depois.** No prompt de partida, uma linha vazia envia `confirm`. Termine o gesto e deixe as peças centradas antes de pressionar Enter. Pode pausar entre rei e torre no roque. Na captura, a retirada da peça adversária precisa deixar a casa visivelmente livre para a varredura. Na promoção, substitua o peão pela peça desejada e pressione Enter. Quando solicitado, envie `promote Q`, `promote R`, `promote B` ou `promote N` e confirme com outro Enter.

Se o lance for ilegal, ambíguo ou diferente do indicado, o buzzer soa e o LED vermelho acompanha o bloqueio. **Reponha todas as peças na última posição exibida**, inclusive a peça capturada. O painel lista as casas a ocupar ou liberar. Quando a montagem voltar a coincidir com a referência, o verde libera uma tentativa nova. O mesmo procedimento vale para perda de aquisição.

| Comando no terminal | Efeito |
| --- | --- |
| Enter / `confirm` | Solicita a validação do lance montado. |
| 1 / `status`, 3 / `pgn` | Consulta posição e sensores ou exibe o PGN. `tecnico` inclui FEN, leituras e diagnóstico dos OLEDs. |
| 4 / `maquina`, 5 / `local` | Escolhe máquina de pretas ou dois jogadores. A troca entre essas duas modalidades preserva a posição. Ao sair de uma lição, abre uma partida inicial. |
| 6 / `nomes` | Edita os nomes dos jogadores. |
| 2 / `tutorial` | Lista as lições. Também aceita `tutorial roque`, `tutorial promocao` e `tutorial en-passant`. |
| `repetir` | Remonta a lição atual do primeiro passo. No protocolo, `tutorial restart`. |
| `new` | Inicia outra partida e descarta a posição ativa. |
| `finish` / `resign white` / `resign black` | Encerra com `*` ou registra abandono e oferece o PGN ao computador. |
| `pasta CAMINHO` / `save` | Corrige o destino local e repete a gravação pendente. O arquivo pendente deve ser salvo antes de trocar de sessão. |
| `oled 100` / `oled 400` | Na placa, seleciona a frequência de Wire1 em kHz e reinicializa os OLEDs. O boot começa em 100 kHz. |
| `metrics` | Na placa, consulta heap e folga de pilha em bytes. |
| `?` / `sair` | Mostra a ajuda ou fecha a conexão. |

A máquina escolhe um lance com `ChessEvaluation::GenLegalMoveListSorted` da THC. Execute a origem entre colchetes e o destino entre parênteses, incluindo a retirada da peça adversária se houver captura. Se a sugestão incluir promoção, use a peça anunciada. Enter confirma a resposta física. A lição de promoção pede também a escolha explícita da peça.

### Roteiro de apresentação

Cada linha abaixo começa em uma sessão independente. No simulador, `sync` monta a posição pedida. Para mover, use `lift CASA` e `place CASA`. Enter em uma linha vazia ou `confirm` conclui cada lance. Na placa, faça as mesmas ações com as peças. O desenho do terminal mostra a posição oficial mesmo durante a reposição.

| Cena e montagem | Ações | O que mostrar |
| --- | --- | --- |
| Dois jogadores, posição inicial | Leve E2 a E5 e pressione Enter. Volte a E2, aguarde verde, mova E2 a E4, espere e só então pressione Enter. | A rejeição conserva a vez das brancas e o PGN vazio. O lance válido aparece como `1. e4 *`. |
| Máquina, mesma partida | Digite `maquina`. Execute a resposta preta destacada e confirme. Digite `finish`. | O tabuleiro muda de vez após a confirmação. Abra o PGN no caminho informado. |
| `tutorial roque`. Reis em E1/E8 e torres em A1/H1/A8/H8 | Mova E1–G1 **e** H1–F1, depois Enter. Mova E8–C8 **e** A8–D8, depois Enter. | Rei e torre compartilham um lance. O lado do roque muda e o PGN contém `1. O-O O-O-O *`. |
| `tutorial promocao`. Brancas com rei E1 e peão A7, pretas com rei H8 | Substitua o peão A7 por uma torre branca em A8. Enter, `promote R`, Enter. | A torre recém-promovida dá xeque na oitava fileira. Confira `a8=R+`. |
| `tutorial en-passant`. Brancas com rei E1 e peão E5, pretas com rei A8 e peão D7 | Avance D7–D5 e confirme. Agora mova E5–D6, retire o peão de D5 e confirme. | A captura termina em uma casa antes vazia. O PGN contém `1... d5 2. exd6 *`. Use `repetir` para remontar. |

É possível trocar ou repetir uma lição em andamento. Ao completar o último passo, o terminal salva o PGN com o nome da lição, a FEN inicial e SetUp. Para abrir uma lição depois de jogar uma partida comum com lances registrados, encerre e salve a partida.

### Apresentação nos OLEDs

A tela esquerda (0x3D) sinaliza a vez, a lição ou a reposição. A direita (0x3C) mostra coordenadas, progresso da montagem e ação física. Ao aceitar o lance, as duas telas compartilham a cena. No roque viajam dois sprites. No en passant desaparece o peão ultrapassado. Na promoção aparece a letra da nova peça. Xeque, captura e mate têm indicações próprias.

O renderizador prepara um quadro de 128×64 por tela a partir do mesmo instante. Cada alvo fica congelado até o envio dos trechos alterados, em blocos de até 64 bytes intercalados com a aquisição. O cache só avança após a confirmação da transmissão. A meta é um quadro a cada 40 ms (25 FPS). Quadros com mais pixels alterados podem levar mais tempo, especialmente a 100 kHz. As duas telas usam timeout de 12 ms por transação; o MCP usa seu próprio controlador.

A placa consulta cada painel a cada três segundos, inclusive com a matriz indisponível. Após falha de comunicação, o painel passa por inicialização e repintura completa antes de ligar a imagem. Em `tecnico`, `OLED` corresponde à direita e `OLED_TABULEIRO` à esquerda. `OLED_HZ` informa a frequência configurada e `I2C_OLED` os códigos em caso de falha. Para forçar uma repintura, use também `oled 100` ou `oled 400`.

## Aquisição e gravação

A [pinagem e os tempos](../firmware/board_config.h) são compartilhados com o diagnóstico. A aplicação tenta uma varredura completa a cada **25 ms**, I²C a 100 kHz, MCP `0x20`, uma coluna LOW e demais em alta impedância. Acomodação inicial de **300 µs**; `OLAT=0`, `GPPU=0` e pull-ups externos de 47 kΩ. Falha I²C do MCP descarta a varredura e mantém RESET LOW; há tentativa de reinicialização a cada segundo.

`BoardApp::sample()` aplica debounce de **25 ms por casa**, preservando quadros com alterações estáveis. A referência inicial exige estabilização das 64 casas. Na placa, o comando de confirmação solicita mais uma varredura. A confirmação exige leitura disponível, recente e sem mudanças pendentes no filtro. Um intervalo **maior que 100 ms** sem amostra completa invalida a tentativa, inclusive quando provocado por processamento ou saída Serial prolongada. Recuperar exige nova leitura estável igual à ocupação oficial. Esses valores são iniciais e devem ser ajustados com a montagem.

A varredura não é instantânea: mudanças muito rápidas, entre leituras ou mais curtas que o filtro, podem não ser observadas. Isso também vale para a vacância de uma captura. Software não recupera transições que os sensores não entregaram.

O terminal salva automaticamente ao concluir uma lição e no encerramento manual, abandono, mate, afogamento ou material insuficiente reconhecido. Por padrão, cria `build/partidas/` no diretório de execução. Use `--destino CAMINHO` na abertura para escolher outra pasta. O nome `partida-<identificador>.pgn` combina um identificador do boot com o número da sessão. Duas partidas com os mesmos lances recebem nomes distintos.

O computador verifica tamanho e CRC-32 do PGN, grava um temporário na pasta de destino, sincroniza e fecha o arquivo. Publica por renomeação exclusiva no Windows e por hard link em Linux/macOS (escolha um volume com suporte a links físicos). Se uma confirmação se perder, o terminal reconhece o arquivo idêntico na mesma pasta. Um arquivo preexistente com conteúdo diferente é preservado e a falha é informada. Use uma pasta local gravável e com espaço livre.

Em erro de escrita, mantenha o ESP32 ligado, corrija a pasta e envie `save`. Se houver queda de comunicação, reabra o terminal usando a mesma pasta. O `status` inicial recupera a pendência se o adaptador mantiver a placa ligada e sem reset. Partidas ativas e arquivos ainda pendentes ficam na RAM. Um reset, a retirada do cabo que fornece energia ou o encerramento do processo do simulador descarta essa memória.

## Protocolo e simulação

USB serial a 115200 baud, 8N1, ASCII nos comandos e LF/CRLF. Cada comando recebe uma resposta encerrada por uma linha `DONE`. O ESP32 emite `READY protocol=3` no boot. Uma conexão aberta depois do boot pode enviar `hello` e verificar `PROTOCOL 3`. O simulador usa stdin/stdout e responde ao mesmo `hello`.

A versão 3 exige confirmação explícita. Atualize terminal e firmware juntos. O protocolo bruto usa o comando `confirm` para a linha vazia do terminal Python.

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

O relógio virtual avança somente por comando. O filtro recebe amostras a cada 25 ms e o simulador executa ticks de até 5 ms. Uma espera arbitrariamente longa preserva o lance em aberto até a confirmação. O terminal grava arquivos reais.

Cenários: `start`; `castle_w`/`castle_b`; `ep_w`/`ep_b`; `promotion_w`/`promotion_b`; `promotion_capture_w`/`promotion_capture_b`; `ambiguous`; `mate`; `stalemate`; `insufficient`; `busy`. As FENs estão em `SerialGame.cpp`; `status` mostra a posição. Não há parser público de FEN arbitrária. Cenários já encerrados também geram PGN automaticamente.

Para demonstrar mate, envie `new` e `sync`. Execute f2–f3, e7–e5, g2–g4 e d8–h4 com `lift ORIGEM`, `place DESTINO` e Enter após cada par. O terminal deve salvar o PGN com `1. f3 e5 2. g4 Qh4# 0-1`. Confira `Finished`, `Checkmate` e `saved=1` em `status`.

Para experimentar ruído na casa e2, iniciar com `new`, `sync`; enviar `raw ffff00000000efff`, `wait 25`, `raw ffff00000000ffff`, `wait 50`. A retirada curta é filtrada; `status` deve mostrar zero quadros de alterações. Para uma captura, manter cada alteração com `lift`/`place` ou oferecer leituras brutas suficientemente longas.

O receptor limita linhas a **96 caracteres**. Linha excedente, NUL, controles ou CR fora de CRLF são descartados até LF, sem executar sufixos. No ESP32, pausa maior que dois segundos numa linha parcial também provoca descarte. Erros no fluxo simulado invalidam sua tentativa; erros de console na placa não invalidam as leituras físicas independentes. Nenhum parser detecta toda corrupção que resulte em outro comando válido.

## Do sensor ao PGN

- **Acompanhamento.** Uma ocupação usa 64 bits (a1=0, h8=63). A primeira leitura cria a referência. Cada mudança estável registra casas retiradas e adicionadas, em ordem de observação. O limite é de 64 quadros de mudanças. Uma interrupção ou estouro invalida a tentativa até a reposição.
- **Interpretação.** O interpretador compara a tentativa com os movimentos legais da posição oficial. A captura exige observar o destino vazio antes de reocupá-lo. No roque, a ocupação final inclui rei e torre. Em en passant, o peão capturado pode sair antes ou depois de o outro chegar ao destino. A ocupação final precisa corresponder ao movimento completo.
- **Regras.** A [biblioteca THC](../firmware/xadrez/src/thc/LICENSE) é a cópia MIT do commit `aa217cb9b4d759cd7c07e11c98526b0187b0a64b` de `billforsternz/thc-chess-library`. O adaptador fornece FEN, lances legais, SAN e estado da posição. O controle encerra por mate, afogamento ou material insuficiente reconhecido. Tripla repetição e 50 lances são indicadores consultáveis.
- **Registro.** O registro gera o SAN na posição anterior, confere a FEN resultante e só acrescenta o lance se o contexto e o eventual resultado estiverem corretos. Lado e número do lance vêm da posição. O PGN aceita até 8192 bytes de texto de lances; ultrapassar o limite conserva o registro anterior e sinaliza falha. Partidas iniciadas em posição especial incluem FEN e SetUp. O PGN ativo termina em `*`.

| Estado do Controle | Como prosseguir |
| --- | --- |
| `AwaitingPosition` | Repor a ocupação oficial e aguardar uma leitura estável. |
| `Tracking` | Mover as peças. Pressionar Enter ao concluir o gesto. |
| `AwaitingPromotion` | Escolher a peça com `promote` e confirmar com Enter. Alterar a ocupação cancela a seleção. |
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
