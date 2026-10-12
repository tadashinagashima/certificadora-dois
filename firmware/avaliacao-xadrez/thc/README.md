# Avaliação isolada: THC Chess Library

THC é a candidata recomendada na [avaliação técnica](../../../docs/avaliacao-bibliotecas-xadrez.md), que concentra cobertura, resultados e limitações. Esta pasta contém somente os ensaios; não integra a biblioteca ao firmware de diagnóstico, MoveTracker ou interpretação.

## Fonte

Repositório [billforsternz/thc-chess-library](https://github.com/billforsternz/thc-chess-library), commit [`aa217cb9b4d759cd7c07e11c98526b0187b0a64b`](https://github.com/billforsternz/thc-chess-library/commit/aa217cb9b4d759cd7c07e11c98526b0187b0a64b). A cópia mínima em `vendor/` preserva `src/thc.h`, `src/thc.cpp` e a [licença MIT](vendor/LICENSE), sem patches. O [manifesto](upstream.json) fixa os hashes SHA-256, conferidos pelo script antes de compilar.

## Reprodução

Na raiz do projeto:

```powershell
& .\firmware\avaliacao-xadrez\thc\validar.ps1
```

O script utiliza CMake/MSVC do Visual Studio 2022 e Arduino CLI já instalados. `-CMake` e `-ArduinoCli` permitem ajustar caminhos. Não instala bibliotecas globais nem baixa dependências. Para reproduzir o alvo avaliado, manter o core `esp32:esp32` 3.3.12 instalado.

Os testes nativos usam C++14, configuração Release, verificações ativas e `/W4 /WX` no ensaio. A biblioteca upstream é compilada separadamente, sem promover avisos a erros. O CTest executa 15 casos. Para rodar apenas no computador:

```powershell
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
& $cmake -S .\firmware\avaliacao-xadrez\thc -B .\firmware\avaliacao-xadrez\thc\build\native -G 'Visual Studio 17 2022' -A x64
& $cmake --build .\firmware\avaliacao-xadrez\thc\build\native --config Release
& $ctest --test-dir .\firmware\avaliacao-xadrez\thc\build\native -C Release --output-on-failure --no-tests=error
```

O script completo copia os mesmos fontes da biblioteca e de `essential_tests.cpp` para `build/esp32_probe/` e compila com `--fqbn esp32:esp32:esp32 --warnings all --jobs 2`. A opção adicional `-fstack-usage` gera estimativas por função no diretório de build; elas não medem o pico real de pilha. O sketch chama a suíte em `setup()` e preserva seu retorno em `evaluationFailures`, mas esta rodada realizou somente compilação/link, sem upload ou execução na placa.

## Evidências

- [Resultado estruturado](validation/resultado.json): versões, etapas, hashes e limites da execução.
- [Log CTest](validation/native-tests.log) e [JUnit](validation/native-tests.xml): 15/15 casos aprovados.
- [Log ESP32](validation/esp32-compile.log): link aprovado e avisos upstream preservados.

O script interrompe e registra falhas de testes ou compilação. Arquivos de build ficam em `build/`, coberto pelas exclusões locais. Consultar a avaliação técnica antes de interpretar os tamanhos de flash/RAM como consumo em execução ou de adotar a API no firmware.
