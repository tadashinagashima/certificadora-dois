# Firmware principal — simulação Serial

A aplicação em [firmware/principal](../firmware/principal/platformio.ini) integra os cinco módulos da [arquitetura](arquitetura.md) em uma partida controlada por entradas simuladas na Serial. Executa no ESP32 sem matriz, botões, LEDs, buzzer ou SD. O [diagnóstico físico](firmware-diagnostico.md) permanece independente.

## 1. Configuração

O [platformio.ini](../firmware/principal/platformio.ini) fixa a distribuição comunitária [pioarduino 55.03.312](https://github.com/pioarduino/platform-espressif32/releases/tag/55.03.312), com Arduino-ESP32 **3.3.12** e ESP-IDF **5.5.5**. O PlatformIO Core adotado é **6.2.0**. A aplicação também verifica a versão Arduino em compilação.

O alvo `esp32dev` corresponde ao ESP32 clássico/variante `esp32` usado pelo FQBN `esp32:esp32:esp32` das validações anteriores: CPU a 240 MHz, flash de 4 MiB, sem PSRAM e partições `default.csv`. A configuração explicita flash QIO a 80 MHz, conforme as opções padrão daquele FQBN; o empacotamento ESP32 utiliza cabeçalho DIO para inicializar o bootloader QIO. A pinagem continua definida em [eletronica.md](eletronica.md), sem acesso a GPIOs nesta aplicação.

As cinco dependências são referências `symlink://` aos diretórios locais, sem cópia ou atualização dos módulos. A THC continua na versão fixada pelo [manifesto existente](../firmware/xadrez/upstream.json). O core e os intermediários PlatformIO ficam em `firmware/principal/build/`, já coberto pelas exclusões locais. Não se cria `.gitignore`.

## 2. Coordenação da partida

[SerialGame](../firmware/principal/src/SerialGame.cpp) contém a sessão e a interface textual, em C++14 independente de Arduino. O [main.cpp](../firmware/principal/src/main.cpp) cuida somente de Serial, tempo, diagnóstico e entrega das linhas. Todas as operações dos módulos são serializadas no `loopTask`; o callback de erro UART apenas sinaliza perda de dados por uma variável atômica.

A confirmação trabalha numa cópia do Controle. Quando aceita, outra cópia do Registro recebe a posição anterior ainda intacta, o resultado da confirmação e a nova posição. Se houver encerramento automático, ele também é registrado antes de publicar as duas cópias. Cópias preservam o histórico de repetição; não há reconstrução por FEN. Rejeições e promoção pendente atualizam a tentativa do Controle sem acrescentar lances.

Qualquer erro devolvido pelo Registro mantém posição oficial e PGN anteriores e ativa `fault`. Nesse estado, são permitidos diagnóstico, exportação e `new`; comandos de partida ficam bloqueados. Assim, o esgotamento de `kMovetextCapacity` não publica um lance sem registro. O usuário pode exportar o prefixo íntegro antes de reiniciar. Essa proteção cobre retornos dos contratos existentes, não falha geral do alocador ou perda de alimentação.

Abandono e encerramento manual passam pelo Controle e pelo Registro. `new` substitui a partida e exige uma nova observação compatível. Os metadados PGN permanecem desconhecidos (`?`); não há relógio ou cadastro de jogadores. PGN e partida existem somente em RAM: exportar antes de `new`, reset ou desligamento.

A retomada de uma tentativa segue o [contrato do Controle](firmware-controle.md): após rejeição ou interrupção, fornecer uma nova ocupação igual à oficial. Isso não recupera uma partida após reinicialização do ESP32, nem reconstrói a identidade de peças.

## 3. Protocolo Serial v1

115200 baud, 8N1. Comandos ASCII minúsculos, um por linha LF ou CRLF, sem espaços adicionais; casas `a1`–`h8`. Esperar `DONE` antes de enviar o próximo comando. Todas as respostas de comando terminam em uma linha `DONE`; o boot termina em `READY protocol=1`. `ERROR` informa entrada rejeitada ou bloqueio, e `CONFIRM` conserva o resultado público do Controle.

| Comando | Efeito |
| --- | --- |
| `help` | Lista comandos e cenários. |
| `new` / `new start` | Descarta a sessão e inicia a posição padrão. |
| `new CENARIO` | Inicia uma das posições conhecidas da tabela abaixo. |
| `observe HEX16` | Entrega uma ocupação completa de exatamente 16 dígitos hexadecimais; bit 0 = a1, bit 63 = h8. |
| `sync` | Simula uma nova observação igual à ocupação oficial, inclusive para retomar uma tentativa. |
| `lift e2` / `place e4` | Retira/coloca ocupação na última leitura disponível e entrega o quadro completo. Exige leitura anterior. |
| `confirm` | Interpreta e confirma a tentativa. Repetir uma confirmação aceita não duplica o lance. |
| `promote Q` / `R` / `B` / `N` | Seleciona dama, torre, bispo ou cavalo durante promoção pendente; exige nova confirmação. |
| `interrupt` | Simula perda de continuidade da aquisição. |
| `finish` | Encerra manualmente, com resultado `*`. |
| `resign white` / `resign black` | Abandono da cor indicada; registra a vitória da outra cor. |
| `status` | Estado, falha, lances registrados, promoção, confiabilidade, FEN, ocupações, encerramento e indicadores de empate. |
| `pgn` | Documento entre `PGN_BEGIN` e `PGN_END`. Não encerra a partida. |
| `metrics` | Tempo ativo, comandos, erros de entrada, maior duração de comando, heap, maior bloco livre, pilha, integridade e watchdog. |

`sync`, `lift` e `place` são recursos exclusivos da simulação: não leem nem movem peças reais. Capturas exigem observar a casa capturada vazia. No roque, entregar também as alterações da torre; no en passant, retirar o peão capturado. Concluir a substituição física simulada antes de escolher a promoção. Os módulos continuam sendo a única fonte das regras de xadrez.

| Cenário | Posição preparada |
| --- | --- |
| `start` | Posição inicial. |
| `castle_w`, `castle_b` | Reis e torres com os quatro direitos de roque; lado indicado a jogar. |
| `ep_w`, `ep_b` | Captura en passant e5–d6 ou e4–d3. |
| `promotion_w`, `promotion_b` | Peão a7–a8 ou a2–a1. |
| `promotion_capture_w`, `promotion_capture_b` | Promoção capturando torre em b8 ou b1. |
| `ambiguous` | Torre a1 pode capturar a8 ou b1; permite testar evidências conflitantes. |
| `mate`, `stalemate`, `insufficient` | Posições já encerradas, sem lances. |
| `busy` | Posição com muitos movimentos legais, incluindo Bxa6. |

As FENs estão definidas apenas em `SerialGame.cpp`; `status` permite consultá-las, e o PGN não padrão inclui `FEN`/`SetUp`. Não se aceita FEN arbitrária pela Serial, respeitando o escopo do parser THC.

Exemplo de uma partida completa, enviando uma linha e esperando `DONE` a cada passo:

```text
new
sync
lift f2
place f3
confirm
lift e7
place e5
confirm
lift g2
place g4
confirm
lift d8
place h4
confirm
pgn
```

O corpo do PGN será `1. f3 e5 2. g4 Qh4# 0-1` e o estado será `Finished`.

O receptor aceita até **96 caracteres** por linha. Linha excedente, NUL, controles ou CR fora do CRLF são descartados até LF, sem executar sufixos. Uma pausa de mais de dois segundos no meio da linha também descarta o restante até LF. Erros UART detectados e comandos malformados invalidam a tentativa quando há perda potencial de entrada. Receber novos quadros divergentes não restaura essa confiabilidade; usar `sync` ou uma observação oficial completa. Não se promete detectar corrupção que produza outro comando sintaticamente válido.

## 4. Reprodução

Na raiz, preparar o ambiente local uma vez (Python 3.13):

```powershell
New-Item -ItemType Directory -Force firmware/principal/build/tmp | Out-Null
$env:TEMP = Join-Path (Get-Location) 'firmware/principal/build/tmp'
$env:TMP = $env:TEMP
python -m venv --without-scm-ignore-files firmware/principal/build/venv
$python = '.\firmware\principal\build\venv\Scripts\python.exe'
$env:PLATFORMIO_CORE_DIR = Join-Path (Get-Location) 'firmware/principal/build/platformio-core'
$env:PYTHONIOENCODING = 'utf-8'
& $python -m pip install 'platformio==6.2.0'
```

Em sessões posteriores, definir novamente as variáveis. Compilar, identificar a porta e gravar (o upload substitui o programa da placa):

```powershell
& $python -m platformio run --project-dir firmware/principal --environment esp32
& $python -m serial.tools.list_ports -v
& $python -m platformio run --project-dir firmware/principal --environment esp32 --target upload --upload-port COM7
& $python -m platformio device monitor --project-dir firmware/principal --port COM7
```

Para os testes, fechar outros monitores. O coletor usa RTS para reiniciar a placa, confere o SHA-256 do ELF e executa o protocolo real; não instala firmware de teste distinto:

```powershell
$evidencia = 'firmware/principal/validation/' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
& $python firmware/principal/tests/integration.py --port COM7 --elf firmware/principal/build/pio/build/esp32/firmware.elf --output $evidencia --stress-seconds 180
```

A pasta de saída deve ser nova. O teste substitui as partidas em RAM e mantém logs TX/RX, relatório JSON e PGNs. Retorna código diferente de zero em falha. `--suite` seleciona um grupo; sem essa opção, executa todos. A comparação do hash comprova qual ELF foi executado.

Para CMake/MSVC, em build separado dos módulos:

```powershell
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
& $cmake -S firmware/principal -B firmware/principal/build/integration-native -G 'Visual Studio 17 2022' -A x64
& $cmake --build firmware/principal/build/integration-native --config Release
& $ctest --test-dir firmware/principal/build/integration-native -C Release --output-on-failure --no-tests=error
```

O CTest executa os 119 testes preservados e dez grupos da integração. O executável nativo usa o mesmo `SerialGame` e receptor de linhas; o script Python fornece observações e confere resultados/PGNs esperados independentemente. Timeout de bytes, watchdog e métricas de ESP32 são verificados somente na placa.

## 5. Validação da integração

Em **14/10/2026**, passaram **129/129 testes nativos** e os **dez grupos de integração executados no ESP32**. Cobertura: partidas completas com vitória de ambas as cores, captura, quatro roques, en passant de ambas as cores, quatro promoções com/sem captura para ambas as cores, promoção cancelada, rejeição/ambiguidade e retomada, interrupção, 64 quadros e estouro, comandos inválidos e fragmentados, encerramento/reinício, preservação de repetição e limite de 8192 bytes do Registro. As entradas são simuladas em todos esses testes.

O [resultado consolidado](../firmware/principal/validation/20261014T113428Z/resultado.json) relaciona as medições e os hashes. A rodada preserva [JUnit nativo](../firmware/principal/validation/20261014T113428Z/native-tests.xml), [compilação](../firmware/principal/validation/20261014T113428Z/platformio-build.log), [upload verificado](../firmware/principal/validation/20261014T113428Z/platformio-upload.log), [resultado embarcado](../firmware/principal/validation/20261014T113428Z/esp32/result.json) e [transcrição Serial TX/RX](../firmware/principal/validation/20261014T113428Z/esp32/serial.log). O hash ELF do boot corresponde ao [artefato compilado](../firmware/principal/validation/20261014T113428Z/artifacts.json). A imagem ocupa 324.871 B de flash de aplicação e 28.864 B de dados globais.

Os 37 PGNs salvos no ensaio nativo e no embarcado coincidem byte a byte, além das verificações contra sequências SAN e resultados esperados. Exemplos: [mate das pretas](../firmware/principal/validation/20261014T113428Z/esp32/fools_mate.pgn), [mate das brancas](../firmware/principal/validation/20261014T113428Z/esp32/scholars_mate.pgn) e [registro preservado ao esgotar a capacidade](../firmware/principal/validation/20261014T113428Z/esp32/capacity_preserved.pgn). A falha de capacidade bloqueou a confirmação sem alterar posição, número de lances ou PGN; `new` recuperou a sessão.

Depois dos cenários, o ensaio repetiu **132 ciclos durante 180,427 s**, com duas partidas completas, uma posição com muitos movimentos legais, exportações e reinícios em cada ciclo. Não houve reinicialização inesperada, falha de integridade do heap ou perda de registro no watchdog. O heap livre entre os pontos equivalentes antes/depois do trecho estabilizado teve variação de **0 B**. O menor heap livre histórico foi **163.536 B**, a menor margem de pilha **17.872 B**, e o menor maior-bloco-livre amostrado **110.580 B**. A maior duração de comando foi **711.184 µs**, incluindo transmissão Serial; a exportação de PGN longo participa dessa medida.

A [conferência de preservação](../firmware/principal/validation/20261014T113428Z/preservation.json) encontrou os 180 arquivos do levantamento inicial inalterados, incluindo fontes/testes dos módulos e evidências anteriores. Os hashes THC continuam correspondendo ao manifesto fixado.

A aplicação reserva **32 KiB** para `loopTask` devido às cópias e chamadas aninhadas durante confirmação/registro. Habilita o watchdog da tarefa e cede tempo ao runtime. `metrics` mede heap interno de 8 bits (livre, mínimo histórico e maior bloco), margem mínima de pilha, integridade do heap e registro no watchdog. A duração inclui processamento e transmissão da resposta, não representa somente custo do xadrez.

Não foram implementados sensoriamento, debounce físico, indicação por LEDs/buzzer, cadastro de metadados, gravação SD ou recuperação após perda de energia. Falta de memória do alocador não é convertida em `CapacityExceeded`; não foram forçados esgotamento do heap ou erro elétrico da UART. O ensaio de estabilidade tem duração limitada; não comprova operação indefinida. A execução física isolada do MoveTracker continua pendente, embora participe do fluxo integrado executado no ESP32.

## 6. Evidência preservada — Incremento 1


Incremento 1 validado em **14/10/2026**, no ESP32-D0WD-V3 revisão 3.1 conectado à COM7. Evidências em [validation/20261014T045915Z](../firmware/principal/validation/20261014T045915Z/):

- [Compilação e upload finais](../firmware/principal/validation/20261014T045915Z/platformio-build-upload-final.log): sucesso, com verificação dos dados gravados; flash de aplicação 303.864 B e dados globais 28.676 B.
- [Vínculo no ELF](../firmware/principal/validation/20261014T045915Z/link-symbols.json): símbolos definidos dos cinco módulos. [Hashes dos artefatos e fontes](../firmware/principal/validation/20261014T045915Z/artifacts.json) identificam a versão ensaiada.
- [Captura Serial](../firmware/principal/validation/20261014T045915Z/serial.log) e [verificação automática](../firmware/principal/validation/20261014T045915Z/boot-result.json): 30 segundos, um boot, `READY`, 14 mensagens `ALIVE`, SHA-256 completo igual ao ELF compilado e nenhuma falha de runtime observada. Heap livre de 315.720 B e margem mínima de pilha de 8.728 B durante a captura.
- [Regressões nativas](../firmware/principal/validation/20261014T045915Z/native-tests.log): **119/119** aprovadas. [Preservação](../firmware/principal/validation/20261014T045915Z/preservation.json): 59 arquivos dos módulos, incluindo testes, iguais ao levantamento anterior à implementação.
- [Comparação com Arduino CLI](../firmware/principal/validation/20261014T045915Z/core-comparison.json): sete arquivos-chave do core, SDK, flags e partições idênticos por SHA-256. A execução confirma Arduino-ESP32 3.3.12 / IDF 5.5.5. O compilador é GCC 14.2.0+20260121; o empacotamento PlatformIO usa esptool 5.4.0, enquanto a validação anterior usava 5.3.1.

A impressão inicial via `esp_app_get_elf_sha256` fornecia um prefixo; a versão final imprime os 32 bytes de `app_elf_sha256`. No Windows, `PYTHONIOENCODING=utf-8` evita erro ao registrar a barra de progresso do esptool. Tentativas intermediárias e inventário do ambiente permanecem como apoio local em `build/attempts/`.

Esta evidência cobre somente o boot da aplicação mínima anterior à interface de simulação. Os arquivos dessa rodada permanecem preservados. O coletor `verificar-inicializacao.py` corresponde exclusivamente àquela versão do protocolo.
