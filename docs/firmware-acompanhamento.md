# Acompanhamento da jogada

O módulo [MoveTracker](../firmware/acompanhamento/src/MoveTracker.h) implementa o acompanhamento previsto na [arquitetura](arquitetura.md): recebe ocupações do Controle da partida e conserva as alterações da tentativa para consulta posterior. Atende à parte de software de RF02 e à separação de responsabilidades de RNF02. A aquisição física e sua integração permanecem pendentes.

O código é C++14, sem dependência de Arduino, relógio real, periféricos ou biblioteca de xadrez. O [diagnóstico](firmware-diagnostico.md) permanece independente. Os diagramas de componentes e atividades continuam aplicáveis, sem mudança nas dependências entre módulos.

## 1. Representação e contrato

`Occupancy` é `std::uint64_t`: bit 1 significa casa ocupada, seguindo a orientação [definida para o tabuleiro](firmware-diagnostico.md#3-varredura-e-configuração). A identificação das peças não faz parte desse valor.

| Operação | Contrato |
| --- | --- |
| `observe(occupancy, timestampMs)` | Recebe um quadro completo e válido. A primeira chamada estabelece a referência inicial, sem gerar alterações. Nas seguintes, registra as casas retiradas e acrescentadas em um único quadro. |
| `snapshot()` | Devolve uma cópia independente de `AttemptSnapshot`, com ocupação inicial da tentativa, ocupação corrente, histórico e condição de confiabilidade. Atualizações posteriores não modificam essa cópia. |
| `discardAttempt()` | Limpa o histórico e usa a ocupação corrente como referência inicial da próxima tentativa. Não restaura peças nem altera posição lógica. |
| `markInputInterrupted()` | Informa falha de leitura ou perda conhecida de continuidade. Preserva a última ocupação válida, sem fabricar um quadro vazio. |

`AttemptSnapshot.frames[0..frameCount)` contém os quadros com alterações. As posições a partir de `frameCount` não são válidas e não devem ser consultadas: o descarte zera o contador, sem limpar o vetor, e novas alterações sobrescrevem integralmente as posições reutilizadas. Cada `FrameChanges` tem as máscaras `removed` e `added`, além do tempo informado pelo chamador, em milissegundos. As máscaras de um quadro são disjuntas. Uma casa retirada e depois recolocada aparece em dois quadros, mesmo quando a ocupação final coincide com a inicial.

A ordem do vetor corresponde à ordem de recebimento das observações. Não existe ordem atribuída às casas dentro de um mesmo quadro, nem inferência de simultaneidade física. Quadros sem alterações não consomem histórico. Tempos iguais ou o retorno a zero de um contador de 32 bits não reordenam nem mesclam quadros; o módulo não calcula intervalos ou aplica debounce.

## 2. Limites e confiabilidade

A capacidade está definida em `kHistoryCapacity`, no cabeçalho do módulo: **64 quadros com alterações por tentativa**, em armazenamento fixo, sem alocação dinâmica. Esse limite refere-se a quadros, não a casas ou lances. Foi escolhido como uma reserva simples para as sequências de uso previstas; não deriva de medição física.

| Campo de `AttemptSnapshot` | Significado |
| --- | --- |
| `initialized` | Existe pelo menos uma leitura válida de referência. Antes disso, os valores de ocupação não representam uma leitura. |
| `readingAvailable` | Uma leitura válida foi recebida desde a última interrupção informada. Não mede a idade da amostra. |
| `historyOverflow` | Ocorreu uma alteração que excedeu a capacidade. Preencher exatamente o histórico ainda não é estouro. |
| `inputInterrupted` | A aquisição perdeu continuidade durante uma tentativa já inicializada. |
| `reliable()` | Há referência e leitura disponível, sem estouro ou interrupção na tentativa. |

Ao perder confiabilidade, o módulo congela o prefixo do histórico já registrado. Novas leituras válidas continuam atualizando a ocupação corrente, mas não acrescentam transições ao histórico incompleto. As causas permanecem explícitas até o descarte; receber uma leitura válida não recupera as transições perdidas.

O descarte com leitura disponível começa uma tentativa limpa. Se a aquisição ainda estiver indisponível, o descarte mantém a indicação de interrupção. Nesse caso, uma leitura válida e um novo descarte são necessários para estabelecer outra tentativa confiável. Isso apenas define o ciclo de vida do histórico; não reconcilia o tabuleiro com uma partida. Falhas anteriores à primeira referência não contaminam a tentativa iniciada pela primeira leitura válida.

O Controle deve informar interrupções e consultar `reliable()` antes de usar a tentativa. A aquisição é responsável por produzir observações completas, tratar instabilidade dos sensores e determinar se houve uma pausa incompatível com sua operação. O módulo não detecta transições que aconteceram inteiramente entre duas leituras.

## 3. Uso pelo controle futuro

O fluxo previsto é entregar leituras válidas a `observe`; informar falhas com `markInputInterrupted`; consultar uma cópia da tentativa após a confirmação; e descartar o histórico ao encerrar a tentativa. A ordem entre leitura e confirmação será responsabilidade do Controle. As chamadas devem ser serializadas; não há sincronização entre tarefas ou interrupções.

O sistema pressupõe jogadores seguindo as regras e as instruções de uso. Não há mecanismos de reconstrução de movimentações arbitrárias, troca de identidade de peças ou reconciliação com a posição oficial. A validação de legalidade permanece atribuída aos módulos futuros de interpretação e biblioteca de xadrez.

Capturas, roque, *en passant* e promoção são sequências normais de ocupação para o acompanhamento. Os testes verificam a preservação dessas sequências; o módulo não reconhece sua natureza nem decide sua legalidade. A escolha da peça de promoção pertence à interface e à interpretação futuras.

## 4. Compilação e testes

O [script de validação](../firmware/acompanhamento/validar.ps1) usa CMake/CTest, MSVC do Visual Studio 2022 e Arduino CLI. Reutiliza o núcleo Arduino-ESP32 indicado no [guia de diagnóstico](firmware-diagnostico.md#1-preparação-e-compilação), sem instalar dependências. Na raiz:

```powershell
& .\firmware\acompanhamento\validar.ps1
```

Os caminhos padrão correspondem às ferramentas já encontradas nesta máquina. Para outra instalação, fornecer `-CMake 'caminho\cmake.exe'` e `-ArduinoCli 'caminho\arduino-cli.exe'`. O script configura MSVC x64 Release com C++14 e `/W4 /WX`, executa CTest e compila a [sonda ESP32](../firmware/acompanhamento/tests/esp32_compile/esp32_compile.ino) com `--warnings all` e `-Werror` para C++. A sonda liga o mesmo módulo, sem acessar periféricos; não é um firmware funcional de partida.

O CMake também permite executar somente os testes nativos:

```powershell
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
& $cmake -S .\firmware\acompanhamento -B .\firmware\acompanhamento\build\native -G 'Visual Studio 17 2022' -A x64
& $cmake --build .\firmware\acompanhamento\build\native --config Release
& $ctest --test-dir .\firmware\acompanhamento\build\native -C Release --output-on-failure --no-tests=error
```

Os [testes](../firmware/acompanhamento/tests/test_move_tracker.cpp) cobrem inicialização, quadros repetidos, movimento simples, retirada/recolocação, ordens de captura, dois roques, *en passant*, substituição na promoção, múltiplas casas por quadro, os 64 bits, descarte e correção direta, independência da cópia, limite/estouro, interrupções e retorno a zero do tempo. As verificações continuam ativas em Release e não dependem de `assert`.

O [resultado](../firmware/acompanhamento/validation/resultado.json) registra etapas, ferramentas e hashes dos fontes; o [relatório CTest](../firmware/acompanhamento/validation/native-tests.xml) registra os casos executados. Os logs ficam na mesma pasta e os intermediários em `firmware/acompanhamento/build/`, já coberta pelas exclusões locais. Os relatórios do diagnóstico e da integração elétrica/CAD mantêm seus escopos próprios.

Testes nativos usam observações sintéticas. Compilar para ESP32 não equivale a executar na placa, e nenhum desses resultados substitui os [testes físicos](plano-testes-fisicos.md).
