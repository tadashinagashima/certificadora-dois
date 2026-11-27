#include <Wire.h>
#include "../BoardHardware.h"

board::Hardware hardware;
bool haveSnapshot = false;
bool indicatorTest = false;
bool measurementColumnHeld = false;
uint32_t indicatorStart = 0;
uint32_t lastScan = 0;
uint64_t occupancy = 0;

void reportFault() {
  measurementColumnHeld = false;
  haveSnapshot = false;
  hardware.indicate({false, true, false});
  Serial.printf("ERRO matriz: %s. MCP mantido em RESET; enviar r para tentar novamente.\n",
                hardware.error());
}

void reportInitialisation() {
  measurementColumnHeld = false;
  haveSnapshot = false;
  if (!hardware.ready()) {
    reportFault();
    return;
  }
  hardware.indicate({});
  Serial.println("MCP23017 0x20 pronto. Colunas: uma LOW, demais high-Z; linhas ativas LOW.");
}

void printBoard() {
  if (!haveSnapshot) {
    Serial.println("Sem amostra valida da matriz.");
    return;
  }
  Serial.println("Vista superior: lado das brancas embaixo; X=ocupada, .=vazia");
  for (int rank = 7; rank >= 0; --rank) {
    Serial.printf("%d ", rank + 1);
    for (uint8_t file = 0; file < 8; ++file) {
      Serial.print((occupancy & (uint64_t{1} << board::squareIndex(file, rank))) ? "X " : ". ");
    }
    Serial.println();
  }
  Serial.println("  a b c d e f g h");
}

void reportSnapshot(uint64_t next) {
  if (!haveSnapshot) {
    occupancy = next;
    haveSnapshot = true;
    printBoard();
    return;
  }
  const uint64_t changed = occupancy ^ next;
  for (uint8_t index = 0; index < 64; ++index) {
    if (changed & (uint64_t{1} << index)) {
      Serial.printf("%lu ms %c%c %s\n", static_cast<unsigned long>(millis()),
                    board::squareFile(index), board::squareRank(index),
                    (next & (uint64_t{1} << index)) ? "ocupada" : "vazia");
    }
  }
  occupancy = next;
}

void toggleMeasurementColumn() {
  if (measurementColumnHeld) {
    hardware.initialiseMatrix();
    reportInitialisation();
    Serial.println("Medicao fixa encerrada; varredura retomada se MCP pronto.");
    return;
  }
  if (!hardware.ready()) {
    Serial.println("Medicao: MCP indisponivel; enviar r para recuperar.");
    return;
  }
  if (!hardware.selectColumn(0)) {
    reportFault();
    return;
  }
  measurementColumnHeld = true;
  haveSnapshot = false;
  Serial.println("MEDICAO: COL_a fixa LOW; demais colunas high-Z; varredura suspensa.");
  Serial.println("Medir ROW_1..8 com contatos da coluna a abertos/fechados. v ou r libera.");
}

void printHelp() {
  Serial.println("DIAGNOSTICO, NAO E FIRMWARE DE XADREZ. Serial 115200 baud.");
  Serial.println("m=matriz; r=reset MCP; v=COL_a fixa/liberar para medicao; ?=ajuda");
  Serial.println("o=consultar endereco OLED; t=LEDs+buzzer 120ms");
  Serial.println("Transicoes da matriz sao brutas, sem debounce e sem historico persistente.");
}

void setup() {
  Serial.begin(115200);
  hardware.setup();
  printHelp();
  reportInitialisation();
}

void loop() {
  if (indicatorTest && millis() - indicatorStart >= 120) {
    indicatorTest = false;
    hardware.indicate({false, !hardware.ready(), false});
  }
  // Keep the indicator pulse bounded: do not run blocking I2C operations during it.
  if (Serial.available() && !indicatorTest) {
    switch (Serial.read()) {
      case 'm': printBoard(); break;
      case 'r': hardware.initialiseMatrix(); reportInitialisation(); break;
      case 'v': toggleMeasurementColumn(); break;
      case 'o':
        Wire.beginTransmission(board::OLED_ADDRESS);
        Serial.printf("OLED 0x%02X: %s\n", board::OLED_ADDRESS,
                      Wire.endTransmission() == 0 ? "ACK" : "sem resposta");
        break;
      case 't':
        indicatorTest = true;
        indicatorStart = millis();
        hardware.indicate({true, true, true});
        break;
      case '?': printHelp(); break;
    }
  }
  if (hardware.ready() && !measurementColumnHeld && !indicatorTest && millis() - lastScan >= board::SCAN_INTERVAL_MS) {
    lastScan = millis();
    uint64_t next = 0;
    if (hardware.scan(next)) reportSnapshot(next);
    else reportFault();
  }
  delay(1);
}
