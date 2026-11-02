#pragma once
#include <stdint.h>

// Leitura dos sensores e do botao. 
namespace entradas {

enum class ResultadoLeitura { AguardandoEstabilidade, LeituraDisponivel, Falha };

// Prepara as entradas. true = pronto; false = falha, explicada em obterUltimoErro().
// Chamar novamente tenta recuperar o MCP e descarta o estado anterior do filtro.
bool iniciarEntradas();

// Faz uma varredura. Chamar continuamente, inclusive durante a movimentacao.
// Trabalha com matriz[linha][coluna] e filtra cada casa por 30 ms.
// Mais de 100 ms sem chamar a leitura apos uma varredura completa gera Falha.
// So modifica casasOcupadas se retornar LeituraDisponivel:
// 64 bits, 1 = ocupada, a1 = bit 0, h1 = bit 7, a8 = bit 56, h8 = bit 63.
// Encaminhar cada leitura disponivel ao acompanhamento, antes da confirmacao.
// Falha exige avisar a interrupcao; não reutilizar a saida como nova leitura.
// Apos falha o MCP fica em RESET ate nova chamada a iniciarEntradas().
ResultadoLeitura lerCasas(uint64_t& casasOcupadas);

// Consulta a ultima varredura.
bool casasEstaoEstaveis();

// Um true por aperto estabilizado por 30 ms. Segurar nao repete.
// Chamar sempre para acompanhar tambem a soltura.
// Este modulo apenas detecta o aperto.
bool confirmacaoFoiPressionada();

// Texto vazio quando nao ha erro. 
const char* obterUltimoErro();

} 
