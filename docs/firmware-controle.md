# Controle da partida

O [GameController](../firmware/controle/src/GameController.h) coordena o [acompanhamento](firmware-acompanhamento.md), a [interpretação](firmware-interpretacao.md) e o [adaptador THC](firmware-xadrez.md). Mantém a posição oficial e a tentativa física em instâncias próprias, em C++14 independente de Arduino. A confirmação é um comando lógico; aquisição, botões, sinalização e registro PGN continuam separados conforme RNF02.

Implementa o fluxo lógico de RF03–RF06 e a seleção de RF08, além do encerramento de RF09–RF10 nas condições suportadas pelo adaptador. A leitura do botão, a ciclagem visual das opções e a sinalização de RF07 pertencem à interface futura.

## 1. Estado e comandos

| Estado | Comportamento |
| --- | --- |
| `AwaitingPosition` | Aguarda uma nova observação completa com ocupação igual à posição oficial para iniciar a tentativa. |
| `Tracking` | Registra alterações e aceita confirmação. |
| `AwaitingPromotion` | Aguarda seleção explícita da peça e nova confirmação; a posição oficial permanece anterior ao lance. |
| `Finished` | Bloqueia novas alterações até `reset()`. |

O construtor padrão estabelece a posição inicial. O construtor que recebe `const ChessAdapter&` copia integralmente uma posição preparada, inclusive o histórico de repetição; não recarrega FEN nem tenta deduzir peças pelos sensores. Ambos aguardam uma leitura compatível antes da primeira tentativa, exceto quando a posição fornecida já apresenta encerramento automático. `reset()` inicia outra partida na posição padrão, elimina pendências e exige nova leitura.

`observe(occupancy, timestampMs)` recebe os mesmos quadros completos de 64 bits do MoveTracker, com `a1=0` e `h8=63`. Em `AwaitingPosition`, uma leitura compatível descarta o histórico anterior e inicia uma tentativa vazia. Leituras divergentes não alteram a posição oficial. Nos demais estados ativos, as alterações são acumuladas até a confirmação.

`markInputInterrupted()` informa perda de continuidade e retorna a `AwaitingPosition`, cancelando eventual seleção. O histórico e as causas de perda permanecem disponíveis até uma nova leitura compatível. O estouro do histórico continua explícito no snapshot e impede a interpretação ao confirmar. Uma interrupção antes da primeira leitura não representa perda de um histórico inexistente; após a primeira leitura válida, o bloqueio pode ser apenas a divergência da ocupação.

`position()` devolve referência **const** ao adaptador, sem permitir aplicação externa de lances. `attempt()` devolve uma cópia do snapshot. `state()`, `selectedPromotion()` e `ending()` permitem à futura interface consultar a situação sem alterar a partida. Chamadas sobre a mesma instância devem ser serializadas, fora de interrupções; o módulo não cria tarefas, filas ou sincronização.

## 2. Confirmação e promoção

O chamador entrega as observações completas disponíveis antes de chamar `confirm()`. O Controle não lê sensores nem decide se a aquisição está estável. A confirmação interpreta o snapshot atual contra a posição oficial; se houver um único movimento identificado, chama `ChessAdapter::apply()`, que o revalida. Somente o sucesso dessa aplicação produz `Accepted` e atualiza a posição. O histórico então é descartado, usando a ocupação já observada como referência do próximo lance.

| Resultado da confirmação | Efeito |
| --- | --- |
| `Accepted` | Lance aplicado; retorna o `Move` aceito e verifica encerramento automático. |
| `NoMove` | Ocupação voltou à inicial ou não mudou; descarta a tentativa, mantém a posição e continua em `Tracking`. |
| `NoLegalMatch` / `Ambiguous` | Preserva posição e histórico; aguarda uma nova leitura compatível com a posição oficial. |
| `PromotionRequired` | Mantém a tentativa e entra em `AwaitingPromotion`, sem promoção padrão. |
| `UnreliableInput` | Tentativa sem leitura ou com perda de confiabilidade; aplicação bloqueada. |
| `PositionMismatch` | Tentativa ainda sem referência compatível com a posição oficial; aplicação bloqueada. |
| `ApplyFailed` | Defesa para falha da revalidação final; preserva a posição e aguarda leitura compatível. |
| `GameFinished` | Partida encerrada; não altera estado nem histórico. |

Somente `Accepted` preenche o campo `move`; nos demais resultados ele permanece `Move{}`, com casas inválidas. `NoMove` informa uma confirmação sem lance e não deve ser tratado como lance para registro. Repetir a confirmação após um lance aceito não o reaplica. Em `AwaitingPosition`, confirmar nunca inicia uma tentativa: retorna `UnreliableInput` se o snapshot não for confiável e `PositionMismatch` caso contrário.

Após `PromotionRequired`, `selectPromotion()` aceita `Queen`, `Rook`, `Bishop` ou `Knight`. A seleção pode ser trocada, mas não aplica o lance. `None`, valores desconhecidos e seleções fora desse estado retornam falso sem modificar a escolha. Uma nova `confirm()` reinterpreta a tentativa atual com a escolha e aplica somente o movimento legal correspondente. Confirmar sem seleção mantém a promoção pendente.

Uma leitura idêntica durante a escolha conserva a seleção. Qualquer mudança de ocupação a cancela e volta a `Tracking`, continuando o histórico: não é usado um snapshot congelado. Assim, se a peça for retirada para substituição depois da escolha, o jogador deve completar a colocação e repetir confirmação, seleção e confirmação. A ordem prática recomendada é completar a movimentação/substituição física antes da escolha. A ciclagem do botão poderá chamar `selectPromotion()` com cada opção; sua implementação não faz parte deste módulo.

## 3. Rejeição e encerramento

A política de retomada é deliberadamente simples: após rejeição ou interrupção, aguardar uma **nova observação** com ocupação igual à oficial e só então descartar a tentativa anterior. Não se inicia outra tentativa na ocupação divergente, nem se reconcilia uma movimentação irregular com a posição lógica. Igualdade dos 64 bits não identifica peças ou cores; pressupõe a reposição correta e o procedimento de uso já documentado. O limite de 64 quadros e a exigência de observar a vacância em capturas permanecem os dos módulos existentes.

Após cada lance aceito, e ao construir a partida, o Controle consulta `status()`:

- Xeque-mate encerra com vitória do lado oposto a `sideToMove`.
- Afogamento e `insufficientMaterial` encerram como empate.
- Os indicadores de 50 lances e tripla repetição permanecem disponíveis na posição, mas não provocam encerramento automático. Reclamações de empate não foram implementadas nesta etapa.

`resign(player)` encerra por abandono da cor informada, com vitória da outra cor. `finish()` encerra manualmente com resultado `Unfinished`, sem atribuir vencedor ou empate. Ambos dispensam outro lance e cancelam seleção pendente sem aplicá-la. A interface será responsável por identificar quem solicitou o abandono. Comandos de encerramento repetidos, cor inválida e seleção após término retornam falso. Em `Finished`, observações e interrupções são ignoradas; posição e última tentativa continuam consultáveis.

As limitações de material insuficiente, repetição e demais regras são as do [adaptador](firmware-xadrez.md#2-condições-de-encerramento). Não foram acrescentadas regras próprias nem adjudicação de cinco repetições ou 75 lances.

## 4. Compilação e testes

Na raiz, com CMake/MSVC do Visual Studio 2022 e Arduino CLI:

```powershell
& .\firmware\controle\validar.ps1
# Somente testes nativos:
& .\firmware\controle\validar.ps1 -NativeOnly
```

Os parâmetros `-CMake` e `-ArduinoCli` permitem outros caminhos. O script reaproveita os projetos CMake dos três módulos, verifica os hashes THC e compila o Controle e seus testes em C++14 Release com `/W4 /WX /permissive-`. A compilação ESP32 usa as quatro bibliotecas locais por `--library`, core Arduino-ESP32 3.3.12 e FQBN `esp32:esp32:esp32`, sem instalação global das bibliotecas.

Se o ambiente redirecionar o perfil do Windows, `ARDUINO_DIRECTORIES_DATA` deve apontar para a instalação existente. `ARDUINO_BUILD_CACHE_PATH` pode apontar para `firmware/controle/build/core-cache`. Intermediários ficam em `build/`, já excluído localmente; logs, JUnit e hashes em `validation/` constituem evidência reproduzível. Cada execução substitui o relatório anterior e informa se compilou para ESP32.

Os [29 grupos do Controle](../firmware/controle/tests/controller_tests.cpp) verificam referência inicial, atualização somente por confirmação, repetição de confirmação, sequência de lances, capturas, quatro roques, *en passant*, promoção de ambas as cores, escolha e mudança física durante promoção, rejeição, ambiguidade, interrupção, estouro, reinício, preservação de repetição e encerramento. As ocupações esperadas são construídas pelos casos, sem usar a aplicação do Controle para produzir a entrada. As verificações ficam ativas em Release. O mesmo projeto executa os 64 testes existentes, incluindo `TestInternals()` da THC.

A [sonda ESP32](../firmware/controle/tests/esp32_compile/esp32_compile.ino) compila os mesmos 29 grupos e, se executada, publica resultados por Serial a 115200 baud. Reserva **48 KiB de pilha somente para a suíte**, que mantém controladores, cópias de posições e snapshots durante chamadas aninhadas. Isso não constitui medição ou orçamento da aplicação final. Não acessa os periféricos do tabuleiro.

Em **13/10/2026**, passaram **93/93 testes nativos**: 29 do Controle, 24 da interpretação, 22 do acompanhamento e 18 do adaptador. A compilação e o link ESP32 também passaram, com **327.324 bytes de flash** e **24.548 bytes de dados globais** na sonda completa. Os avisos de compilação pertencem à THC preservada; os fontes e testes dos três módulos existentes não foram alterados.

O [resultado estruturado](../firmware/controle/validation/resultado.json) registra comandos, versões e hashes dos fontes. O [JUnit](../firmware/controle/validation/native-tests.xml) identifica os casos e o [log ESP32](../firmware/controle/validation/esp32-compile.log) registra avisos e tamanhos. **Essa primeira rodada não realizou upload nem execução na placa.** Os tamanhos incluem suíte e core Arduino e não medem consumo isolado, pico de heap ou margem de pilha.

## 5. Validação embarcada

A [sonda instrumentada](../firmware/controle/tests/esp32_runtime/runtime_probe.cpp) reutiliza a infraestrutura do MoveInterpreter e executa os 29 grupos funcionais do Controle sem modificá-los. Usa somente observações simuladas, com a biblioteca e os quatro módulos reais; não acessa matriz, botões ou SD.

Com a placa conectada, identificar a porta com `arduino-cli board list` e executar, substituindo `COM7` quando necessário:

```powershell
& .\firmware\controle\validar-esp32.ps1 -Port COM7 -DurationSeconds 180
```

O coletor primeiro executa os 93 testes nativos, depois compila e grava a sonda, substituindo o programa da placa. `-CompileOnly` executa os testes nativos e prepara a imagem sem upload; não registra aprovação embarcada. Os parâmetros `-CMake` e `-ArduinoCli` e as variáveis de ambiente seguem a seção anterior. Após um ensaio, a placa aguarda novos comandos seriais.

Cada rodada recebe identificador próprio e pasta em `firmware/controle/validation/hardware/`. São preservados comandos, versões, JUnit, logs de compilação/upload, captura serial, hashes e cópias da sonda/coletor utilizados. O relatório distingue compilação, upload e execução. O coletor exige início e término com o mesmo identificador, casos e benchmarks completos e ausência de falhas, reinícios ou watchdog; confere novamente os hashes ao final. Os caches `*_flashed.bin`, modificados pelo esptool durante o upload, não são tratados como imagens originais.

### Método de medição

- **Latência de confirmação:** dez aquecimentos e 200 amostras por cenário, em microssegundos com `esp_timer_get_time()`. Cada amostra recebe uma cópia de um Controle preparado fora do intervalo; o relógio cobre somente `confirm()`, incluindo interpretação, aplicação validada e verificação de encerramento quando cabíveis. Preparar a promoção selecionada também fica fora desse intervalo.
- **Latência de fluxo:** cronometra observações, confirmações e comandos de seleção, retomada ou encerramento do cenário, incluindo o despacho e o registro escalar dos retornos/estados para conferência posterior. Construção/cópia inicial, cálculo das ocupações simuladas, carga de FEN, pausas, Serial e espera humana ficam fora. A observação que estabelece a referência está dentro do intervalo. Não representa o tempo físico de execução de uma jogada.
- **Conferência funcional:** os resultados e a posição final são verificados depois do intervalo cronometrado. As suítes completas são executadas antes, depois e aproximadamente a cada dez segundos durante a estabilidade.
- **Heap:** memória interna endereçável por byte, com espaço livre, mínimo local, maior bloco e integridade. As leituras antes da abertura e depois do fechamento separam a memória temporária do monitor ESP-IDF. Os mínimos abrangem chamadas, verificações e atividades do runtime dentro da fase; não isolam o custo exclusivo do Controle.
- **Pilha:** margem mínima cumulativa do `loopTask`, em bytes, com reserva de 48 KiB. Inclui testes, Controladores, cópias de posições/snapshots e instrumentação. A reserva não é um orçamento validado para o firmware final.
- **Estabilidade:** repetição dos cenários, com conferência dos resultados e checkpoints de memória. O watchdog do `loopTask` é habilitado e sua inscrição verificada. Alimentação e pausas ocorrem entre chamadas/casos, sem desabilitar a vigilância durante as operações.

São 15 cenários de confirmação e 11 de fluxo. Incluem posição inicial, captura, roque, *en passant*, Kiwipete com 48 lances legais e uma posição sintética com três damas e 62 lances legais, sem alegação de alcançabilidade ou pior caso. A promoção é medida antes da escolha, após seleção e com substituição física que cancela a escolha e exige reseleção. Os demais cenários cobrem histórico com 64 quadros, confirmação repetida, captura ambígua, lance ilegal/interrupção seguidos de reposição e tentativa válida, mate, afogamento, material insuficiente e encerramento manual/abandono durante promoção.

O campo `frames` dos benchmarks informa os quadros da **sequência-base preparada**. Nos fluxos de reseleção e retomada, há observações adicionais dentro do intervalo; o campo não é sua contagem total. `legal_moves` informa a lista legal anterior à operação medida. O contador `operations` da estabilidade tem unidade `fixture_runs`: conta cenários executados, não todas as chamadas ao Controle, pois preparação e fluxos podem conter várias confirmações. A estabilidade inclui o custo de preparar novamente posições e executar as suítes.

### Resultados na placa — 13/10/2026

A [rodada concluída](../firmware/controle/validation/hardware/20261013T1745531218641Z/resultado.json) realizou compilação, upload e execução real na **COM7**. A DevKit V1 informada pela equipe identificou **ESP32-D0WD-V3, revisão 3.1, dois núcleos, 240 MHz, flash de 4 MiB e ausência de PSRAM**, com Arduino-ESP32 3.3.12, ESP-IDF v5.5.5 e esptool 5.3.1. A sonda executou no `loopTask`, núcleo 1, com watchdog inscrito.

O [log serial](../firmware/controle/validation/hardware/20261013T1745531218641Z/serial.log) contém início e término com o mesmo identificador. A [conferência das evidências](../firmware/controle/validation/hardware/20261013T1745531218641Z/evidence-check.json) verificou os 131 registros até `end`, 33 hashes de fontes, cinco imagens compiladas e três cópias da instrumentação/coletor utilizados. Os fontes e testes funcionais de GameController, MoveTracker, MoveInterpreter, ChessAdapter e THC permaneceram intactos. Os [93/93 testes nativos](../firmware/controle/validation/hardware/20261013T1745531218641Z/native-tests.xml) passaram novamente antes do upload.

- **Funcionalidade:** 29/29 casos aprovados antes e depois da estabilidade, mais 18 suítes intermediárias: **20 suítes e 580 casos aprovados**. Os benchmarks tiveram **5.200 amostras e 260 aquecimentos**, sem falhas. A suíte cobre as quatro promoções de ambas as cores e movimentos especiais; os benchmarks selecionam os cenários descritos nas tabelas.
- **Estabilidade:** **50.700 cenários em 180,270 segundos**, em 1.950 lotes dos 26 cenários, sem falhas de retorno, reinícios, watchdog ou corrupção de heap observada. Cada lote usa novas fixtures; as suítes intercaladas também verificam sequências de jogadas e histórico na mesma instância.
- **Heap:** checkpoints constantes em **218.124 bytes livres**, mínimo local agregado de **217.724 bytes** e maior bloco de **110.580 bytes**. Antes e depois do monitor de estabilidade havia **218.184 bytes livres**, também o valor final. Em todos os 26 benchmarks, o mínimo local coincidiu com o espaço livre anterior e posterior de cada lote; os **60 bytes temporários do monitor** foram restituídos. Não houve retenção progressiva observada durante a estabilidade.
- **Pilha:** margem mínima de **21.496 bytes**, com **49.152 bytes reservados**, diferença de 27.656 bytes em relação à reserva. Após a primeira suíte a margem era 28.280 bytes e, ao final dos benchmarks, 24.472 bytes. O menor valor apareceu ao repetir a suíte dentro da estabilidade. Esses números incluem os quadros de chamada e as cópias da instrumentação, não apenas `confirm()`.
- **Tamanhos:** a [compilação da sonda completa](../firmware/controle/validation/hardware/20261013T1745531218641Z/esp32-compile.log) registrou **358.739 bytes de flash** e **25.512 bytes de dados globais**. Na placa, `sizeof(GameController)=3720` e `sizeof(ConfirmationResult)=8` bytes. O tamanho do controlador por valor não inclui o armazenamento temporário de suas chamadas.

Latência de **`confirm()`**, em microssegundos, com 200 amostras por cenário:

| Cenário | Movimentos legais | Mediana | Máximo |
| --- | ---: | ---: | ---: |
| Inicial, e2–e4 | 20 | 2.414 | 2.429 |
| Kiwipete, roque curto | 48 | 9.434 | 9.446 |
| Posição sintética com três damas | 62 | 18.096 | 18.102 |
| Captura e4xd5 | 7 | 604 | 605 |
| Roque branco curto | 26 | 4.190 | 4.198 |
| En passant branco | 7 | 605 | 612 |
| Promoção com escolha pendente | 9 | 732 | 738 |
| Promoção com dama já selecionada | 9 | 240 | 247 |
| Promoção com cavalo selecionado, encerrando por material | 9 | 242 | 251 |
| Captura ambígua | 13 | 1.224 | 1.229 |
| e2–e4 com histórico de 64 quadros | 20 | 2.430 | 2.441 |
| Confirmação repetida, sem outro lance | 20 | 17 | 17 |
| Lance que produz mate | 30 | 4.706 | 4.709 |
| Lance que produz afogamento | 29 | 5.435,5 | 5.450 |
| Captura que produz material insuficiente | 7 | 634 | 638 |

Latência de **fluxo completo**, em microssegundos, com 200 amostras por cenário:

| Cenário | Mediana | Máximo |
| --- | ---: | ---: |
| Inicial, e2–e4 | 2.430 | 2.440 |
| Captura e4xd5 | 611 | 616 |
| Roque branco curto | 4.202 | 4.215 |
| En passant branco | 616,5 | 621 |
| Promoção, seleção de dama e confirmação | 987 | 992 |
| Promoção, seleção de cavalo e encerramento por material | 989 | 999 |
| Promoção com alteração física, cancelamento e reseleção | 1.737 | 1.744 |
| Rejeição de e2–e5, reposição e aceitação de e2–e4 | 4.729 | 4.737 |
| Interrupção, reposição e aceitação de e2–e4 | 2.455 | 2.466 |
| Encerramento manual durante promoção | 748,5 | 753 |
| Abandono durante promoção | 748 | 753 |

Mínimos e médias constam no relatório. Os tempos incluem o custo do relógio, sem subtração; as operações rápidas estão sujeitas à sua resolução. Promoção selecionada mede somente a segunda confirmação, enquanto o fluxo completo inclui a primeira confirmação e a escolha. O maior tempo observado, **18,102 ms**, não constitui limite superior para todas as posições ou para o firmware integrado.

O heap livre, sem o monitor, passou de **218.736 para 218.272 e depois 218.184 bytes** entre os primeiros cenários, diferença total de **552 bytes**. Essa redução ocorreu entre lotes, fora dos intervalos locais de benchmark; sua origem não foi isolada e não é atribuída exclusivamente ao Controle. O mínimo global de **177.900 bytes** já existia antes da primeira suíte. As conclusões de estabilidade utilizam as leituras locais nas mesmas condições, sem confundir esses valores com consumo durante `confirm()`.

### Ocorrências e parecer

Os avisos da compilação pertencem aos fontes THC preservados. O [upload](../firmware/controle/validation/hardware/20261013T1745531218641Z/esp32-upload.log) confirmou os hashes gravados e reiniciou a placa automaticamente. O esptool estimou o cristal em **41,13 MHz** antes de normalizá-lo para 40 MHz, ocorrência semelhante à dos [ensaios anteriores](firmware-xadrez.md#ocorrências-e-parecer). Essa estimativa não é uma medição direta do cristal; não houve falha de comunicação ou execução que justificasse alterar a configuração. Nenhuma intervenção física foi necessária.

**O GameController é viável para prosseguir à integração no ESP32 adotado**, nos casos e condições deste ensaio. Os fluxos de confirmação, promoção, rejeição, retomada e encerramento foram executados com os módulos reais e observações simuladas. Nenhum problema medido exigiu modificação ou otimização dos módulos funcionais. A placa permaneceu com a nova sonda instalada, aguardando comandos seriais.

## 6. Limites e próximas integrações

O Controle não incorpora aquisição, debounce, confirmação por botão, LEDs/buzzer, display ou geração SAN/PGN. O [Registro](firmware-registro.md) é um módulo independente; seu contexto anterior deve ser uma **cópia** de `position()` obtida antes de `confirm()`. Guardar a referência devolvida não preserva o estado anterior. O retorno de `confirm()` identifica o lance aceito, e a posição resultante permanece consultável. A ligação dessas chamadas no ciclo principal permanece pendente.

O Controle não adiciona alocação dinâmica própria; armazena adaptador e tracker por valor e usa as cópias temporárias exigidas pelos contratos. Não se presume ausência de heap nos serviços THC. A execução isolada foi validada na placa conforme a seção anterior; ainda é necessário dimensionar pilha, heap e latência do firmware integrado. Não foi ensaiada a pilha Arduino padrão de 8 KiB, e a reserva de 48 KiB da suíte não deve ser transferida automaticamente à aplicação.

O ensaio de três minutos não demonstra estabilidade indefinida nem ausência universal de alocações ou vazamentos. Os cenários cobrem posições selecionadas com até 62 movimentos legais e não substituem a validação da matriz, das transições físicas de captura ou da interação com interface e SD. `confirm()` é síncrono; a integração precisa avaliar o intervalo ocupado pelo processamento junto à aquisição e às demais tarefas, sem tomar a maior latência observada como garantia de pior caso.

`ApplyFailed` é um retorno defensivo: com posição privada, chamadas serializadas e os contratos atuais, o movimento recém-identificado deve continuar legal. Não se introduziu uma interface de simulação de falhas apenas para provocar esse ramo.
