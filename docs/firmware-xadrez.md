# Adaptador da biblioteca de xadrez

O [ChessAdapter](../firmware/xadrez/src/ChessAdapter.h) implementa a interface com a THC selecionada na [avaliação de bibliotecas](avaliacao-bibliotecas-xadrez.md). Mantém a posição lógica, consulta movimentos legais, aplica lances validados e fornece SAN e condições de encerramento. Atende à responsabilidade da biblioteca prevista na [arquitetura](arquitetura.md), com testes próprios de RF05, RF06, RF08, RF10 e da notação de RF11.

O módulo é C++14, independente de Arduino e do MoveTracker. Não recebe ocupações físicas, eventos de botão ou confirmação; não identifica movimentos, coordena partidas ou produz PGN. O Controle futuro deverá chamar `apply()` somente após a confirmação do jogador. Nenhum firmware principal ou ligação com periféricos foi acrescentado.

## 1. Contrato

Todos os tipos públicos pertencem ao namespace `chessboard`. As casas usam `Square` de 8 bits: **a1=0, h1=7, a8=56, h8=63**. `kInvalidSquare=64` representa ausência de casa. A ocupação lógica é `std::uint64_t`, com bit 1 para casa ocupada, compatível com a convenção do acompanhamento; não há dependência entre os dois módulos.

`Move` contém origem, destino e `Promotion`. A promoção precisa ser explícita: `Queen`, `Rook`, `Bishop` ou `Knight`. Movimentos comuns usam `None`. O roque usa o destino final do rei, como e1→g1 ou e1→c1. Os efeitos de captura, roque e *en passant* vêm do lance legal encontrado na THC, sem códigos adicionais impostos ao chamador.

| Operação | Comportamento |
| --- | --- |
| Construção / `reset()` | Estabelece a posição inicial e limpa o histórico lógico. |
| `pieceAt(square)` | Retorna a peça e a cor pelo enum `Piece`; casa vazia ou índice fora de 0–63 retorna `Empty`. |
| `occupancy()` / `sideToMove()` | Informam ocupação lógica e lado a jogar. |
| `legalMoves()` | Retorna uma cópia da lista de movimentos legais. Somente `moves[0..count)` é válido; a capacidade fixa de 218 corresponde à revisão THC fixada. |
| `isLegal(move)` | Compara origem, destino e promoção com a lista legal atual. |
| `san(move, output)` | Produz SAN na posição atual, antes de aplicar o lance. Retorna falso para lance inválido e preserva `output`. |
| `apply(move)` | Consulta novamente a lista legal e aplica o lance encontrado. Retorna falso sem modificar posição ou histórico se não houver correspondência. |
| `status()` | Informa xeque, mate/afogamento e condições de empate separadamente. |
| `fen()` / `loadFen(text)` | Exportam e preparam posições para testes e diagnóstico. A carga bem-sucedida limpa o histórico de repetição; falha preserva o estado anterior. |

Índices inválidos, promoção ausente quando obrigatória, promoção em lance comum e valores desconhecidos do enum são rejeitados. Um movimento obtido anteriormente não recebe autorização permanente: `apply()` o revalida na posição corrente.

Todas as consultas são `const` e preservam posição, direitos, contadores e histórico de repetição. Cópias e atribuições de `ChessAdapter` mantêm o histórico disponível na THC e podem evoluir independentemente. As chamadas sobre uma mesma instância devem ser serializadas pelo chamador.

`loadFen()` usa o parser da THC em uma posição temporária, verifica `IsLegal()` e os limites básicos dos contadores, e somente então substitui o estado. Destina-se a posições de teste conhecidas; **não é um parser FEN estrito nem uma prova de alcançabilidade da posição**. Aceita o dialeto permissivo da THC e pressupõe metadados de roque/*en passant* coerentes. Não serve para recuperar uma partida a partir dos sensores. Exportar e recarregar FEN não preserva repetição; copiar o adaptador preserva.

## 2. Condições de encerramento

`PositionStatus.sideToMove` identifica o lado a jogar; quando `terminal` é `Checkmate` ou `Stalemate`, identifica o lado sem movimentos legais. `inCheck` informa se esse rei está sob ataque.

As condições de empate são observações independentes:

- `insufficientMaterial`: somente o resultado `DRAWTYPE_INSUFFICIENT_AUTO` da THC. Cobre reis isolados e rei com bispo/cavalo contra rei; não promete reconhecer toda posição morta.
- `fiftyMoveClaim`: contador atual de pelo menos 100 meio-lances sem captura ou movimento de peão.
- `threefoldRepetitionClaim`: a posição atual aparece pelo menos três vezes no histórico disponível.

O adaptador não encerra a partida por esses indicadores, não considera uma intenção de lance ainda não aplicado e não escolhe a política de reclamação de empate. O resultado THC `DRAWTYPE_INSUFFICIENT`, dependente do lado que consulta, não é convertido em empate automático. Não há adjudicação adicional de cinco repetições, 75 lances, abandono ou acordo entre jogadores.

## 3. Decisões de implementação

A classe é concreta e armazena `thc::ChessRules` por valor, sem interface virtual ou alocação adicional para esconder a implementação. O header inclui a dependência para definir esse membro privado, mas as operações públicas não recebem nem devolvem tipos THC. A conversão `índice ^ 56` fica interna ao adaptador.

A seleção compara os campos do movimento com `GenLegalMoveList`, sem usar o parser permissivo `TerseIn`. Somente um `thc::Move` produzido pela biblioteca chega a `PlayMove`; as regras não foram reimplementadas. `reset()` atribui uma nova `ChessRules`, pois `Init()` isolado não restaura as peças.

Consultas que fazem simulações internas na THC trabalham sobre uma cópia local do tipo completo. Isso simplifica a preservação do estado ao custo de pilha adicional. A lista de movimentos usa armazenamento fixo; SAN/FEN usam `std::string` e podem alocar memória. Não há promessa de ausência de heap na biblioteca.

Os fontes e a licença MIT ficam em [src/thc](../firmware/xadrez/src/thc/LICENSE), sem modificações, no commit `aa217cb9b4d759cd7c07e11c98526b0187b0a64b`. O [manifesto](../firmware/xadrez/upstream.json) fixa os hashes. A cópia da avaliação anterior foi preservada como evidência histórica; o adaptador inclui sua própria cópia para que a biblioteca Arduino seja autocontida e não dependa da pasta de ensaios. Uma atualização futura deve ser deliberada e acompanhada de nova validação.

## 4. Compilação e testes

Na raiz, com CMake/MSVC do Visual Studio 2022 e Arduino CLI disponíveis:

```powershell
& .\firmware\xadrez\validar.ps1
```

Os parâmetros `-CMake` e `-ArduinoCli` permitem informar outros caminhos. Reutiliza-se o core Arduino-ESP32 **3.3.12**, FQBN `esp32:esp32:esp32`, sem instalação global da biblioteca ou download de dependências. Em um ambiente isolado que redirecione o perfil do Windows, apontar `ARDUINO_DIRECTORIES_DATA` para a instalação existente antes de executar o script; `ARDUINO_BUILD_CACHE_PATH` pode apontar para `firmware/xadrez/build/core-cache`.

O script confere os hashes upstream, compila o adaptador e os testes nativos com C++14 e `/W4 /WX`, executa os casos CTest e compila a sonda ESP32 com `--warnings all`. A THC é compilada intacta e seus avisos conhecidos não são promovidos a erros. Os intermediários ficam em `build/`, coberto pelas exclusões locais.

Os [18 casos automatizados](../firmware/xadrez/tests/adapter_tests.cpp) cobrem:

- Layout interno THC, posição inicial, orientação das 64 casas e lista legal.
- Rejeição de índices/promoções inválidos, proteção do rei e revalidação de lance antigo.
- Consultas e rejeições sem mutação; reinício, carga de posição e cópias com histórico.
- Capturas, quatro roques, perda do direito de roque, *en passant* e sua expiração.
- Quatro promoções para ambas as cores, promoção com captura e SAN correspondente.
- SAN com desambiguação, xeque e mate; afogamento, material insuficiente, 50 lances e repetição.

A sonda usa os mesmos fontes de teste e a biblioteca Arduino local por `--library`. Reserva **24 KiB de pilha apenas para a suíte**, que mantém cópias de tabuleiros durante as verificações; isso não define o orçamento do firmware futuro. Se executada posteriormente, publica os resultados por Serial a 115200 baud e em `adapterTestFailures`. Não acessa a matriz, botões ou SD.

Em **12/10/2026**, os **18/18 casos nativos passaram** e a compilação/link ESP32 foi concluída. O binário completo da sonda ocupa **322.824 bytes de flash** e **24.548 bytes de dados globais**. Esses valores incluem testes e core Arduino, não medem o custo isolado do adaptador ou o pico de pilha/heap. Os avisos de compilação observados pertencem aos fontes THC preservados.

O [resultado estruturado](../firmware/xadrez/validation/resultado.json) registra versões, comandos, hashes e etapas. O [JUnit](../firmware/xadrez/validation/native-tests.xml) identifica cada caso; o [log ESP32](../firmware/xadrez/validation/esp32-compile.log) registra avisos e tamanhos.

## 5. Validação embarcada

A [sonda instrumentada](../firmware/xadrez/tests/esp32_runtime/runtime_probe.cpp) reutiliza os mesmos 18 testes, incluindo `TestInternals()`, antes e depois de um ensaio de estabilidade. Não acessa sensores ou periféricos. O upload substitui o programa da placa pela sonda; ao terminar, ela aguarda outro comando serial.

Com a placa conectada e o driver USB instalado, identificar a porta com `arduino-cli board list` e executar, substituindo `COM3` pela porta encontrada:

```powershell
& .\firmware\xadrez\validar-esp32.ps1 -Port COM3 -DurationSeconds 180
```

O parâmetro `-CompileOnly` prepara o binário sem upload; não indica aprovação embarcada. O script verifica os fontes THC, compila, grava a placa e captura a serial a 115200 baud. Cada execução usa um identificador próprio e uma pasta datada em `firmware/xadrez/validation/hardware/`, contendo versões, comandos, hashes dos fontes/binários, logs de compilação/upload, captura serial e `resultado.json`. Início, upload e conclusão na placa são registrados separadamente; timeout, reinício, falhas funcionais ou resultado incompleto impedem aprovação.

### Método de medição

- **Tempos:** 10 aquecimentos e 200 amostras por operação/posição, com `esp_timer_get_time()` em microssegundos. Medem-se `legalMoves`, `isLegal`, rejeição de um lance com casas válidas, `san`, `apply` e `status`, nas posições inicial, Kiwipete, *en passant* e promoção. O relatório contém mínimo, mediana, média e máximo. Preparação/cópia da posição, validação dos retornos e Serial ficam fora do intervalo; o custo das leituras do relógio e do despacho não é subtraído. `apply()` inclui a revalidação prevista no contrato, e SAN recebe uma string inicialmente vazia em cada amostra.
- **Heap:** espaço interno endereçável por byte (`MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`), com bytes livres, mínimo e maior bloco. O monitor local do ESP-IDF separa os mínimos de cada fase daqueles do boot. Nos benchmarks, `heap_before_bytes` e `heap_after_bytes` são lidos com o monitor ativo; `heap_before_monitor_bytes` e `heap_after_monitor_bytes` permitem conferir sua restituição. O [monitor aloca armazenamento temporário](https://github.com/espressif/esp-idf/blob/v5.5.5/components/heap/heap_caps.c#L288-L338), que não deve ser atribuído ao adaptador. Os mínimos agregam regiões que podem atingir seus menores valores em momentos diferentes; constituem uma indicação conservadora, incluindo o runtime, não uma medida instantânea exclusiva do adaptador.
- **Pilha:** `uxTaskGetStackHighWaterMark(nullptr)` retorna bytes no ESP-IDF utilizado. A margem é cumulativa desde a criação do `loopTask`, com 24 KiB reservados para esta suíte; inclui testes, cópias locais e instrumentação. Não define o orçamento da aplicação nem comprova funcionamento com a pilha Arduino padrão.
- **Estabilidade:** repetição das 24 combinações de operações/posições, com verificação dos retornos, pausas para o escalonador e repetição da suíte a cada aproximadamente 10 segundos. Checkpoints registram heap, maior bloco, margem de pilha e integridade do heap. Nenhum watchdog é desativado. A duração é delimitada e não substitui ensaio do firmware integrado.

### Resultados na placa — 12/10/2026

Foram realizadas duas rodadas completas, com upload verificado e execução real pela **COM7**, na DevKit V1 informada pela equipe. O dispositivo identificou **ESP32-D0WD-V3, revisão 3.1, dois núcleos, 240 MHz, flash de 4 MiB e ausência de PSRAM**. Ambiente: Arduino-ESP32 3.3.12, ESP-IDF v5.5.5 e esptool 5.3.1; testes no `loopTask`, núcleo 1.

A [rodada final](../firmware/xadrez/validation/hardware/20261012T2053549253927Z/resultado.json) registra aprovação de compilação, upload e execução separadamente. O [log serial](../firmware/xadrez/validation/hardware/20261012T2053549253927Z/serial.log) contém os resultados recebidos da placa; seus registros até `end` foram conferidos com o JSON. A sonda final ocupa **348.259 bytes de flash** e **25.512 bytes de dados globais**. Os fontes do ChessAdapter, THC e testes funcionais existentes permaneceram iguais aos hashes da validação anterior.

A [conferência das imagens](../firmware/xadrez/validation/hardware/20261012T2053549253927Z/image-verification.json) distingue os binários dos caches `*_flashed.bin`, modificados pelo esptool durante o upload. O coletor passou a excluir esses caches do manifesto; a [versão usada no ensaio](../firmware/xadrez/validation/hardware/20261012T2053549253927Z/collector-used.ps1) foi preservada, e o relatório original não foi reescrito. Essa correção de metadados não alterou a imagem executada ou as medições.

- **Funcionalidade:** 18/18 casos aprovados antes e depois da repetição, mais 18 execuções intermediárias da suíte: **20 suítes completas, 360 casos aprovados**, incluindo 20 chamadas aprovadas a `TestInternals()`. Não ocorreram falhas, reinícios inesperados, watchdog ou corrupção de heap observada.
- **Estabilidade:** **709.752 operações em 180,175 segundos**, distribuídas por 29.573 lotes nas quatro posições; todas as verificações de retorno passaram. Os benchmarks acrescentaram 4.800 amostras medidas, além dos aquecimentos.
- **Heap:** durante a estabilidade, **243.692 bytes livres**, mínimo local agregado de **243.308 bytes** e maior bloco de **110.580 bytes**, sem queda entre checkpoints. O heap final, com monitor encerrado, foi **243.752 bytes**. Nos 24 benchmarks, heap antes/depois permaneceu igual; os **60 bytes temporários do monitor** foram restituídos em cada lote. Não houve retenção progressiva observada nesses ensaios.
- **Pilha:** margem mínima na rodada final de **11.160 bytes**, com **24.576 bytes reservados**; diferença de 13.416 bytes em relação à reserva. A [primeira rodada](../firmware/xadrez/validation/hardware/20261012T2049502004066Z/resultado.json) registrou margem de **11.144 bytes**, a menor entre as duas execuções. Esses valores incluem toda a suíte e os quadros de chamada da instrumentação, não apenas o adaptador.
- **Tamanhos no alvo:** `sizeof(ChessAdapter)=2140`, `sizeof(thc::ChessRules)=2140`, `sizeof(LegalMoves)=660` e `sizeof(thc::Move)=4` bytes.

Medianas por operação, em **microssegundos**, na rodada final:

| Operação | Inicial | Kiwipete | En passant | Promoção |
| --- | ---: | ---: | ---: | ---: |
| Geração de movimentos legais | 100 | 217 | 57 | 65 |
| Validação de lance legal | 90 | 210 | 47 | 55 |
| Rejeição de lance ilegal | 92 | 211 | 49 | 57 |
| SAN | 629 | 2.236 | 181 | 219 |
| Aplicação com revalidação | 90 | 210,5 | 48 | 55 |
| Estado da posição | 44 | 60 | 32 | 34 |

O maior tempo observado foi **2.245 µs**, em SAN de Kiwipete. Mínimos, médias e máximos das 24 combinações constam no JSON; não representam limites superiores para todas as posições ou para o firmware integrado. A posição inicial tinha 20 movimentos legais; Kiwipete, 48; *en passant*, 7; promoção, 9.

O heap antes da primeira suíte era 244.368 bytes, e o mínimo global já era 203.500 bytes, atingido antes do ensaio. A redução inicial de 616 bytes até o valor final inclui inicialização e saída de diagnóstico, sem isolamento de sua origem; não deve ser atribuída exclusivamente ao ChessAdapter. A análise de estabilidade usa os checkpoints e as comparações dentro de cada fase, com monitor nas mesmas condições.

### Ocorrências e parecer

A comunicação utilizou o driver Silicon Labs CP210x **11.6.0.420** e a porta **COM7**. Os uploads foram realizados com reinicialização automática, sem acionamento manual de BOOT. A preparação sem upload foi preservada como evidência distinta.

A primeira rodada já havia passado em 709.632 operações e 20 suítes. A instrumentação foi então corrigida para separar a alocação temporária do monitor de heap, e a rodada completa foi repetida. O [fonte da primeira sonda](../firmware/xadrez/validation/hardware/20261012T2049502004066Z/runtime_probe.cpp) foi preservado junto ao relatório correspondente.

No segundo upload, o esptool emitiu uma estimativa de cristal de 41,01 MHz antes de normalizá-la para 40 MHz. Essa [estimativa deriva do baudrate e divisor UART](https://github.com/espressif/esptool/blob/v5.3.1/esptool/loader.py#L1898-L1924), não de medição direta do cristal. Após o ensaio, duas consultas somente de leitura com `esptool --chip esp32 --port COM7 --baud 115200 --no-stub read-mem 0x3ff40014` retornaram [0x00800160](../firmware/xadrez/validation/hardware/20261012T2053549253927Z/uart-clock-check-1.log) e [0x00000161](../firmware/xadrez/validation/hardware/20261012T2053549253927Z/uart-clock-check-2.log), ambas sem aviso. Essas consultas reiniciaram a placa deliberadamente após os testes, sem mudar a flash. Não há evidência suficiente para atribuir defeito físico ao cristal ou modificar a configuração.

**O ChessAdapter e a revisão THC ensaiada são viáveis para prosseguir à integração no ESP32 adotado**, considerando as funcionalidades verificadas, os tempos observados e a memória disponível nesta sonda. Não foi identificada incompatibilidade de execução que exija alteração do módulo. O ensaio não comprova ausência universal de vazamentos nem dimensiona o orçamento final com tarefas e periféricos adicionais.

## 6. Pendências

Dimensionar e medir novamente pilha, heap e latência no firmware integrado. A suíte não foi executada com a pilha Arduino padrão de 8 KiB, e sua reserva de 24 KiB não deve ser transposta automaticamente para a aplicação. Permanecem os limites de histórico, FEN e condições de empate descritos no contrato; os testes não constituem certificação completa de todas as posições de xadrez.

A [interpretação das movimentações](firmware-interpretacao.md) utiliza o adaptador sem modificar seu contrato. Controle, interface de promoção, geração PGN e ligação com sensoriamento/SD continuam em suas etapas próprias. Os [testes físicos do tabuleiro](plano-testes-fisicos.md) não foram substituídos por esta validação.
