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

O [resultado estruturado](../firmware/controle/validation/resultado.json) registra comandos, versões e hashes dos fontes. O [JUnit](../firmware/controle/validation/native-tests.xml) identifica os casos e o [log ESP32](../firmware/controle/validation/esp32-compile.log) registra avisos e tamanhos. **Não houve upload nem execução deste módulo na placa.** Os tamanhos incluem suíte e core Arduino e não medem consumo isolado, pico de heap ou margem de pilha.

## 5. Limites e próximas integrações

Esta etapa não incorpora aquisição, debounce, confirmação por botão, LEDs/buzzer, display ou registro SAN/PGN. O contexto anterior necessário ao futuro Registro deve ser uma **cópia** de `position()` obtida antes de `confirm()`; guardar a referência devolvida não preserva o estado anterior. O retorno de `confirm()` identifica o lance aceito, e a posição resultante permanece consultável.

O Controle não adiciona alocação dinâmica própria; armazena adaptador e tracker por valor e usa as cópias temporárias exigidas pelos contratos. Não se presume ausência de heap nos serviços THC. A execução embarcada, latência, heap e dimensionamento de pilha do Controle e do firmware integrado continuam pendentes. Os ensaios anteriores dos outros módulos não validam automaticamente este novo caminho de chamada.

`ApplyFailed` é um retorno defensivo: com posição privada, chamadas serializadas e os contratos atuais, o movimento recém-identificado deve continuar legal. Não se introduziu uma interface de simulação de falhas apenas para provocar esse ramo.
