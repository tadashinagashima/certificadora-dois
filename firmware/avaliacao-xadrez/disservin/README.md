# Avaliação isolada: Disservin/chess-library

**Não recomendada para a DevKit V1 atual, sem alterações na biblioteca.** A geração de movimentos legais compila como C++ separado, mas o link para ESP32 clássico falha por excesso de DRAM. Esta pasta contém somente a avaliação, sem integração com diagnóstico, MoveTracker ou firmware funcional.

## Versão e reprodução

Avaliação em 12/10/2026. Fonte primária: [repositório upstream](https://github.com/Disservin/chess-library), commit [`53e6a841dcda7059a2af363d85f785ef1817304a`](https://github.com/Disservin/chess-library/commit/53e6a841dcda7059a2af363d85f785ef1817304a), de 26/07/2026; cabeçalho identifica versão 0.9.4. Licença MIT, com preservação de copyright e licença nas cópias. O [manifesto](upstream.json) fixa SHA-256 do cabeçalho e da licença. Não há patch no upstream.

Na raiz do repositório:

```powershell
& .\firmware\avaliacao-xadrez\disservin\avaliar.ps1
```

O script usa MSVC/CMake, Arduino CLI e o core ESP32 3.3.12 já instalados; baixa somente os dois arquivos fixados em `build/upstream/` quando ausentes e confere os hashes. Cópias de compilação, objetos e executáveis ficam em `build/`, já excluída pelas regras locais. Os parâmetros `-CMake`, `-ArduinoCli` e `-Esp32Nm` permitem ajustar os caminhos. A execução precisa de rede apenas para o primeiro download. Falhas de compilação das sondas são registradas como resultado da avaliação, e não tratadas como falha do script de coleta; falhas dos testes nativos são propagadas.

## Evidências

| Verificação | Resultado |
| --- | --- |
| MSVC x64 Release, C++17, `/W4 /EHsc` | Compilação e **62 verificações aprovadas**, em um teste CTest. As verificações não dependem de `assert`. |
| Inclusão direta em `.ino`, `esp32:esp32:esp32`, core 3.3.12 | Falha: a macro Arduino `sq(x)` colide com `Square sq(f, back_rank)` no cabeçalho. |
| Cabeçalho intacto em `.cpp` separado de `Arduino.h` | Compila o objeto; o link falha: `.dram0.bss` excede `dram0_0_seg` em **795.592 bytes**. |
| Símbolos Xtensa do objeto | `RookAttacks`: 819.200 bytes; `BishopAttacks`: 41.984 bytes; total **861.184 bytes**, além de 4.096 bytes das duas tabelas auxiliares. |

Os [testes nativos](tests.cpp) verificam 20 movimentos iniciais, ocupação a1=0/h8=63, rejeição de `e2e5`, avanço simples, captura, dois roques brancos, *en passant*, quatro promoções, SAN correspondente, consulta SAN sem alteração de estado, cópia independente e retorno integral por `unmakeMove`. Verificam ainda mate, afogamento, material insuficiente, 50 movimentos e repetição tripla com histórico.

São casos mínimos escolhidos para a avaliação; não constituem prova completa das regras nem substituem a suíte upstream. Os tamanhos nativos `sizeof(Board)=248` e `sizeof(Movelist)=1028` são de MSVC x64, não medições de heap no ESP32. A representação compacta de 24 bytes anunciada pelo upstream não inclui tabelas globais, histórico ou o objeto `Board` completo.

As sondas mantêm a geração legal no link por um resultado `volatile`; não acessam sensores ou outros periféricos. A segunda sonda resolve apenas a colisão de macros por separação normal de unidades C++, sem editar a biblioteca. Não foram tentados patches de tabelas, PSRAM ou mudança de placa, que extrapolariam a avaliação da arquitetura adotada.

O [resultado estruturado](validation/resultado.json), o [log nativo](validation/native-tests.log), o [CTest](validation/native-tests.xml), os logs de [inclusão direta](validation/esp32-compile.log) e [link isolado](validation/esp32-isolated-compile.log) e os [símbolos das tabelas](validation/esp32-table-symbols.log) permitem conferir os resultados.

## Contratos e limitações relevantes

- API C++17, somente cabeçalho, com movimentos legais, ocupação de 64 bits, SAN, aplicação/desfazimento e encerramento. As funcionalidades são convenientes para a arquitetura proposta, mas não compensam o bloqueio de memória no alvo.
- `uciToMove` converte uma representação; `makeMove` não substitui validação. Os testes verificam pertencimento à lista legal antes de aplicar. Roque é representado internamente como rei para a casa da torre; um futuro adaptador precisaria converter essa representação.
- `Board` usa `std::vector` para histórico, com reserva inicial para 256 estados; strings e outras operações também usam alocação dinâmica. `sizeof(Board)` não inclui essas alocações.
- `isGameOver` agrega mate, afogamento, material insuficiente, 50 movimentos e repetição tripla. A inspeção do código mostra que material insuficiente é uma verificação básica; não é prova geral de posição morta. A política de reclamação de empate versus encerramento automático precisaria ser definida pelo controle; não foi implementada nesta avaliação.
- SAN cobre a notação do lance. Produzir cabeçalhos, resultado e persistência de um PGN completo permanece responsabilidade do registro da partida.
- Nenhum binário utilizável da sonda foi produzido para upload. Não houve execução em placa nem medição física de memória, tempo, alimentação ou sensoriamento.
