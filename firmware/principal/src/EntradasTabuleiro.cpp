#include "EntradasTabuleiro.h"
#include <Arduino.h>
#include <Wire.h>
#include "../../diagnostico/board_config.h"

// Pinagem prevista, definida em board_config.h:
// ESP32: SDA=21, SCL=22, RESET do MCP=14, CONFIRMAR=32.
// Colunas a..h no MCP: GPA0, GPA1, GPA2, GPA3, GPA4, GPA5, GPA7, GPB7.
// Linhas  1..8 no MCP: GPB0, GPB1, GPB2, GPB3, GPB4, GPB5, GPB6, GPA6.

namespace entradas {
namespace {

// O MCP possui duas portas de 8 pinos: A e B.
constexpr uint8_t DIRECAO_A = 0x00;
constexpr uint8_t DIRECAO_B = 0x01;
constexpr uint8_t LEITURA_A = 0x12;
constexpr uint8_t LEITURA_B = 0x13;
constexpr uint8_t SAIDA_A = 0x14;
constexpr uint8_t SAIDA_B = 0x15;

constexpr uint32_t ESTABILIDADE_CASA_MS = 30;
constexpr uint32_t ESTABILIDADE_BOTAO_MS = 30;
constexpr uint32_t PAUSA_MAXIMA_MS = 100;

bool matriz[8][8] = {};        // Ocupacao filtrada: true = ocupada.
bool ultimaLeitura[8][8] = {}; // Leitura que ainda pode estar oscilando.
uint32_t mudouEmMs[8][8] = {};

bool primeiraVarredura = true;
bool leituraDisponivel = false;
bool leituraEstavel = false;
bool matrizPronta = false;
bool i2cIniciado = false;
uint32_t ultimaVarreduraMs = 0;
const char* ultimoErro = "As entradas ainda nao foram inicializadas.";

bool botaoPreparado = false;
bool botaoLido = false;
bool botaoEstavel = false;
uint32_t botaoMudouEmMs = 0;

bool selecionarColuna(int coluna);
bool lerLinhas(bool linhasOcupadas[8]);
bool liberarColunas();
bool escreverRegistro(uint8_t endereco, uint8_t valor);
bool lerRegistro(uint8_t endereco, uint8_t& valor);
void registrarFalha(const char* mensagem);
uint64_t montarOcupacao();

} 

// VARREDURA: esta funcao deve ser chamada repetidamente.
ResultadoLeitura lerCasas(uint64_t& casasOcupadas) {
  if (!matrizPronta) {
    return ResultadoLeitura::Falha;
  }
  const uint32_t pausaMs = static_cast<uint32_t>(millis()) - ultimaVarreduraMs;
  if (!primeiraVarredura && pausaMs > PAUSA_MAXIMA_MS) {
    registrarFalha("Mais de 100 ms sem leitura. Reinicie as entradas.");
    return ResultadoLeitura::Falha;
  }

  leituraEstavel = true;
  for (int coluna = 0; coluna < 8; coluna++) {
    if (!selecionarColuna(coluna)) {
      registrarFalha("Falha ao selecionar a coluna.");
      return ResultadoLeitura::Falha;
    }
    delayMicroseconds(board::COLUMN_SETTLE_US);

    bool linhasOcupadas[8] = {};
    if (!lerLinhas(linhasOcupadas)) {
      registrarFalha("Falha ao ler os sensores.");
      return ResultadoLeitura::Falha;
    }

    for (int linha = 0; linha < 8; linha++) {
      bool ocupada = linhasOcupadas[linha];
      uint32_t agoraMs = millis();

      // A primeira varredura prepara a matriz; ainda nao entrega uma leitura.
      if (primeiraVarredura) {
        matriz[linha][coluna] = ocupada;
        ultimaLeitura[linha][coluna] = ocupada;
        mudouEmMs[linha][coluna] = agoraMs;
      } else if (ocupada != ultimaLeitura[linha][coluna]) {
        ultimaLeitura[linha][coluna] = ocupada;
        mudouEmMs[linha][coluna] = agoraMs;
      }

      // Aceita a mudanca quando ela permanece nas leituras por pelo menos 30 ms.
      if (agoraMs - mudouEmMs[linha][coluna] >= ESTABILIDADE_CASA_MS) {
        matriz[linha][coluna] = ocupada;
      } else {
        leituraEstavel = false;
      }
    }
  }

  if (!liberarColunas()) {
    registrarFalha("Falha ao liberar as colunas.");
    return ResultadoLeitura::Falha;
  }
  primeiraVarredura = false;
  ultimaVarreduraMs = millis();
  if (leituraEstavel) {
    leituraDisponivel = true;
  }
  if (!leituraDisponivel) {
    return ResultadoLeitura::AguardandoEstabilidade;
  }

  casasOcupadas = montarOcupacao();
  return ResultadoLeitura::LeituraDisponivel;
}

// PREPARACAO: chamar uma vez ao ligar, ou para tentar recuperar uma falha.
bool iniciarEntradas() {
  matrizPronta = false;
  primeiraVarredura = true;
  leituraDisponivel = false;
  leituraEstavel = false;

  pinMode(board::MCP_RESET_PIN, OUTPUT_OPEN_DRAIN);
  digitalWrite(board::MCP_RESET_PIN, LOW);            // Mantem o MCP em RESET.
  pinMode(board::BUTTON_CONFIRM, INPUT);              // O projeto usa resistor externo.
  botaoLido = digitalRead(board::BUTTON_CONFIRM) == LOW;
  botaoEstavel = botaoLido;
  botaoMudouEmMs = millis();
  botaoPreparado = true;

  if (!i2cIniciado) {
    i2cIniciado = Wire.begin(board::SDA_PIN, board::SCL_PIN, 100000);
  }
  if (!i2cIniciado) {
    registrarFalha("Nao foi possivel iniciar o I2C.");
    return false;
  }
  Wire.setTimeOut(25);

  // O RESET ja deixa as direcoes em entrada, as saidas em LOW,
  // os pull-ups e as interrupcoes desativados, e o modo BANK=0.
  delay(2);
  digitalWrite(board::MCP_RESET_PIN, HIGH); // Libera o RESET para o resistor externo.
  delay(2);

  uint8_t direcaoA = 0, direcaoB = 0, saidaA = 0, saidaB = 0;
  if (!lerRegistro(DIRECAO_A, direcaoA) || direcaoA != 0b11111111 ||
      !lerRegistro(DIRECAO_B, direcaoB) || direcaoB != 0b11111111 ||
      !lerRegistro(SAIDA_A, saidaA) || saidaA != 0 ||
      !lerRegistro(SAIDA_B, saidaB) || saidaB != 0) {
    registrarFalha("O MCP23017 nao confirmou a configuracao apos o RESET.");
    return false;
  }

  matrizPronta = true;
  ultimoErro = "";
  return true;
}

// BOTAO: chamar repetidamente, acompanhando tanto o aperto quanto a soltura.
bool confirmacaoFoiPressionada() {
  if (!botaoPreparado) {
    return false;
  }

  bool pressionado = digitalRead(board::BUTTON_CONFIRM) == LOW;
  uint32_t agoraMs = millis();
  if (pressionado != botaoLido) {
    botaoLido = pressionado;
    botaoMudouEmMs = agoraMs;
  }
  if (agoraMs - botaoMudouEmMs < ESTABILIDADE_BOTAO_MS) {
    return false;
  }
  if (botaoLido == botaoEstavel) {
    return false;
  }

  botaoEstavel = botaoLido;
  return botaoEstavel; // true apenas no novo aperto; soltar retorna false.
}

bool casasEstaoEstaveis() {
  return matrizPronta && leituraDisponivel && leituraEstavel;
}

const char* obterUltimoErro() {
  return ultimoErro;
}

namespace {

bool escreverRegistro(uint8_t endereco, uint8_t valor) {
  Wire.beginTransmission(board::MCP_ADDRESS);
  Wire.write(endereco);
  Wire.write(valor);
  return Wire.endTransmission() == 0;
}

bool lerRegistro(uint8_t endereco, uint8_t& valor) {
  Wire.beginTransmission(board::MCP_ADDRESS);
  Wire.write(endereco);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(board::MCP_ADDRESS, static_cast<uint8_t>(1)) != 1) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }
  valor = Wire.read();
  return true;
}

bool liberarColunas() {
  // Bit 1 = entrada, sem acionar o pino. Bit 0 = saida.
  return escreverRegistro(DIRECAO_A, 0b11111111) &&
         escreverRegistro(DIRECAO_B, 0b11111111);
}

bool selecionarColuna(int coluna) {
  if (!liberarColunas()) {
    return false;
  }

  // A configuracao numera GPA0..7 como 0..7 e GPB0..7 como 8..15.
  int pino = board::COL_BITS[coluna];
  uint8_t direcao = 0b11111111;
  if (pino < 8) {
    bitClear(direcao, pino);
    return escreverRegistro(DIRECAO_A, direcao);
  }
  bitClear(direcao, pino - 8);
  return escreverRegistro(DIRECAO_B, direcao);
}

bool lerLinhas(bool linhasOcupadas[8]) {
  uint8_t portaA = 0, portaB = 0;
  if (!lerRegistro(LEITURA_A, portaA) || !lerRegistro(LEITURA_B, portaB)) {
    return false;
  }

  for (int linha = 0; linha < 8; linha++) {
    int pino = board::ROW_BITS[linha];
    if (pino < 8) {
      linhasOcupadas[linha] = bitRead(portaA, pino) == LOW;
    } else {
      linhasOcupadas[linha] = bitRead(portaB, pino - 8) == LOW;
    }
  }
  return true;
}

void registrarFalha(const char* mensagem) {
  digitalWrite(board::MCP_RESET_PIN, LOW);
  matrizPronta = false;
  primeiraVarredura = true;
  leituraDisponivel = false;
  leituraEstavel = false;
  ultimoErro = mensagem;
}

// Converte a matriz para uint64_t
uint64_t montarOcupacao() {
  uint64_t ocupacao = 0;
  for (int linha = 0; linha < 8; linha++) {
    for (int coluna = 0; coluna < 8; coluna++) {
      if (matriz[linha][coluna]) {
        int casa = linha * 8 + coluna;
        ocupacao |= uint64_t{1} << casa;
      }
    }
  }
  return ocupacao;
}

} 
}  
