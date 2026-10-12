# Avaliação isolada: htcw_chess

**Não recomendada nesta versão.** A biblioteca compila nativamente, mas cinco dos onze testes mínimos de regras falharam. A compilação Arduino-ESP32 3.3.12 também falhou. O problema funcional inclui o resultado de um roque normal; não depende de movimentação física incorreta pelo usuário.

## Fonte e escopo

- Projeto: [codewitch-honey-crisis/htcw_chess](https://github.com/codewitch-honey-crisis/htcw_chess).
- Versão/tag: **0.5.3**, commit [`5bdd175c88e2d18202bf136544813b11256a2bb0`](https://github.com/codewitch-honey-crisis/htcw_chess/tree/5bdd175c88e2d18202bf136544813b11256a2bb0).
- Licença: [MIT original](vendor/htcw_chess/LICENSE), preservada junto aos fontes.
- Cópia mínima em `vendor/htcw_chess/`: `src/chess.h`, `src/source/chess.c`, `library.properties` e `LICENSE`, sem correções locais.
- Os arquivos foram copiados do checkout dessa tag. A [proveniência](validation/proveniencia.json) registra os objetos Git originais e hashes SHA-256 da cópia; [resultado.json](validation/resultado.json) registra fontes dos testes e ferramentas.

Não houve instalação global da biblioteca, integração ao firmware, alteração do MoveTracker ou implementação de interpretação/registro. A API é C; os testes são C++14. Os fontes usam arrays fixos e não chamam alocação dinâmica. A indexação a1=0/h8=63 coincide com a ocupação do projeto, mas a API entrega identificadores de peças por casa, não uma máscara de ocupação pronta.

## Testes mínimos

Os testes verificam requisitos de regras, não adotam falhas conhecidas como resultados aprovados. Portanto, `validar.ps1` retorna código **1** na versão avaliada. O CTest preserva cinco falhas nos relatórios.

| Caso | Resultado nativo |
| --- | --- |
| Posição inicial, 20 movimentos e consulta sem mutação | Aprovado |
| Movimento simples e rejeição de avanço de três casas, sem alterar posição | Aprovado |
| Captura `1.e4 d5 2.exd5` | Aprovado |
| *En passant* imediato | Aprovado |
| Expiração do *en passant* | **Falhou:** aceitou `1.e4 a6 2.e5 d5 3.Nf3 a5 4.exd6 e.p.` |
| Roque após `1.e4 e5 2.Nf3 Nc6 3.Bc4 Nf6` | **Falhou:** rei em h1, torre em e1, mantendo turno das brancas |
| Roque atravessando f1 atacada por torre em f8 | Aprovado |
| Mover torre cravada e2-a2, expondo rei e1 à torre e8 | **Falhou:** movimento aceito |
| Quatro promoções: dama, torre, bispo e cavalo | Aprovado |
| Mate após `1.f3 e5 2.g4 Qh4#` | **Falhou:** apontou negras em mate em lugar das brancas |
| Afogamento: rei preto a8 contra rei branco c6 e dama branca b6 | **Falhou:** informou partida em andamento |

O teste de roque usa a codificação encontrada no fonte da biblioteca, origem do rei e casa da torre (`e1` → `h1`), e confere o resultado convencional esperado em g1/f1 e a troca de turno. Logo, a falha não resulta de exigir uma codificação de lance diferente da upstream.

Posições reduzidas de promoção, cravada, ataque ao trajeto e afogamento são montadas diretamente na estrutura publicada `chess_game_t`, incluindo posições dos reis e bloqueio de roque. Isso é uma facilidade exclusiva do ensaio: a biblioteca não oferece carregamento FEN. Os demais casos partem de `chess_init()` e sequências de lances.

## Funcionalidades ausentes e limites

A inspeção do cabeçalho e da implementação não encontrou API SAN/PGN, leitura/escrita FEN, contador de meio-lances ou histórico de posições para repetição. `chess_status()` expõe apenas normal, xeque, mate e afogamento; portanto repetição, regra dos 50 lances e material insuficiente exigiriam trabalho adicional. Não foram implementadas essas extensões.

O armazenamento de estado é pequeno (`sizeof(chess_game_t)` aparece no relatório do teste inicial), mas tamanho reduzido não compensa as falhas em RF05/RF10. A avaliação para após esse conjunto mínimo; não inclui busca de todos os defeitos, benchmark, perft extensivo ou correções da biblioteca.

## Reprodução

Da raiz do repositório, com as ferramentas já disponíveis:

```powershell
& .\firmware\avaliacao-xadrez\htcw\validar.ps1
```

`-NativeOnly` executa apenas CMake/MSVC/CTest. `-CMake` e `-ArduinoCli` permitem apontar outras instalações. Intermediários ficam em `build/`, já excluído localmente; evidências ficam em [validation](validation/resultado.json).

O ensaio nativo compila C99 upstream com `/W4` e define `_CRT_SECURE_NO_WARNINGS` para permitir `sprintf` padrão no MSVC; os testes usam `/W4 /WX`. Os avisos upstream não são promovidos a erros. A sonda Arduino usa `--warnings all`, `--jobs 2`, FQBN `esp32:esp32:esp32` e core instalado 3.3.12. Ela referencia geração de movimentos, aplicação, status e promoção para verificar compilação/vinculação; não executa a suíte nativa na placa.

O [log ESP32](validation/esp32-compile.log) registra erro de declaração implícita de `memcpy` em `chess.c:487`: o fonte inclui `memory.h`, e o compilador sugere `string.h`. Não foi aplicado workaround nem correção upstream. Como a vinculação não foi concluída, não há medida de flash/RAM estática da sonda.

Compilação para ESP32 não comprova execução, consumo máximo de pilha/heap, alimentação ou integração física. Não foi feito upload.
