# Mecânica

O [modelo FreeCAD](../hardware/mecanica/tabuleiro_sensorial.FCStd) é a fonte editável da proposta de montagem. FreeCAD foi escolhido pela API Python e pelos recursos paramétricos nativos; a revisão A foi verificada na versão 1.1.4. A fabricação depende da conferência dos componentes e dos [testes físicos](plano-testes-fisicos.md).

## 1. Arquivos

| Arquivo | Finalidade |
| --- | --- |
| [FCStd](../hardware/mecanica/tabuleiro_sensorial.FCStd) | Planilha de parâmetros, expressões e operações geométricas editáveis |
| [STEP](../hardware/mecanica/exportacoes/tabuleiro_sensorial.step) | Intercâmbio de sólidos, incluindo reservas de componentes |
| [Prancha PDF](../hardware/mecanica/exportacoes/prancha_dimensional.pdf) e [SVG](../hardware/mecanica/exportacoes/prancha_dimensional.svg) | Planta, corte, frente e cotas; sem escala de impressão garantida |
| [Montagem](../hardware/mecanica/exportacoes/montagem.png), [vista explodida](../hardware/mecanica/exportacoes/vista_explodida.png) e [matriz inferior](../hardware/mecanica/exportacoes/matriz_inferior.png) | Apresentação derivada da geometria; deslocamentos da vista explodida são apenas visuais |
| [Lista de corte](../hardware/mecanica/exportacoes/lista_corte.csv) | Quantidades e dimensões nominais, sujeitas à aprovação para fabricação |
| [Coordenadas dos sensores](../hardware/mecanica/exportacoes/coordenadas_sensores.csv) | Casas a1–h8, índices e centros em milímetros |
| [Verificação geométrica](../hardware/mecanica/exportacoes/verificacao_geometrica.json) e [reabertura FCStd/STEP](../validacao/revisao-cad.json) | Resultados das verificações por software |

O STEP não preserva a planilha nem as expressões do FreeCAD. Sua importação no SOLIDWORKS ainda não foi testada. As casas coloridas são elementos de apresentação, excluídos do STEP e das verificações físicas.

## 2. Dimensões e parâmetros

Os parâmetros estão na planilha `Parametros`, coluna B. A tabela usa os aliases das expressões; valores propostos não constituem especificações aprovadas para fabricação.

| Parâmetro | Valor inicial | Situação |
| --- | --- | --- |
| `Outer` | 350 mm | Exterior quadrado aproximado previsto no projeto |
| `Pitch` / `Game` / `Margin` | 37,5 / 300 / 25 mm | Área de jogo prevista; `Game = 8 × Pitch`, margem centrada |
| `GlassNominal` / `Glass` | 350 / 3 mm | Espessura definida; medir o lado do vidro existente |
| `Height` / `FootHeight` | 60 / 3 mm | Proposta: 63 mm com pés, sem peças de xadrez |
| `Base` / `Wall` / `Carrier` | 6 / 9 / 6 mm | MDF do fundo, paredes e suporte; confirmar material |
| `Gasket` / `GasketWidth` | 1 / 10 mm | Junta proposta; espessura instalada/comprimida |
| `ReedDiameter` / `ReedLength` | 2 / 14 mm | Envelope compatível com medidas aproximadas; conferir tolerâncias e terminais |
| `ReedHole` | 2,8 mm | Furo proposto; validar fixação |
| `SensorGap` | 3 mm | Folga proposta entre topo do corpo do reed e face inferior do vidro |
| `LeadAbove` / `LeadBelow` | 1,5 / 5 mm | Terminais retos ilustrativos |
| `ChannelWidth` / `ChannelDepth` | 3 / 2 mm | Canais propostos para fios isolados |
| `MagnetDiameter` / `MagnetHeight` | 12 / 3 mm | Referência do planejamento; alternativa Ø10 mm em avaliação, com espessura a medir |
| `Support` | 12 mm | Seção quadrada proposta dos apoios de canto |
| `BaseScrew` / `Pilot` | 3,4 / 2,5 mm | Furos de passagem e pilotos; ferragens a selecionar |
| `FootDiameter` | 14 mm | Pés adesivos propostos |
| `ButtonDiameter` / `LedDiameter` | 12 / 5 mm | Furos de painel provisórios |

O plano inferior do fundo é Z = 0 e os pés chegam a Z = −3. O suporte ocupa Z = 50 a 56; a junta, Z = 56 a 57; o vidro, Z = 57 a 60. O corpo do reed ocupa Z = 40 a 54, com centro em Z = 47: **10 mm abaixo da face inferior do vidro**. `SensorGap` mede a distância ao topo do corpo, não ao centro nem ao contato magnético interno.

O ímã de referência ocupa Z = 60,2 a 63,2: sua face inferior e seu centro estão, respectivamente, **13,2 e 14,7 mm acima do centro do reed**. A folga de 0,2 mm sobre o vidro é ilustrativa; embutimento e feltro alteram a distância real. Essas cotas não representam alcance magnético validado.

A frente, lado das brancas, corresponde ao menor Y. Para índices de coluna e linha entre 0 e 7, os centros seguem `X = Margin + (coluna + 0,5) × Pitch` e `Y = Margin + (linha + 0,5) × Pitch`. Na revisão A, a1 = (43,75; 43,75) e h8 = (306,25; 306,25), em acordo com a eletrônica e o diagnóstico. O passo entre sensores é **37,5 mm**; na fronteira entre duas casas, o afastamento lateral a cada eixo é **18,75 mm**. A seletividade deve ser verificada no [piloto com vidro](plano-testes-fisicos.md).

## 3. Estrutura e interfaces propostas

O fundo removível e o suporte superior medem 350 × 350 × 6 mm, fixados a quatro apoios de canto. Cada placa tem quatro furos Ø3,4 mm, com escareado de 90°, Ø6,8 mm e profundidade de 1,7 mm. Os apoios possuem pilotos de 10 mm nas faces superior e inferior. Parafusos e roscas não foram modelados; as cabeças devem ficar embutidas.

As paredes frontal e traseira medem 350 × 44 × 9 mm; as laterais, 332 × 44 × 9 mm, com juntas de topo. Uma travessa de 332 × 12 × 9 mm apoia o suporte entre as colunas d/e. A fixação das paredes e da travessa depende do MDF disponível.

O suporte contém 64 furos para reeds verticais, oito canais por direção, dois coletores e passagem de chicote de 8 × 28 mm à direita. Diodos e cola quente inferior são envelopes; fios, soldas, cruzamentos e terminais dobrados não estão modelados. Redes distintas devem permanecer isoladas e abaixo do apoio do vidro.

O vidro removível repousa sobre junta periférica de 10 mm de largura, com contorno externo de 346 × 346 mm. Duas pegas laterais R10 permitem acesso à borda. Apoio, bordas lapidadas, estabilidade e retenção no transporte precisam de validação.

A frente recebe três botões e dois LEDs. A abertura lateral de 70 × 20 mm reserva acesso ao USB e ao microSD; o acesso real depende da posição dos módulos, cabos e cartão. O OLED opcional ainda não tem recorte ou suporte.

Os sólidos `REF` são reservas, não modelos de fabricante:

| Reserva | Envelope nominal |
| --- | --- |
| ESP32 / USB | 58 × 30 × 15 mm |
| Adaptador SD passivo / microSD | 32 × 24 × 2,1 mm |
| Placa MCP23017 e interfaces | 85 × 65 × 18 mm |
| Buzzer | 16 × 16 × 12 mm |

A placa de controle termina em Z = 28 e os terminais inferiores dos reeds em Z = 35, deixando **7 mm** sem considerar fios, soldas, conectores e fixações. Não há PCB ou protoboard detalhada. A reserva acomoda a montagem do MCP e das interfaces, conforme a [eletrônica](eletronica.md). Conferir dimensões, USB e furação da DevKit V1 de 30 pinos antes de definir seus suportes. A revisão elétrica B não altera a geometria desta proposta mecânica.

## 4. Edição e reprodução

Para editar o `.FCStd`, alterar a coluna B da planilha `Parametros` e recomputar, preservando aliases e fórmulas. O documento usa operações nativas de `Part`, sem complemento obrigatório.

**Editar o documento não atualiza as exportações.** O gerador recria o modelo e pode substituir alterações manuais. Para reproduzir uma revisão, ajustar `PARAMS` e as cotas complementares em [`gerar_modelo.py`](../hardware/mecanica/gerar_modelo.py). Rever também cotas fixas de acessos/reservas, rótulos das imagens, anotações da prancha e valores esperados pelos validadores.

Dependências:

- Python do FreeCAD, com `FreeCAD`, `Part`, `Import`, `MeshPart`, `FreeCADGui` e `PySide` para geração e apresentação nativa.
- NumPy e Pillow para as imagens por CPU; o renderizador usa as fontes Segoe UI nos caminhos definidos em `renderizar.py`.
- Python com ReportLab para a prancha; esse script não importa FreeCAD.

Executar na raiz do repositório, ajustando o caminho do Python do FreeCAD:

```powershell
$freecadPython = 'C:\Program Files\FreeCAD 1.1\bin\python.exe'
& $freecadPython .\hardware\mecanica\gerar_modelo.py
if ($LASTEXITCODE -ne 0) { throw 'Falha na geração ou verificação geométrica' }
& $freecadPython .\hardware\mecanica\finalizar_visual.py
if ($LASTEXITCODE -ne 0) { throw 'Falha ao salvar a apresentação nativa' }
& $freecadPython .\hardware\mecanica\renderizar.py
if ($LASTEXITCODE -ne 0) { throw 'Falha no render' }
python .\hardware\mecanica\gerar_prancha.py
if ($LASTEXITCODE -ne 0) { throw 'Falha ao gerar a prancha' }
& $freecadPython .\scripts\revisar_cad.py
if ($LASTEXITCODE -ne 0) { throw 'Falha na reabertura FCStd/STEP' }
python .\scripts\validar_integracao.py
if ($LASTEXITCODE -ne 0) { throw 'Falha na validação integrada' }
```

`finalizar_visual.py` inicializa a GUI, oculta a janela e salva cores, transparências e câmera. `renderizar.py` lê a malha intermediária em `hardware/mecanica/build/`, ignorada pelo Git. Para verificar os arquivos salvos sem regenerá-los, executar somente os dois validadores finais; `revisar_cad.py` reabre FCStd e STEP sem salvá-los.

## 5. Validação e limites

Os relatórios geométricos conferem validade dos sólidos, erros do documento, interseções volumétricas e equivalência de sólidos/volumes entre FCStd e STEP. Contatos de volume zero são permitidos; elementos gráficos ficam fora da verificação. As contagens e resultados estão nos relatórios vinculados acima.

O teste paramétrico alterou `Outer` de 350 para 360 mm, verificou o aumento de 10 mm no fundo e o deslocamento de 5 mm do reed h8, restaurando depois o valor inicial. Esse resultado valida as expressões verificadas, não todas as combinações de parâmetros.

As verificações não abrangem campo magnético, rigidez, empenamento, tolerâncias, durabilidade da cola ou espaço real para fiação. Altura dos sensores, furos, fixações, dimensões dos componentes e escolha dos ímãs permanecem sujeitos ao [plano de testes físicos](plano-testes-fisicos.md). Após alterar o modelo, repetir as verificações e revisar as exportações antes de fabricar.
