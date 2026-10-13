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

A função usa armazenamento local de tamanho limitado e não acrescenta alocação dinâmica própria. São examinados no máximo 64 quadros e a lista fixa de até 218 movimentos do adaptador. `apply()` revalida cada candidato na cópia, portanto o custo inclui gerações repetidas da lista legal, aproximadamente quadrático em sua quantidade. Essa escolha preserva a interface existente e evita duplicar regras; latência e margem de pilha deverão ser medidas na execução embarcada do módulo.

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

A execução da nova suíte na placa, a medição de seus recursos e a integração com confirmação e aquisição física permanecem pendentes. A [validação embarcada anterior do adaptador](firmware-xadrez.md#5-validação-embarcada) mantém seu escopo próprio; não comprova execução do intérprete.
