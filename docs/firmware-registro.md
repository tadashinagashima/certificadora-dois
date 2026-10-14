# Registro SAN/PGN

O [GameRecord](../firmware/registro/src/GameRecord.h) registra lances aceitos e exporta uma partida em PGN, em C++14 independente de Arduino. Implementa a parte lógica de RF11 e o reflexo dos encerramentos de RF09–RF10 no registro, conforme a [issue #7](https://github.com/tadashinagashima/certificadora-dois/issues/7). A persistência em microSD de RF12 permanece uma integração própria.

O módulo recebe os resultados públicos do [Controle](firmware-controle.md) e utiliza o [ChessAdapter](firmware-xadrez.md) para SAN e conferência das posições. Não possui um Controlador, não recebe sensores e não aplica movimentos na posição oficial. A inclusão de `GameController.h` reutiliza `ConfirmationResult` e `GameEnd`; não introduz chamadas do Registro ao Controle. Os módulos anteriormente validados e seus testes foram preservados.

## 1. Contrato

| Operação | Comportamento |
| --- | --- |
| `begin(initial, metadata)` | Inicia ou substitui o registro, guardando FEN inicial, lado, número do lance e metadados. Um erro preserva o registro anterior. |
| `record(before, confirmation, after)` | Acrescenta somente um lance `Accepted`, gerando SAN em `before` e conferindo a posição `after`. |
| `finish(official, ending)` | Registra o resultado, sem exigir outro lance, e impede novos acréscimos. |
| `started()` / `finished()` | Informam se o registro foi iniciado e encerrado. |
| `plyCount()` | Conta meios-lances efetivamente registrados. |
| `movetext()` | Referência const à sequência numerada de SAN, sem cabeçalhos, resultado ou quebras de linha. |
| `pgn()` | Retorna o documento completo por valor; antes de `begin()`, retorna string vazia. |

As chamadas sobre uma instância devem ser serializadas, fora de interrupções. O registro não mantém uma segunda partida ativa: armazena texto e a FEN esperada para o próximo contexto. A FEN inclui peças, lado, direitos de roque, *en passant* e contadores; não identifica a origem de um histórico de repetição. Cabe ao chamador fornecer os contextos da mesma partida.

`record()` compara `before.fen()` com o contexto esperado, solicita `before.san(move)` e aplica o lance em uma **cópia** de `before` para conferir `after.fen()`. Somente após essas verificações e o teste de capacidade altera o registro. As duas posições recebidas, inclusive seus históricos de repetição, permanecem intactas. Não há regras SAN próprias nem reconstrução de peças a partir de ocupação.

| Retorno | Significado |
| --- | --- |
| `Ok` | Operação concluída. |
| `Ignored` | Confirmação diferente de `Accepted`; nenhuma alteração, mesmo antes de iniciar ou depois de encerrar. |
| `NotStarted` | Tentativa de registrar lance aceito ou encerrar antes de `begin()`. |
| `AlreadyFinished` | Lance aceito após encerramento ou tentativa de alterar o encerramento existente. |
| `InvalidMetadata` | Metadados fora dos limites ou formato aceito. |
| `InvalidContext` | Posição anterior divergente, posição posterior incoerente, encerramento numa posição não registrada ou número inicial de lance inválido. |
| `InvalidMove` | Lance informado como aceito, mas inválido no contexto anterior. |
| `CapacityExceeded` | O acréscimo ultrapassaria o limite do movetext. |
| `InvalidResult` | Combinação de motivo/resultado inválida ou término automático incompatível com a posição. |

Todos esses retornos de erro preservam o registro. Duplicatas e saltos de sequência são rejeitados pela conferência de contexto. O módulo não tenta recuperar uma sequência perdida. `begin()` aceita posições preparadas pelo adaptador, inclusive FENs de testes; não é um validador de posições arbitrárias externas.

## 2. Ligação com o Controle

A posição anterior deve ser uma **cópia**, obtida antes de cada confirmação. Guardar a referência retornada por `position()` não preserva esse contexto. A ordem de uso é:

1. Chamar `record.begin(game.position(), metadata)` no início e verificar `Ok`.
2. Após entregar as observações ao Controle, copiar `const ChessAdapter before = game.position()`.
3. Obter `const ConfirmationResult confirmation = game.confirm()`.
4. Chamar `record.record(before, confirmation, game.position())` e conferir o retorno.
5. Quando `game.state() == GameState::Finished`, chamar `record.finish(game.position(), game.ending())` e conferir `Ok`.

As etapas 2–4 se repetem na segunda confirmação de uma promoção. A primeira produz `Ignored`; só a confirmação aceita registra a peça escolhida. Rejeições, `NoMove`, seleção de promoção e observações físicas não acrescentam lances. Encerramento manual ou abandono requer apenas a etapa 5, inclusive durante promoção pendente. Uma posição inicial terminal também pode ser registrada e encerrada sem lances.

O Controle já terá aplicado um lance quando o Registro o recebe. Portanto, falha de registro **não desfaz** a posição oficial. A futura integração deve tratar o retorno e interromper o fluxo de gravação/partida quando houver perda de continuidade ou capacidade, em vez de seguir silenciosamente. Nenhuma recuperação, sinalização de erro ou coordenação adicional foi inserida nos módulos existentes.

`finish()` exige que a posição oficial corresponda ao último lance registrado. Manual/`Unfinished` produz `*`; abandono aceita vitória branca ou preta; mate confere o vencedor contra `status()`; afogamento e material insuficiente conferem a condição antes de aceitar empate. Mantém-se a política de encerramento do Controle, sem adjudicar reclamações de repetição ou 50 lances. Repetir exatamente o mesmo encerramento, na mesma posição, retorna `Ok`; tentar modificá-lo retorna `AlreadyFinished`.

## 3. Formato e limites

A exportação segue a estrutura da [especificação PGN](https://www.saremba.de/chessgml/standards/pgn/pgn-complete.htm): cabeçalhos `Event`, `Site`, `Date`, `Round`, `White`, `Black` e `Result`, linha vazia, lances numerados e marcador final consistente com `Result`. A notação SAN usa os serviços já validados do adaptador para captura, roque, *en passant*, promoção, desambiguação, xeque e mate. Não há comentários, variantes, importação ou tags livres.

Posições iniciais diferentes da padrão acrescentam `FEN` e `SetUp "1"`. O contador da FEN é respeitado, inclusive início pelas pretas: `42... e5 43. e4 Nf6`. A posição padrão produz `1. e4 e5 2. Nf3`. O movetext exportado usa linhas de até 79 caracteres, sem dividir tokens, com LF e uma linha vazia ao final. Enquanto a partida estiver ativa, o documento termina em `*`; isso não encerra a instância.

Os metadados são fornecidos pelo chamador, sem inventar relógio, nomes ou local. Campos vazios viram `?`, e a data vazia vira `????.??.??`. Cada valor possui limite de **128 bytes**; controles ASCII, NUL e DEL são rejeitados. Aspas e barras invertidas são escapadas; um valor também é rejeitado se sua linha de cabeçalho escapada exceder 255 bytes. A data exige `YYYY.MM.DD`, com cada campo composto por dígitos ou inteiramente desconhecido; confere ano 1–9999, mês 1–12 e dia 1–31, sem validar combinações calendáricas.

Nomes acentuados são preservados como UTF-8 fornecido pelo chamador, sem conversão ou validação de codificação. Essa escolha exige leitor compatível com UTF-8; não representa restrição ao Latin-1 do documento PGN histórico. A sintaxe dos lances, marcadores e tags permanece ASCII.

O limite `kMovetextCapacity` é de **8192 bytes**, incluindo números, SAN e espaços, sem cabeçalhos ou marcador de resultado. Não é um limite legal de duração da partida nem uma quantidade fixa de lances. Ao excedê-lo, o lance inteiro é recusado; não há truncamento ou descarte de lances antigos. O registro preserva o prefixo anterior e seu contexto. Se a posição oficial já avançou, `finish()` nela falha por divergência, evitando apresentar esse prefixo como uma partida completa.

Esse limite não é um orçamento total de RAM. `std::string` pode reservar mais capacidade que seu conteúdo; metadados, FENs, cópia temporária do adaptador, SAN e a string devolvida por `pgn()` consomem memória adicional. A exportação mantém o histórico e o documento simultaneamente. Não há tratamento próprio de esgotamento do alocador; `CapacityExceeded` indica somente o limite lógico configurado. Pilha, heap e latência do firmware integrado ainda precisam ser dimensionados, considerando as medições isoladas abaixo.

## 4. Compilação e testes

Na raiz do projeto, com CMake/MSVC do Visual Studio 2022 e Arduino CLI:

```powershell
& .\firmware\registro\validar.ps1
# Somente testes nativos:
& .\firmware\registro\validar.ps1 -NativeOnly
```

Os parâmetros `-CMake` e `-ArduinoCli` permitem outros caminhos. Se necessário, `ARDUINO_DIRECTORIES_DATA` aponta para a instalação do core e `ARDUINO_BUILD_CACHE_PATH` para `firmware/registro/build/core-cache`. A compilação usa as cinco bibliotecas locais por `--library`, sem instalá-las globalmente. Builds e caches ficam em `build/`, coberto por `.git/info/exclude`; fontes, testes e evidências em `validation/` permanecem versionáveis.

O script confere hashes THC, compila em C++14 Release com `/W4 /WX /permissive-` e executa as suítes existentes junto dos [26 grupos do Registro](../firmware/registro/tests/record_tests.cpp). As verificações permanecem ativas em Release. Os oráculos SAN/PGN são literais; os testes de integração entregam observações simuladas ao Controle real.

A cobertura inclui partidas completas com vitórias branca e preta, capturas, quatro roques, *en passant* de ambas as cores, quatro promoções com e sem captura para cada cor, xeque/mate, três formas de desambiguação, promoção pendente, rejeição e retomada, encerramento sem lance e em posições terminais, numeração a partir de FEN, duplicatas, contextos incoerentes, preservação de repetição, metadados, quebra de linhas e capacidade. Os ciclos de cavalos usados para esgotar o armazenamento verificam o limite do Registro; não representam nova política de empate do Controle.

A [sonda de compilação](../firmware/registro/tests/esp32_compile/esp32_compile.ino) liga os mesmos 26 grupos no ESP32, com reserva de 48 KiB para a suíte, que mantém Controladores e cópias de posições. Essa reserva não é orçamento validado do firmware final. O script não faz upload nem acessa a placa.

Os comandos, versões, hashes e resultados ficam no [relatório estruturado](../firmware/registro/validation/resultado.json), no [JUnit](../firmware/registro/validation/native-tests.xml) e no [log de compilação ESP32](../firmware/registro/validation/esp32-compile.log). Cada execução substitui as evidências anteriores e distingue compilação de execução física.

Em **13/10/2026**, passaram **119/119 testes nativos**: 26 do Registro e os 93 dos módulos existentes, incluindo `TestInternals()` da THC. A compilação e o link da sonda passaram com Arduino-ESP32 **3.3.12**, FQBN `esp32:esp32:esp32`, usando **357.244 bytes de flash** e **24.548 bytes de dados globais**. Esses tamanhos incluem suíte e core; não medem pico de heap, margem de pilha ou consumo isolado do Registro. Os avisos de compilação pertencem à THC preservada. **Não houve upload nem execução na placa nesta rodada.**

## 5. Validação embarcada

A [sonda instrumentada](../firmware/registro/tests/esp32_runtime/runtime_probe.cpp) e o [coletor](../firmware/registro/validar-esp32.ps1) reutilizam a infraestrutura do Controle. A suíte funcional do Registro é executada sem modificações; os demais módulos participam pelas suas interfaces reais. A sonda recebe somente comandos seriais e usa observações simuladas, sem acessar matriz, botões ou SD.

Identificar a porta com `arduino-cli board list` e executar, ajustando `COM7` quando necessário:

```powershell
& .\firmware\registro\validar-esp32.ps1 -Port COM7 -DurationSeconds 180
```

O coletor executa os 119 testes nativos, compila e grava a sonda, substituindo o programa da placa, e captura a resposta ao comando com identificador único. `-CompileOnly` prepara a imagem sem upload ou aprovação embarcada. Caminhos de ferramentas e variáveis de ambiente seguem a seção anterior. Ao terminar, a sonda aguarda novos comandos.

Cada execução recebe uma pasta própria em `firmware/registro/validation/hardware/`, com logs, JUnit, versões, hashes dos fontes e imagens, captura serial e cópias da instrumentação utilizada. O coletor confere início/término, casos, benchmarks, contadores de partidas longas, integridade do heap e ausência de falhas, reinícios ou watchdog. Os hashes são conferidos novamente ao final. O PGN recebido pela serial é preservado em `long-game.pgn`, com tamanho e checksum conferidos contra os informados pela placa. O arquivo contém o prefixo registrado até o limite, terminado em `*`; não é uma partida encerrada nem uma gravação em SD.

### Método

- **Funcionalidade:** 26 grupos antes e depois da estabilidade, com suítes adicionais aproximadamente a cada 30 segundos, entre partidas longas. Incluem partidas completas, movimentos especiais, promoção pendente, rejeições, resultado, formato PGN, metadados, preservação da posição e falha de capacidade.
- **Latência isolada:** dez aquecimentos e 200 amostras por cenário, usando `esp_timer_get_time()`. Os intervalos cobrem `record()`, `ChessAdapter::san()`, `pgn()` ou `finish()` e a atribuição do retorno. Preparação, cópia da fixture, conferência, liberação do resultado, Serial e pausas ficam fora. O custo do relógio não é subtraído.
- **Contextos:** registro inicial, captura, roque em Kiwipete, *en passant*, promoção, mate e posição sintética com três damas e 62 movimentos legais. Não se presume pior caso nem alcançabilidade dessa posição sintética. A SAN nela é `Qcd4+`, incluindo desambiguação e xeque. Encerramento cobre manual, abandono, mate, afogamento e material insuficiente.
- **Comprimento:** exportação com 0, 100, 600 e 1276 meios-lances; no maior caso, os cinco metadados textuais têm 128 bytes cada. Registro próximo ao limite mede tanto o último lance que cabe quanto o seguinte, recusado. Para repetir `record()` e `finish()` nas mesmas condições, cada amostra usa uma cópia do Registro; a capacidade das strings dessa cópia pode diferir da capacidade acumulada durante uma partida. O crescimento natural também é medido no ensaio prolongado.
- **Partida prolongada:** o Controle recebe retirada e colocação dos cavalos na sequência `Nf3 Nf6 Ng1 Ng8`, repetida na mesma instância. A cada lance, a sonda confirma, registra e exporta PGN. Confere a numeração, tamanho, contagem, SAN, estrutura exportada e checksum acumulado a partir da sequência esperada. No limite, verifica repetição da recusa sem alteração do prefixo e rejeição do encerramento na posição oficial já avançada. Esse ciclo exercita armazenamento; não altera a política de empate por reclamação do Controle.
- **Estabilidade:** repete partidas até o limite, reiniciando as mesmas instâncias, alternando metadados padrão e máximos e exportando após cada confirmação. Os contadores distinguem lances registrados e exportações, que incluem uma tentativa excedente por partida. O tempo solicitado é mínimo: a última partida e uma eventual suíte terminam antes de encerrar a fase.
- **Tempos do fluxo prolongado:** mede separadamente confirmação, registro e exportação; o intervalo agregado começa antes das observações e termina ao obter o PGN. Inclui os relógios intermediários e uma consulta de heap, mas exclui cópia do contexto anterior, preparação das ocupações, verificações e liberação do PGN. Não representa tempo físico de movimentação das peças.
- **Heap:** monitora memória interna endereçável por byte, mínimo local, maior bloco e integridade. Benchmarks incluem cópias e verificações da instrumentação no intervalo de memória, embora essas operações fiquem fora da cronometragem. Checkpoints longos distinguem memória antes, com o PGN retido e após sua liberação. Leituras antes/depois do monitor e da destruição dos objetos permitem separar retenção das strings, custo do monitor e memória restituída.
- **Pilha e watchdog:** margem mínima cumulativa do `loopTask`, em bytes, com reserva de 48 KiB para a suíte. O watchdog permanece habilitado e conferido; a sonda configura 30 segundos para acomodar o teste funcional de capacidade, que exporta repetidamente sem callback intermediário. Alimentação e pausas ocorrem entre operações/casos. Essa configuração pertence ao ensaio, sem definir a pilha ou o prazo de vigilância do firmware final.

### Resultados na placa — 13/10/2026

A [rodada concluída](../firmware/registro/validation/hardware/20261013T2357537220813Z/resultado.json) realizou compilação, upload e execução real na **COM7**. A DevKit V1 informada pela equipe identificou **ESP32-D0WD-V3, revisão 3.1, dois núcleos, 240 MHz, flash de 4 MiB e ausência de PSRAM**, com Arduino-ESP32 3.3.12, ESP-IDF v5.5.5 e esptool 5.3.1. A sonda executou no `loopTask`, núcleo 1, com watchdog inscrito.

O [log serial](../firmware/registro/validation/hardware/20261013T2357537220813Z/serial.log) contém início e término com o mesmo identificador. A [conferência das evidências](../firmware/registro/validation/hardware/20261013T2357537220813Z/evidence-check.json) verificou 132 registros contra o relatório, 43 hashes de fontes, cinco imagens compiladas e três cópias da instrumentação. Também confirmou os 41 hashes da validação anterior: GameRecord, os quatro módulos anteriores, THC, testes funcionais e configuração nativa foram preservados. Os [119/119 testes nativos](../firmware/registro/validation/hardware/20261013T2357537220813Z/native-tests.xml) passaram antes do upload.

- **Funcionalidade:** 26/26 grupos aprovados antes e depois da estabilidade, mais quatro suítes intermediárias: **seis suítes e 156 casos aprovados**. Os 21 benchmarks tiveram **4.200 amostras e 210 aquecimentos**, sem falhas.
- **Estabilidade:** **17 partidas até o limite em 189,123 segundos**, com **21.692 meios-lances registrados**, **21.709 exportações** e 17 recusas esperadas por capacidade. Não houve falhas de conferência, reinícios, watchdog ou corrupção de heap observada. Esses contadores não incluem a partida detalhada anterior à estabilidade nem os lances executados pelas suítes e preparação de benchmarks.
- **Limite:** na partida detalhada, couberam **1.276 meios-lances**, ocupando **8.185 bytes**, com sete bytes restantes. O acréscimo seguinte, ` 639. Nf3`, exigia nove bytes e retornou `CapacityExceeded`. Repetir a chamada preservou o prefixo; tentar encerrá-lo na posição oficial já avançada retornou `InvalidContext`. A quantidade de lances depende da notação; 1.276 não é um limite geral do módulo.
- **PGN recebido:** o [arquivo exportado pela placa](../firmware/registro/validation/hardware/20261013T2357537220813Z/long-game.pgn) contém **8.917 bytes**, os metadados máximos e a sequência até `638. Ng1 Ng8 *`. O coletor conferiu tamanho/checksum; a verificação no computador comparou todos os lances e cabeçalhos com o texto esperado e confirmou linhas de movetext de até 79 caracteres. Não foi testado um aplicativo externo de análise de xadrez.
- **Tamanhos:** a [compilação da sonda](../firmware/registro/validation/hardware/20261013T2357537220813Z/esp32-compile.log) registrou **391.843 bytes de flash** e **25.512 bytes de dados globais**. Na placa, `sizeof(GameRecord)=240`, `sizeof(GameController)=3720` e `sizeof(ChessAdapter)=2140` bytes. Os 240 bytes do objeto Registro não incluem as strings alocadas no heap.

Latência de **registro**, em microssegundos; cada cenário teve 200 amostras:

| Cenário | Movimentos legais | Mediana | Máximo |
| --- | ---: | ---: | ---: |
| Inicial, e2–e4 | 20 | 1.007 | 1.034 |
| Kiwipete, roque curto | 48 | 2.789 | 2.816 |
| Três damas, `Qcd4+` | 62 | 2.879 | 2.902 |
| Captura e4xd5 | 7 | 468 | 490 |
| En passant e5xd6 | 7 | 467 | 490 |
| Promoção `a8=Q+` | 9 | 496 | 517 |
| Mate `Qh4#` | 30 | 1.603,5 | 1.629 |
| Último lance que cabe, cópia com 8.181 bytes | 22 | 1.318 | 1.344 |
| Lance que excede o limite, cópia com 8.185 bytes | 20 | 1.209 | 1.236 |

Latência de **SAN, PGN e encerramento**, também em microssegundos, com 200 amostras por cenário:

| Operação e contexto | Mediana | Máximo |
| --- | ---: | ---: |
| SAN inicial, `e4` | 627 | 648 |
| SAN Kiwipete, `O-O` | 2.235 | 2.265 |
| SAN três damas, `Qcd4+` | 2.352 | 2.374 |
| PGN sem lances, 96 bytes | 66 | 73 |
| PGN com 100 meios-lances, 687 bytes | 434 | 440 |
| PGN com 600 meios-lances, 3.888 bytes | 2.157 | 2.159 |
| PGN com 1.276 meios-lances e metadados máximos, 8.917 bytes | 4.722 | 4.724 |
| Encerramento manual após 100 meios-lances | 86 | 91 |
| Abandono após 100 meios-lances | 86 | 90 |
| Encerramento em posição de mate | 112 | 125 |
| Encerramento em posição de afogamento | 115 | 128 |
| Encerramento em posição de material insuficiente | 107 | 117 |

Nos fluxos das partidas longas, os maiores tempos observados foram **3.320 µs para confirmação**, **1.374 µs para registro**, **4.731 µs para exportação** e **9.361 µs para o intervalo agregado**. São máximos de operações/ciclos distintos entre a partida detalhada e a estabilidade; não devem ser somados como uma amostra única. O limite de capacidade ainda exige SAN e conferência da posição antes de recusar o lance, conforme a implementação preservada. Nenhuma dessas medidas representa garantia de pior caso para todas as posições.

### Memória e estabilidade

Os 17 checkpoints ao final de partidas repetidas apresentaram **201.312 bytes livres**, com Registro/Controle ainda vivos. Após destruí-los, o heap voltou a **218.136 bytes** com o monitor ativo e **218.196 bytes** após fechá-lo, exatamente o valor anterior à fase. O maior bloco livre permaneceu em **110.580 bytes**. Não houve retenção progressiva observada; `begin()` reutiliza a capacidade já alocada das strings, portanto reiniciar uma partida não implica devolvê-la ao heap.

O mínimo local da estabilidade foi **126.396 bytes livres**, incluindo as suítes intermediárias enquanto os objetos da partida longa permaneciam vivos. A partida detalhada isolada teve mínimo local de **155.612 bytes**, e a primeira suíte, **146.340 bytes**. O mínimo global final foi **123.228 bytes**; esse contador abrange todo o ensaio e não isola uma operação. O heap inicial passou de 218.704 para 218.196 bytes durante os primeiros benchmarks, diferença de **508 bytes**, anterior à estabilidade; sua origem não foi isolada nem atribuída exclusivamente ao Registro.

Próximo ao limite, tanto a string de movetext quanto a de PGN reservaram **15.360 bytes de capacidade**, embora os conteúdos fossem 8.185 e 8.917 bytes. Nos checkpoints com PGN retido, o heap passou de **201.312 para 185.436 bytes livres** e voltou a 201.312 após sua liberação: diferença observada de **15.876 bytes**, incluindo alocação/runtime. No benchmark de exportação máxima, a diferença entre heap anterior e mínimo local foi **30.744 bytes**, abrangendo crescimento temporário e verificações. O benchmark de registro próximo ao limite, feito em cópia, apresentou diferença de **26.156 bytes**; a cópia cresceu de capacidade 8.181 para 16.362 bytes, comportamento diferente do buffer já expandido na partida longa.

A menor margem de pilha foi **24.904 bytes**, com **49.152 bytes reservados**, diferença de 24.248 bytes em relação à reserva. Inclui suíte, instrumentação, Controle, cópias do adaptador e chamadas aninhadas; não mede o custo exclusivo do Registro. A margem após a primeira suíte era 29.048 bytes, caindo ao executá-la dentro da estabilidade. A pilha Arduino padrão de 8 KiB não foi ensaiada.

### Ocorrências e parecer

A preparação nativa da instrumentação corrigiu a expectativa de SAN do cenário com três damas para `Qcd4+`, antes do upload. O [upload](../firmware/registro/validation/hardware/20261013T2357537220813Z/esp32-upload.log) confirmou os hashes gravados e identificou cristal de 40 MHz, sem o aviso de estimativa visto em alguns ensaios anteriores. Os avisos de compilação pertencem à THC preservada. Não houve intervenção física, falha funcional ou de execução que exigisse modificar os módulos.

**O GameRecord é viável para prosseguir à integração no ESP32 adotado**, nas condições deste ensaio. O registro, a exportação, os encerramentos e a recusa por capacidade foram executados diretamente na placa. A placa permaneceu com a sonda do Registro instalada, aguardando comandos seriais.

Para a integração, o limite de 8192 bytes deve ser tratado como limite do conteúdo, sem confundi-lo com RAM total. Exportar uma partida longa cria outra string e ocupou até cerca de 4,7 ms nos casos medidos, antes de qualquer escrita em SD. O orçamento conjunto de memória e o intervalo de aquisição precisam considerar esses custos, os demais módulos e a latência do cartão. Também é necessário tratar `CapacityExceeded` e demais erros: o Controle já pode ter aplicado o lance, enquanto o Registro preserva apenas o prefixo anterior. A sonda verificou essa condição e não implementou recuperação ou política de interface.

O ensaio de pouco mais de três minutos não demonstra estabilidade indefinida, ausência universal de vazamentos ou tolerância a esgotamento real do heap. Não simulou falha do alocador, energia interrompida, gravação em SD ou concorrência com aquisição e interface.

## 6. Pendências

Permanecem a ligação com o ciclo principal e a interface, o dimensionamento e ensaio do firmware integrado e a persistência em SD, incluindo nome do arquivo, escrita e tratamento de falhas de armazenamento. A saída atual é um documento em memória. A validação isolada com observações simuladas não substitui a verificação da aquisição física nem da interação com os periféricos.
