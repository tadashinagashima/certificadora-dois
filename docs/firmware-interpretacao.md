# Interpretação da movimentação

O módulo [MoveInterpreter](../firmware/interpretacao/src/MoveInterpreter.h) relaciona a tentativa registrada pelo [MoveTracker](firmware-acompanhamento.md) aos movimentos legais do [ChessAdapter](firmware-xadrez.md). Implementa a identificação prevista em RF04 e utiliza a validação de RF05, mantendo a separação de RNF02. É C++14 independente de Arduino, sem estado próprio, aquisição de sensores ou controle da partida.

## 1. Contrato

```cpp
InterpretationResult interpretMove(const AttemptSnapshot& attempt,
                                   const ChessAdapter& position,
                                   Promotion selection = Promotion::None);
```

O chamador entrega a tentativa completa após a confirmação e a posição oficial anterior ao lance. A função consulta ambos sem modificá-los, inclusive o histórico de repetição. Usa a representação de 64 bits dos módulos existentes, com `a1 = 0` e `h8 = 63`, e devolve um `Move` nos mesmos índices.

| Resultado | Significado |
| --- | --- |
| `Identified` | Exatamente um movimento legal corresponde às observações e à seleção de promoção, quando necessária. |
| `NoMove` | A ocupação final coincide com a inicial, inclusive após retirar e recolocar uma peça. |
| `NoLegalMatch` | Nenhum lance legal completo corresponde às observações e à seleção. Pode representar movimento incompleto, ilegal ou captura sem transição de destino observada. |
| `Ambiguous` | Mais de um movimento corresponde, além da escolha de promoção para um mesmo par origem/destino. Não há escolha por ordem de geração ou última casa alterada. |
| `PromotionRequired` | As correspondências são promoções de um mesmo par origem/destino e a peça ainda não foi selecionada. |
| `UnreliableInput` | A tentativa está sem referência/leitura, perdeu continuidade, excedeu o histórico ou contém quadros inconsistentes. |
| `PositionMismatch` | A ocupação inicial da tentativa difere da ocupação da posição oficial. |

Somente `Identified` preenche `move`. Nos demais resultados, o campo permanece `Move{}`, com casas inválidas e sem promoção. A confiabilidade é verificada antes da correspondência da posição; ambas são verificadas antes de `NoMove`.

`Promotion::None` não escolhe dama automaticamente. Após `PromotionRequired`, o chamador pode repetir a consulta com dama, torre, bispo ou cavalo. Uma seleção informada filtra os candidatos por esse tipo; seleção em lance comum ou valor desconhecido não produz correspondência. A escolha não resolve ambiguidade entre destinos de captura distintos.

## 2. Correspondência física e legal

Primeiro, o módulo verifica o prefixo válido `frames[0..frameCount)`: limite do vetor, máscaras disjuntas, retirada somente de casas ocupadas, adição somente em casas vazias e presença de alguma alteração em cada quadro. Reaplica as máscaras agrupadas e confere a ocupação corrente. Não consulta a cauda do vetor nem atribui ordem às casas do mesmo quadro; timestamps não participam da interpretação.

Em seguida, consulta os movimentos legais e aplica cada candidato em uma cópia completa do adaptador. A ocupação resultante deve coincidir com a ocupação observada. As regras e os efeitos de roque, *en passant* e promoção continuam definidos exclusivamente pela biblioteca de xadrez.

Uma captura em destino inicialmente ocupado exige que esse destino tenha aparecido vazio no histórico. Por exemplo, em `e4xd5`, o desaparecimento de `e4` sozinho não distingue uma captura da peça atacante ainda levantada. O histórico precisa registrar `d5` vazia antes de terminar ocupada. A peça atacante ou a capturada pode ser retirada primeiro, e ambas podem desaparecer no mesmo quadro. Se duas casas de captura legal distintas apresentarem essa evidência e a mesma ocupação final, o retorno será `Ambiguous`.

O roque aceita movimentação do rei e da torre em qualquer ordem, desde que o quadro final corresponda ao lance completo. No *en passant*, a retirada do peão capturado pode ocorrer antes ou depois da colocação do atacante. Na promoção, a retirada/recolocação na última fileira é aceita, mas os sensores não distinguem a peça promovida; essa informação vem de `selection`.

A função usa armazenamento local de tamanho limitado e não acrescenta alocação dinâmica própria. São examinados no máximo 64 quadros e a lista fixa de até 218 movimentos do adaptador. `apply()` revalida cada candidato na cópia, portanto o custo inclui gerações repetidas da lista legal, aproximadamente quadrático em sua quantidade. Essa escolha preserva a interface existente e evita duplicar regras. A validação embarcada descrita adiante mede esse custo sem alterar o algoritmo.

## 3. Limites e integração futura

O sistema pressupõe jogadores seguindo as regras e o procedimento de uso. Não há recuperação de manipulações arbitrárias nem reconciliação de peças com a posição oficial. Alterações temporárias que retornam à ocupação anterior não são interpretadas como identidade de peças ou intenção do jogador.

O chamador deve fornecer tentativa e posição correspondentes à mesma jogada e serializar as chamadas. A igualdade de ocupação inicial não prova identidade das peças, lado a jogar ou atualidade do histórico. Transições que ocorreram inteiramente entre leituras não podem ser reconstruídas; em uma captura, a ausência da vacância observada impede a identificação, mesmo se houver um único destino legal.

O Controle futuro receberá a confirmação, entregará o snapshot à interpretação e decidirá a aplicação oficial e o descarte da tentativa. A dependência do intérprete em `MoveTracker.h` utiliza apenas seu tipo de dados; não há consulta direta a um tracker ativo. O módulo não aplica o lance oficial, escolhe sinais da interface, lê botões, produz SAN/PGN ou altera o ciclo de vida da tentativa. Os [diagramas existentes](arquitetura.md) continuam aplicáveis.

## 4. Validação

Na raiz, com CMake/MSVC do Visual Studio 2022 e Arduino CLI instalados:

```powershell
& .\firmware\interpretacao\validar.ps1
# Somente as suites nativas:
& .\firmware\interpretacao\validar.ps1 -NativeOnly
```

Os parâmetros `-CMake` e `-ArduinoCli` aceitam outros caminhos. O script reutiliza os projetos CMake de acompanhamento e xadrez, verifica os hashes THC e compila C++14 em Release com `/W4 /WX /permissive-` no novo módulo e seus testes. Para ESP32, usa as três bibliotecas locais por `--library`, sem instalação global, e o core Arduino-ESP32 **3.3.12**, FQBN `esp32:esp32:esp32`.

Em ambiente que redirecione o perfil do Windows, apontar `ARDUINO_DIRECTORIES_DATA` para a instalação existente. `ARDUINO_BUILD_CACHE_PATH` pode apontar para `firmware/interpretacao/build/core-cache`. Os intermediários ficam em `build/`, já coberto por `.git/info/exclude`; logs, JUnit e hashes dos fontes utilizados ficam em `validation/` como evidência técnica. Cada execução substitui o relatório anterior desse módulo e registra se a compilação ESP32 foi realizada.

Os [24 grupos de testes](../firmware/interpretacao/tests/interpreter_tests.cpp) usam observações sintéticas via MoveTracker e posições conhecidas no ChessAdapter. Cobrem lances comuns, extremos da representação de 64 bits, capturas e ambiguidade, quatro roques em diferentes ordens, *en passant*, quatro promoções para ambas as cores, promoção com captura/substituição, confiabilidade, consistência dos quadros, capacidade e descarte, timestamps, preservação das entradas e uma sequência de 15 lances. As ocupações esperadas são especificadas pelos casos, sem utilizar a simulação do intérprete como referência. As verificações permanecem ativas em Release.

A [sonda ESP32](../firmware/interpretacao/tests/esp32_compile/esp32_compile.ino) compila os mesmos 24 grupos e publica os resultados por Serial a 115200 baud se executada. Reserva **32 KiB de pilha somente para os testes**, que mantêm snapshots e cópias de tabuleiros simultaneamente; essa reserva não constitui medição nem orçamento definido para a aplicação. Não acessa periféricos do tabuleiro.

Em **12/10/2026** (horário de Brasília), passaram **64/64 casos nativos**: 24 da interpretação, 22 do acompanhamento e 18 do adaptador, incluindo `TestInternals()` da THC. A compilação/link para ESP32 também passou, com **317.268 bytes de flash** e **24.548 bytes de dados globais** na sonda completa. Os avisos observados pertencem aos fontes THC preservados. Os fontes dos módulos existentes não foram alterados.

O [relatório estruturado](../firmware/interpretacao/validation/resultado.json) registra versões, comandos, hashes e a ausência de upload/execução na placa; o [JUnit](../firmware/interpretacao/validation/native-tests.xml) identifica os casos e o [log ESP32](../firmware/interpretacao/validation/esp32-compile.log) registra a compilação. Os tamanhos do binário incluem suíte e core Arduino; não medem o consumo isolado do módulo, o pico de heap ou a margem de pilha.

Essa primeira rodada não executou a nova suíte na placa. A [validação embarcada anterior do adaptador](firmware-xadrez.md#5-validação-embarcada) mantém seu escopo próprio; os ensaios do intérprete estão descritos a seguir.

## 5. Validação embarcada

A [sonda instrumentada](../firmware/interpretacao/tests/esp32_runtime/runtime_probe.cpp) adapta a infraestrutura de medição do ChessAdapter e reutiliza os 24 testes funcionais sem alterações. Executa a suíte antes e depois da estabilidade e em intervalos de aproximadamente dez segundos. Os dados de ocupação são sintéticos, produzidos pelo MoveTracker; não há acesso à matriz, aos botões ou ao SD.

Com a placa conectada, identificar a porta com `arduino-cli board list` e executar, substituindo `COM7` quando necessário:

```powershell
& .\firmware\interpretacao\validar-esp32.ps1 -Port COM7 -DurationSeconds 180
```

O parâmetro `-CompileOnly` prepara o binário sem gravar ou executar na placa. O upload substitui o programa da placa pela sonda, que aguarda comandos seriais após cada ensaio. Não é necessário manter periféricos conectados. Os parâmetros de ferramentas e ambiente seguem a seção anterior.

Cada rodada recebe identificador próprio e pasta em `firmware/interpretacao/validation/hardware/`, com fontes da instrumentação utilizada, versões, comandos, hashes, logs de compilação/upload e captura serial. O coletor distingue compilação, upload e execução, confere os nomes dos 24 testes e os metadados dos 20 cenários, e rejeita falhas, reinícios, watchdog, timeout ou conclusão incompleta. Ao terminar, confere novamente os hashes de fontes e imagens compiladas; caches `*_flashed.bin`, modificados pelo esptool, não são tratados como imagens originais.

### Método de medição

- **Latência:** dez aquecimentos e 200 amostras por cenário, usando `esp_timer_get_time()` em microssegundos. O intervalo contém a chamada completa a `interpretMove`, incluindo cópias e revalidações internas. Preparação do FEN/snapshot, conferência do status/movimento devolvido, Serial e pausas ficam fora do intervalo. O custo da leitura do relógio não é subtraído.
- **Cenários:** movimento inicial, captura, quatro roques, Kiwipete, *en passant* de ambas as cores, promoção pendente/selecionada e com captura, ausência de lance, rejeição, ambiguidade, interrupção e histórico com 64 quadros. Uma posição sintética com três damas e 62 movimentos legais amplia a carga; não representa prova de alcançabilidade nem pior caso do xadrez. As contagens de movimentos legais são verificadas na preparação.
- **Heap:** memória interna endereçável por byte, com espaço livre, mínimo por fase, maior bloco e verificação de integridade. O monitor local do ESP-IDF separa o ensaio do mínimo global atingido no boot. Nos benchmarks e na estabilidade, os valores anteriores à abertura e posteriores ao fechamento permitem distinguir o armazenamento temporário do monitor. Os mínimos agregam regiões e incluem atividades do runtime; não isolam exclusivamente o intérprete.
- **Pilha:** margem mínima cumulativa do `loopTask`, em bytes, medida por `uxTaskGetStackHighWaterMark(nullptr)`, com reserva de 32 KiB. Inclui suíte, cópias de snapshots/tabuleiros, core e instrumentação. Não é o consumo isolado da função nem define a pilha da aplicação futura.
- **Estabilidade:** repetição dos 20 cenários, conferindo cada resultado e repetindo a suíte periodicamente. O watchdog do `loopTask` é habilitado explicitamente e seu registro é verificado; a alimentação ocorre entre casos/amostras, nunca dentro de `interpretMove`. Pausas permitem execução das demais tarefas. Heap, integridade e margem de pilha são registrados em checkpoints.

### Resultados na placa — 13/10/2026

A [rodada concluída](../firmware/interpretacao/validation/hardware/20261013T0736246658167Z/resultado.json) teve compilação, upload e execução real aprovados pela **COM7**. A DevKit V1 informada pela equipe identificou **ESP32-D0WD-V3, revisão 3.1, dois núcleos, 240 MHz, flash de 4 MiB e ausência de PSRAM**. Foram utilizados Arduino-ESP32 3.3.12, ESP-IDF v5.5.5 e esptool 5.3.1, com a sonda no `loopTask`, núcleo 1.

O [log serial](../firmware/interpretacao/validation/hardware/20261013T0736246658167Z/serial.log) registra início, resultados e conclusão com o mesmo identificador. A [conferência das evidências](../firmware/interpretacao/validation/hardware/20261013T0736246658167Z/evidence-check.json) confirmou os 115 registros até `end`, 18 hashes de fontes, cinco imagens compiladas e as cópias da sonda/coletor utilizadas. MoveInterpreter, MoveTracker, ChessAdapter, THC e os testes funcionais permaneceram iguais à validação anterior. Os **64/64 testes nativos** também passaram novamente antes do upload.

- **Funcionalidade:** 24/24 casos aprovados antes e depois da estabilidade, mais 18 execuções intermediárias completas: **20 suítes e 480 casos aprovados**. Os benchmarks tiveram **4.000 amostras e 200 aquecimentos**, todos com status e movimento esperados.
- **Estabilidade:** **57.640 interpretações em 180,265 segundos**, distribuídas por 2.882 lotes dos 20 cenários. Não houve falhas de retorno, reinícios inesperados, watchdog ou corrupção de heap observada.
- **Heap:** checkpoints constantes em **234.508 bytes livres**, mínimo local agregado de **234.128 bytes** e maior bloco de **110.580 bytes**. Antes e depois do monitor de estabilidade havia **234.568 bytes livres**, também o valor final. Nos 20 benchmarks, espaço livre antes/depois e mínimo local foram iguais; os **60 bytes temporários do monitor** foram restituídos. Não houve retenção progressiva observada durante a estabilidade.
- **Pilha:** margem mínima de **12.600 bytes**, com **32.768 bytes reservados**, diferença de 20.168 bytes em relação à reserva. Após a primeira suíte, a margem era 16.408 bytes; o mínimo menor apareceu quando a suíte foi executada dentro da estabilidade. Esses valores incluem os quadros de chamada e dados locais da instrumentação, não apenas a interpretação.
- **Tamanhos:** sonda completa com **344.527 bytes de flash** e **25.512 bytes de dados globais**. No alvo, `sizeof(ChessAdapter)=2140`, `sizeof(LegalMoves)=660`, `sizeof(AttemptSnapshot)=1560`, `sizeof(MoveTracker)=1560` e `sizeof(InterpretationResult)=8` bytes.

Latências em **microssegundos**, com 200 amostras por cenário:

| Cenário | Movimentos legais | Mediana | Máximo |
| --- | ---: | ---: | ---: |
| Inicial, e2–e4 | 20 | 2.271 | 2.281 |
| Kiwipete, roque curto | 48 | 9.155 | 9.161 |
| Captura e4xd5 | 7 | 513 | 514 |
| Roque branco curto | 26 | 3.987 | 3.988 |
| Roque branco longo | 26 | 3.987 | 3.988 |
| Roque preto curto | 26 | 3.975 | 3.977 |
| Roque preto longo | 26 | 3.975 | 3.977 |
| En passant branco | 7 | 512,5 | 516 |
| En passant preto | 7 | 521,5 | 523 |
| Promoção branca, escolha pendente | 9 | 723 | 725 |
| Promoção branca, dama selecionada | 9 | 142 | 147 |
| Promoção preta, dama selecionada | 9 | 146 | 151 |
| Promoção com captura, escolha pendente | 13 | 1.234 | 1.239 |
| Promoção com captura, dama selecionada | 13 | 262 | 267 |
| Sem movimento | 20 | 8 | 10 |
| Lance ilegal e2–e5 | 20 | 2.272 | 2.276 |
| Captura ambígua | 13 | 1.215 | 1.219 |
| e2–e4 com 64 quadros | 20 | 2.287 | 2.295 |
| Entrada interrompida | 20 | 1 | 2 |
| Posição sintética com três damas | 62 | 17.790 | 17.793 |

Mínimos e médias também constam no JSON. As contagens indicam a lista legal da posição, não necessariamente o número de candidatos simulados: destinos de captura sem vacância e tipos de promoção diferentes da seleção são filtrados. Os resultados rápidos próximos de 1 µs estão sujeitos à resolução do relógio. O maior tempo observado, **17,793 ms**, não é limite superior para todas as posições ou para o firmware integrado.

O heap livre caiu de 235.120 bytes antes da primeira suíte para 234.568 bytes ao final, diferença de **552 bytes**. A redução ocorreu entre fases, fora dos intervalos medidos dos benchmarks; sua origem não foi isolada e não é atribuída exclusivamente ao intérprete. O mínimo global de 194.284 bytes já existia antes do ensaio. A análise de estabilidade usa as leituras com o monitor local nas mesmas condições.

### Ocorrências, parecer e pendências

Os avisos da compilação concluída pertencem aos fontes THC preservados; o ensaio não exigiu alterações nos módulos funcionais.

O [upload](../firmware/interpretacao/validation/hardware/20261013T0736246658167Z/esp32-upload.log) verificou os dados gravados e reiniciou a placa automaticamente, sem intervenção em BOOT. O esptool repetiu a estimativa de cristal de 41,01 MHz antes de normalizá-la para 40 MHz, ocorrência já analisada no [ensaio do adaptador](firmware-xadrez.md#ocorrências-e-parecer). Esse aviso não é uma medição direta do cristal; não ocorreu falha de comunicação ou execução que justificasse alterar a configuração nesta rodada.

**O MoveInterpreter é viável para prosseguir à integração no ESP32 adotado**, considerando os casos, latências e recursos observados nesta sonda. Nenhum problema medido exige refatoração ou otimização do módulo.

A aplicação futura ainda precisa dimensionar sua pilha e medir o comportamento com Controle, aquisição e demais tarefas. Não foi ensaiada a pilha Arduino padrão de 8 KiB; a reserva de 32 KiB da suíte não deve ser transferida automaticamente ao firmware final. O ensaio de três minutos não demonstra estabilidade indefinida nem ausência universal de alocação/vazamento. Os casos usam ocupações sintéticas e até 62 movimentos legais; não substituem o teste da matriz, a validação das transições físicas de captura ou a avaliação de todas as posições possíveis. A integração com confirmação, interface e registro da partida permanece fora desta etapa.
