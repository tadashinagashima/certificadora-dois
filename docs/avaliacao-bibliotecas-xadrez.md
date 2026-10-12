# Avaliação de bibliotecas de xadrez

## 1. Recomendação e escopo

**Recomenda-se a THC Chess Library, na revisão fixada nesta avaliação, como candidata à integração futura.** Foi a única das três alternativas ensaiadas que passou nos casos funcionais mínimos e produziu um binário para o ESP32 clássico sem alterações no código upstream. A execução na placa, especialmente a verificação do layout de memória, permanece necessária antes da adoção definitiva.

A avaliação atende à escolha prevista no planejamento e aos [requisitos](requisitos.md) RF05, RF06, RF08, RF10 e RF11. Considera a [arquitetura existente](arquitetura.md), o prazo do projeto e a preservação das responsabilidades: regras e posição lógica na biblioteca; interpretação física, controle, interface e registro PGN em seus próprios módulos. Não houve integração com MoveTracker, sensoriamento ou firmware principal, nem implementação de interpretação de jogadas.

O sistema pressupõe uso correto do tabuleiro. Os casos de movimentos ilegais verificam a validação exigida por RF05 e a preservação da posição oficial; não introduzem recuperação de manipulações físicas arbitrárias. Capturas, roque, *en passant* e promoção foram avaliados como operações normais.

## 2. Ambiente e alternativas

Ensaios realizados em **12/10/2026**, com MSVC x64 Release, CMake/CTest 3.29.5, Arduino CLI 1.5.2-rc.1 e core Arduino-ESP32 **3.3.12**, FQBN `esp32:esp32:esp32`. O alvo corresponde à DevKit V1 adotada; não depende de PSRAM. As verificações nativas permanecem ativas em Release, sem depender de `assert`.

Foram selecionadas três bibliotecas C/C++ com licença MIT: uma distribuição simples em dois arquivos, uma biblioteca moderna de cabeçalho único e uma alternativa publicada para Arduino/ESP-IDF. Cada pasta contém versão fixada, fontes ou download com hash, licença, testes, script e evidências. Nenhuma fonte upstream foi corrigida para favorecer um resultado.

| Alternativa e revisão | Funcionalidade nativa | Arduino-ESP32 | Parecer |
| --- | --- | --- | --- |
| [THC](https://github.com/billforsternz/thc-chess-library/tree/aa217cb9b4d759cd7c07e11c98526b0187b0a64b), `aa217cb9b4d759cd7c07e11c98526b0187b0a64b` | **15/15 casos CTest aprovados**, C++14 | Compilação e link aprovados, com avisos upstream | Recomendada para a próxima etapa; execução no alvo pendente |
| [Disservin](https://github.com/Disservin/chess-library/tree/53e6a841dcda7059a2af363d85f785ef1817304a), cabeçalho 0.9.4, `53e6a841dcda7059a2af363d85f785ef1817304a` | **62 verificações aprovadas em 1 caso CTest**, C++17 | Link excede DRAM em **795.592 bytes** | Inadequada ao alvo atual sem adaptação significativa |
| [htcw_chess](https://github.com/codewitch-honey-crisis/htcw_chess/tree/5bdd175c88e2d18202bf136544813b11256a2bb0), 0.5.3, `5bdd175c88e2d18202bf136544813b11256a2bb0` | **6/11 casos CTest aprovados**, fonte C99 | Falha de compilação por declaração de `memcpy` | Não recomendada nesta revisão |

As quantidades refletem suítes mínimas específicas por API, não um ranking por número de testes. Não foi executada uma certificação completa das regras nem um comparativo de velocidade.

## 3. Evidências da THC

Os [testes da candidata](../firmware/avaliacao-xadrez/thc/tests/essential_tests.cpp) verificam:

| Área | Casos cobertos |
| --- | --- |
| Representação e consultas | FEN inicial e após e4; 20 movimentos iniciais; conversão de todas as 64 casas; consultas sem alteração da posição; `TestInternals()` upstream |
| Legalidade e efeitos | Avanço simples; captura; rejeição de turno errado, peça bloqueada, avanço de três casas e movimento que expõe o próprio rei |
| Roque | Dois lados para ambas as cores; posição final de rei/torre e turno; rejeição de passagem por casa atacada; direito não restaurado ao devolver a torre |
| *En passant* | Captura e retirada do peão correto; rejeição sem direito e ao expor o próprio rei |
| Promoção | Dama, torre, bispo e cavalo, para ambas as cores, com SAN correspondente |
| SAN | Captura, roques, promoções, desambiguação por coluna/linha, xeque e mate, calculados na posição anterior ao lance |
| Encerramento | Mate do louco, afogamento, reis isolados, limiar de 100 meio-lances e repetição tripla em sequência de cavalos |
| Cópia e simulação | Cópia independente de `ChessRules`, preservando repetição; `PushMove`/`PopMove` restaurando FEN |
| Contagem de movimentos (*perft*) | Posição inicial: 20, 400 e 8.902 nós nas profundidades 1–3; Kiwipete: 48 e 2.039 nas profundidades 1–2 |

O [resultado estruturado](../firmware/avaliacao-xadrez/thc/validation/resultado.json), o [CTest](../firmware/avaliacao-xadrez/thc/validation/native-tests.xml) e o [log ESP32](../firmware/avaliacao-xadrez/thc/validation/esp32-compile.log) registram ferramentas, hashes e resultados. A sonda ESP32 compila os mesmos fontes de teste, mas **não foi carregada nem executada na placa**.

O binário completo da sonda, incluindo o core Arduino e os testes, ocupa **294.560 bytes de flash** e **24.532 bytes de dados globais**. Esses valores demonstram viabilidade de link desta sonda; não medem o custo isolado da biblioteca nem o pico de pilha/heap do firmware futuro. No MSVC x64, `sizeof(ChessRules)=2144`, `sizeof(ChessPosition)=88`, `sizeof(Move)=4` e `sizeof(MOVELIST)=876`; esses tamanhos não devem ser extrapolados como medição do ESP32.

### Limitações e decisões para uma integração futura

- **Portabilidade:** o upstream usa pressupostos sobre bitfields e layout de memória em `DETAIL_ADDR`. `TestInternals()` passou no computador; a mesma verificação e os casos essenciais precisam executar no ESP32. Compilar `static_assert(sizeof(Move)==4)` no alvo cobre apenas parte desses pressupostos.
- **Coordenadas:** THC usa `a8=0` e `h1=63`; o projeto usa `a1=0` e `h8=63`. A conversão `índice ^ 56` foi verificada no ensaio para todas as casas. A conversão de índices e a construção da ocupação de 64 bits caberão à futura interface da biblioteca, sem alterar MoveTracker.
- **Validação e aplicação:** `PlayMove` pressupõe um movimento válido. A interface futura deve selecionar um lance da lista legal, com promoção explícita, e só então aplicá-lo após a confirmação. `TerseIn`, usado no ensaio, consulta movimentos legais, mas seu parser é permissivo com sufixos de promoção; não é um contrato apropriado para validar entradas arbitrárias.
- **Consultas e histórico:** `MOVELIST` permite enumeração em array fixo. SAN/FEN usam `std::string`. O histórico e a pilha de detalhes têm 256 entradas; não são armazenamento de uma partida inteira. Para preservar repetição em cópias, manter o tipo completo `ChessRules`: reconstruir apenas de FEN ou `ChessPosition` perde o histórico. `PlayMove` mantém os contadores da partida; `PushMove`/`PopMove` servem à simulação.
- **Empates:** os testes comprovam os casos citados, não toda posição morta possível. `IsDraw` distingue resultados de insuficiência de material e recebe o lado que consulta. A política de encerramento automático ou reclamação de empate ainda deve ser definida no Controle; não se deve converter indiscriminadamente todo resultado da API em empate automático. Não foi validada adjudicação completa das regras de cinco repetições/75 lances.
- **Manutenção e avisos:** a revisão consultada é de 17/02/2023. A compilação ESP32 emite avisos upstream, incluindo índices `char`, variável não utilizada e *fallthrough*. O ensaio preservou o fonte e registrou os avisos; não aplica `-Werror` ao upstream. O código dos testes nativos usa `/W4 /WX`.
- **Registro:** a emissão de SAN foi testada. Cabeçalhos, resultado, sequência PGN e gravação em SD continuam fora da biblioteca avaliada e não foram implementados.

## 4. Impedimentos das demais alternativas

Na **Disservin**, a inclusão direta no `.ino` colide com a macro Arduino `sq(x)`. Colocar a biblioteca intacta em um `.cpp` separado resolve essa colisão, mas revela o impedimento principal: as tabelas mutáveis de ataques de torres e bispos somam **861.184 bytes**, e o link ultrapassa a DRAM disponível. Redesenhar tabelas, recorrer a PSRAM ou trocar a placa acrescentaria trabalho incompatível com a decisão eletrônica atual e o prazo. O [relatório específico](../firmware/avaliacao-xadrez/disservin/README.md) registra os símbolos do objeto e os dois logs de compilação.

Na **htcw_chess**, falharam roque, expiração do *en passant*, proteção do próprio rei, identificação da cor em mate e reconhecimento de afogamento. O roque foi testado na codificação da própria API, rei para a casa da torre, após uma sequência normal de abertura. Também faltam APIs de SAN/FEN, repetição e contador dos 50 lances. A falha de compilação por `memcpy` é adicional; corrigir apenas o include não eliminaria os impedimentos funcionais. Os casos, posições e saídas estão no [relatório específico](../firmware/avaliacao-xadrez/htcw/README.md).

## 5. Reprodução e pendências

Executar da raiz, usando as ferramentas instaladas e os fontes fixados:

```powershell
& .\firmware\avaliacao-xadrez\thc\validar.ps1
& .\firmware\avaliacao-xadrez\disservin\avaliar.ps1
& .\firmware\avaliacao-xadrez\htcw\validar.ps1
```

Cada script grava seu próprio `validation/resultado.json`, logs e JUnit; os intermediários ficam em `build/`, excluído localmente. THC e htcw incluem a cópia mínima licenciada; Disservin baixa os arquivos da revisão fixada quando ausentes e confere os hashes. Os parâmetros de caminhos e comportamentos de saída estão nos READMEs de [THC](../firmware/avaliacao-xadrez/thc/README.md), [Disservin](../firmware/avaliacao-xadrez/disservin/README.md) e [htcw](../firmware/avaliacao-xadrez/htcw/README.md). O script htcw retorna falha deliberadamente porque os testes falham; o coletor Disservin registra as falhas de alvo nos campos próprios, sem transformá-las em aprovação de compatibilidade.

Para a próxima etapa, recomenda-se executar a sonda THC no ESP32 e observar pilha/heap antes de fixar sua adoção. Depois, uma interface pequena poderá expor posição, movimentos legais, aplicação confirmada, SAN e condições de encerramento aos módulos previstos. Nenhuma dessas etapas foi iniciada nesta avaliação. Os [testes físicos do tabuleiro](plano-testes-fisicos.md) continuam pendentes e independentes destes resultados.
