# Requisitos do Sistema — Tabuleiro de Xadrez Sensorial

**Disciplina:** Certificadora da Competência 2  
**Curso:** Engenharia de Computação — UTFPR, Câmpus Cornélio Procópio

## 1. Objetivo e escopo

Desenvolver um protótipo de tabuleiro de xadrez 8×8 para partidas presenciais entre dois jogadores, capaz de detectar a ocupação das casas, interpretar e validar as jogadas e registrar digitalmente a partida em formato PGN (*Portable Game Notation*).

Os requisitos a seguir foram elaborados a partir do planejamento do projeto e descrevem as funcionalidades e restrições previstas para o protótipo final.

## 2. Requisitos funcionais

| ID | Requisito | Critério de aceitação |
| --- | --- | --- |
| **RF01** | O sistema deve detectar a ocupação (ocupada ou vazia) de cada uma das 64 casas do tabuleiro. | A leitura da matriz 8×8 corresponde à ocupação física das casas testadas. |
| **RF02** | O sistema deve acompanhar as alterações de ocupação ocorridas durante uma jogada, inclusive estados intermediários. | A sequência de alterações pode ser consultada ou processada até a confirmação da jogada. |
| **RF03** | O sistema deve permitir que o jogador confirme o término da movimentação física por meio de um botão. | O acionamento do botão inicia a interpretação da jogada realizada. |
| **RF04** | O sistema deve identificar um movimento candidato a partir do estado lógico da partida, da ocupação física e das alterações observadas. | Em cenários de teste, o movimento interpretado corresponde à movimentação física executada. |
| **RF05** | O sistema deve validar o movimento candidato segundo as regras do xadrez, utilizando uma biblioteca integrada ao firmware. | Lances legais são aceitos e lances ilegais são rejeitados. |
| **RF06** | O sistema deve contemplar movimentos comuns e situações especiais: capturas, roque, *en passant* e promoção de peão. | Casos de teste para cada tipo de movimento são interpretados e validados corretamente. |
| **RF07** | O sistema deve manter um estado lógico oficial da partida e atualizá-lo somente após a confirmação e a validação de uma jogada. | Antes da validação, o estado lógico permanece inalterado; após um lance válido, reflete a nova posição. |
| **RF08** | O sistema deve rejeitar jogadas inválidas, preservar o estado lógico anterior e sinalizar o erro ao jogador. | Uma jogada inválida não modifica a posição lógica e produz indicação visual e sonora. |
| **RF09** | Em uma promoção de peão, o sistema deve permitir escolher Dama, Torre, Bispo ou Cavalo por meio de um botão que percorre ciclicamente as opções, com indicação visual da escolha. | Cada uma das quatro opções pode ser selecionada e associada à jogada de promoção. |
| **RF10** | O sistema deve disponibilizar um comando de encerramento manual da partida, incluindo situações como abandono. | O acionamento do comando encerra a partida no sistema. |
| **RF11** | O sistema deve registrar os lances válidos da partida em formato PGN. | O arquivo gerado representa a sequência de jogadas aceitas em formato PGN. |
| **RF12** | O sistema deve armazenar o registro da partida em cartão microSD removível. | O arquivo PGN é gravado no cartão e pode ser acessado posteriormente. |
| **RF13** | O sistema deve identificar condições de encerramento da partida por meio da biblioteca de xadrez. | Os cenários de encerramento contemplados pela biblioteca são reconhecidos nos testes. |

## 3. Requisitos não funcionais

| ID | Requisito | Critério de aceitação |
| --- | --- | --- |
| **RNF01** | O sensoriamento deve permitir identificar a ocupação das casas de forma confiável, sem acionamento indevido de casas adjacentes. | Ensaios com o protótipo de uma casa e, posteriormente, com a matriz demonstram a detecção esperada e verificam interferências entre casas. |
| **RNF02** | O firmware deve ser desenvolvido em C/C++ para ESP32, utilizando o ecossistema Arduino. | O firmware pode ser compilado e executado no ESP32 utilizado no projeto. |
| **RNF03** | O firmware deve separar as responsabilidades de sensoriamento, acompanhamento e interpretação da jogada, biblioteca de xadrez, interface com o usuário e registro da partida. | A organização do código permite identificar os módulos ou funções responsáveis por essas atividades. |
| **RNF04** | O sistema deve operar com alimentação do ESP32 por USB e níveis elétricos compatíveis com o microcontrolador. | Os circuitos são alimentados pela configuração prevista e suas interfaces utilizam níveis compatíveis com o ESP32. |
| **RNF05** | O desenvolvimento do protótipo deve respeitar o orçamento máximo de R$ 500,00 previsto no planejamento. | O total de despesas do projeto não ultrapassa o limite estabelecido. |

## 4. Restrições e definições de implementação

- **Estrutura física:** dimensões externas aproximadas de 35 × 35 cm, área de jogo de 30 × 30 cm, base de MDF e tabuleiro de vidro removível de 3 mm.
- **Peças e sensores:** peças comerciais adaptadas com ímãs de ferrite em disco de aproximadamente Ø12 × 3 mm; um Reed Switch sob cada casa.
- **Matriz elétrica:** 64 Reed Switches, 64 diodos 1N4148 e organização em oito linhas e oito colunas.
- **Leitura da matriz:** expansor MCP23017 conectado ao ESP32 por I²C.
- **Armazenamento:** cartão microSD de 8 GB com adaptador SD, conectado ao ESP32 por SPI, mantendo o cartão acessível para remoção.
- **Interface:** botão de confirmação, seleção de promoção, comando de encerramento e sinalização visual e sonora de erros, conforme a implementação definida pela equipe.
- **Uso das peças:** durante a partida, as peças devem ser levantadas e colocadas na casa de destino, evitando arrastá-las sobre o tabuleiro.

## 5. Funcionalidade opcional

**Display OLED 0,96" SSD1315 (I²C):** poderá apresentar movimentos, estado da partida, seleção de promoção e mensagens ao jogador. Sua inclusão no protótipo final dependerá dos testes de integração, da complexidade e do tempo disponível. **Não constitui requisito obrigatório de entrega.**

## 6. Pontos a validar durante o desenvolvimento

- Confiabilidade do conjunto Reed Switch, ímã e vidro de 3 mm, incluindo a interferência entre casas adjacentes.
- Definição da biblioteca de xadrez compatível com o ESP32 e com as funcionalidades necessárias.
- Detalhamento da interface física e da sinalização ao jogador.

Esses pontos exigem testes ou decisões de implementação e não são considerados funcionalidades já validadas.
