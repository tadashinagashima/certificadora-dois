#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include "board_config.h"

constexpr uint8_t REG_IODIRA = 0x00;
constexpr uint8_t REG_IPOLA = 0x02;
constexpr uint8_t REG_GPINTENA = 0x04;
constexpr uint8_t REG_IOCON = 0x0A;
constexpr uint8_t REG_GPPUA = 0x0C;
constexpr uint8_t REG_GPIOA = 0x12;
constexpr uint8_t REG_OLATA = 0x14;
constexpr uint32_t SCAN_INTERVAL_MS = 25;

bool matrixReady = false;
bool wireReady = false;
bool haveSnapshot = false;
bool indicatorTest = false;
bool measurementColumnHeld = false;
uint32_t indicatorStart = 0;
uint32_t lastScan = 0;
uint64_t occupancy = 0;

// All functions use BANK=0, SEQOP=0 established by a hardware reset.
bool mcpWrite8(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(board::MCP_ADDRESS);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool mcpWrite16(uint8_t reg, uint16_t value) {
  Wire.beginTransmission(board::MCP_ADDRESS);
  Wire.write(reg);
  Wire.write(static_cast<uint8_t>(value));
  Wire.write(static_cast<uint8_t>(value >> 8));
  return Wire.endTransmission() == 0;
}

bool mcpRead16(uint8_t reg, uint16_t &value) {
  Wire.beginTransmission(board::MCP_ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(board::MCP_ADDRESS, static_cast<uint8_t>(2)) != 2) {
    while (Wire.available()) Wire.read();
    return false;
  }
  const uint8_t a = Wire.read();
  const uint8_t b = Wire.read();
  value = a | (static_cast<uint16_t>(b) << 8);
  return true;
}

void matrixFault(const char *reason) {
  // Independent of I2C: reset disables every output, even if an IODIR write failed.
  digitalWrite(board::MCP_RESET_PIN, LOW);
  matrixReady = false;
  measurementColumnHeld = false;
  haveSnapshot = false;
  digitalWrite(board::LED_RED, HIGH);
  Serial.printf("ERRO matriz: %s. MCP mantido em RESET; enviar r para tentar novamente.\n", reason);
}

bool initialiseMatrix() {
  measurementColumnHeld = false;
  if (!wireReady) {
    matrixFault("controlador I2C indisponivel");
    return false;
  }
  digitalWrite(board::MCP_RESET_PIN, LOW);
  delay(2);
  digitalWrite(board::MCP_RESET_PIN, HIGH);  // Open-drain: release to the external 10k pull-up.
  delay(2);

  // Keep all pins high-Z, preload LOW before enabling any column; external pull-ups.
  if (!mcpWrite16(REG_IODIRA, 0xFFFF) || !mcpWrite16(REG_OLATA, 0) ||
      !mcpWrite8(REG_IOCON, 0) || !mcpWrite16(REG_IPOLA, 0) ||
      !mcpWrite16(REG_GPINTENA, 0) || !mcpWrite16(REG_GPPUA, 0)) {
    matrixFault("configuracao I2C sem confirmacao");
    return false;
  }
  uint16_t directions = 0;
  uint16_t latches = 0;
  if (!mcpRead16(REG_IODIRA, directions) || directions != 0xFFFF ||
      !mcpRead16(REG_OLATA, latches) || latches != 0) {
    matrixFault("verificacao de IODIR/OLAT falhou");
    return false;
  }
  matrixReady = true;
  haveSnapshot = false;
  digitalWrite(board::LED_RED, LOW);
  Serial.println("MCP23017 0x20 pronto. Colunas: uma LOW, demais high-Z; linhas ativas LOW.");
  return true;
}

bool scanMatrix(uint64_t &snapshot) {
  snapshot = 0;
  for (uint8_t file = 0; file < 8; ++file) {
    // Break before make is required even when the next column uses another port.
    if (!mcpWrite16(REG_IODIRA, 0xFFFF) ||
        !mcpWrite16(REG_IODIRA, board::columnDirection(file))) {
      matrixFault("selecao da coluna falhou");
      return false;
    }
    delayMicroseconds(board::COLUMN_SETTLE_US);
    uint16_t levels = 0;
    if (!mcpRead16(REG_GPIOA, levels)) {
      matrixFault("leitura I2C falhou; amostra descartada");
      return false;
    }
    for (uint8_t rank = 0; rank < 8; ++rank) {
      if (!(levels & (uint16_t{1} << board::ROW_BITS[rank]))) {
        snapshot |= uint64_t{1} << board::squareIndex(file, rank);
      }
    }
  }
  if (!mcpWrite16(REG_IODIRA, 0xFFFF)) {
    matrixFault("liberacao das colunas falhou");
    return false;
  }
  return true;
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

void checkSD() {
  SD.end();
  if (!SD.begin(board::SD_CS, SPI, 1000000, "/sd", 1, false)) {
    Serial.println("SD: montagem falhou (cartao/contatos/alimentacao/FAT). Sem formatacao.");
    return;
  }
  Serial.printf("SD: tipo=%u capacidade=%llu MiB\n", static_cast<unsigned>(SD.cardType()),
                static_cast<unsigned long long>(SD.cardSize() / (1024 * 1024)));
  File root = SD.open("/", FILE_READ);
  if (!root || !root.isDirectory()) {
    Serial.println("SD: nao foi possivel abrir o diretorio raiz para leitura.");
  } else {
    Serial.println("SD: diretorio raiz aberto para leitura; nenhum arquivo modificado.");
  }
  root.close();
  SD.end();
}

void toggleMeasurementColumn() {
  if (measurementColumnHeld) {
    initialiseMatrix();
    Serial.println("Medicao fixa encerrada; varredura retomada se MCP pronto.");
    return;
  }
  if (!matrixReady) {
    Serial.println("Medicao: MCP indisponivel; enviar r para recuperar.");
    return;
  }
  if (!mcpWrite16(REG_IODIRA, 0xFFFF) ||
      !mcpWrite16(REG_IODIRA, board::columnDirection(0))) {
    matrixFault("selecao de COL_a para medicao falhou");
    return;
  }
  measurementColumnHeld = true;
  haveSnapshot = false;
  Serial.println("MEDICAO: COL_a fixa LOW; demais colunas high-Z; varredura suspensa.");
  Serial.println("Medir ROW_1..8 com contatos da coluna a abertos/fechados. v ou r libera.");
}

uint8_t sdTestByte(uint32_t offset) {
  return static_cast<uint8_t>(offset ^ (offset >> 8) ^ 0xA5);
}

void sampleDuringSDTest() {
  if (!matrixReady) return;
  uint64_t next = 0;
  if (scanMatrix(next)) reportSnapshot(next);
}

void testSDWrite() {
  if (measurementColumnHeld) {
    Serial.println("SD escrita: encerrar medicao fixa com v ou r antes do ensaio.");
    return;
  }
  SD.end();
  if (!SD.begin(board::SD_CS, SPI, 1000000, "/sd", 1, false)) {
    Serial.println("SD escrita: montagem falhou; sem formatacao.");
    return;
  }
  char path[16];
  bool freeName = false;
  for (unsigned index = 0; index < 1000; ++index) {
    snprintf(path, sizeof(path), "/DIAG%03u.TST", index);
    if (!SD.exists(path)) {
      freeName = true;
      break;
    }
  }
  if (!freeName) {
    Serial.println("SD escrita: nomes DIAG000..999 ocupados; nenhum arquivo alterado.");
    SD.end();
    return;
  }
  File file = SD.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("SD escrita: nao foi possivel criar arquivo novo.");
    SD.end();
    return;
  }
  Serial.printf("SD escrita: novo arquivo %s, 16384 bytes; preservar cartao conectado.\n", path);
  const bool matrixAtStart = matrixReady;
  constexpr uint32_t totalBytes = 16384;
  uint8_t buffer[512];
  bool ok = true;
  digitalWrite(board::LED_GREEN, HIGH);
  digitalWrite(board::LED_RED, HIGH);
  // Include buzzer load during the first block; SD calls can extend this pulse.
  digitalWrite(board::BUZZER, HIGH);
  for (uint32_t offset = 0; offset < totalBytes && ok; offset += sizeof(buffer)) {
    for (size_t i = 0; i < sizeof(buffer); ++i) buffer[i] = sdTestByte(offset + i);
    ok = file.write(buffer, sizeof(buffer)) == sizeof(buffer);
    file.flush();
    digitalWrite(board::BUZZER, LOW);
    sampleDuringSDTest();
  }
  file.close();
  if (ok) {
    file = SD.open(path, FILE_READ);
    ok = file && file.size() == totalBytes;
    for (uint32_t offset = 0; offset < totalBytes && ok; offset += sizeof(buffer)) {
      ok = file.read(buffer, sizeof(buffer)) == static_cast<int>(sizeof(buffer));
      for (size_t i = 0; i < sizeof(buffer) && ok; ++i) ok = buffer[i] == sdTestByte(offset + i);
      sampleDuringSDTest();
    }
    file.close();
  }
  digitalWrite(board::BUZZER, LOW);
  digitalWrite(board::LED_GREEN, LOW);
  digitalWrite(board::LED_RED, matrixReady ? LOW : HIGH);
  SD.end();
  Serial.printf("SD %s: %s preservado; gravacao/flush/releitura %s.\n",
                ok ? "OK" : "FALHOU", path, ok ? "conferidos" : "incompletos ou divergentes");
  Serial.printf("Matriz durante SD: %s. Medir 3V3 e aquecimento; este teste nao mede tensao.\n",
                matrixAtStart && matrixReady ? "varreduras intercaladas sem erro I2C" : "indisponivel ou com erro; ensaio integrado incompleto");
}

void pollButtons() {
  static const uint8_t pins[3] = {board::BUTTON_CONFIRM, board::BUTTON_PROMOTE, board::BUTTON_END};
  static const char *names[3] = {"confirmar", "promover", "encerrar"};
  static bool observed[3] = {false, false, false};
  static bool stable[3] = {false, false, false};
  static uint32_t changedAt[3] = {0, 0, 0};
  for (uint8_t i = 0; i < 3; ++i) {
    const bool pressed = digitalRead(pins[i]) == LOW;
    if (pressed != observed[i]) {
      observed[i] = pressed;
      changedAt[i] = millis();
    }
    if (observed[i] != stable[i] && millis() - changedAt[i] >= 30) {
      stable[i] = observed[i];
      Serial.printf("Botao %s: %s (diagnostico, sem efeito em partida).\n", names[i],
                    stable[i] ? "pressionado" : "solto");
    }
  }
}

void printHelp() {
  Serial.println("DIAGNOSTICO, NAO E FIRMWARE DE XADREZ. Serial 115200 baud.");
  Serial.println("m=matriz; r=reset MCP; v=COL_a fixa/liberar para medicao; ?=ajuda");
  Serial.println("s=ler SD; w=novo arquivo SD 16KiB + releitura/cargas; t=LEDs+buzzer 120ms");
  Serial.println("Transicoes da matriz sao brutas, sem debounce e sem historico persistente.");
}

void setup() {
  Serial.begin(115200);
  // Assert MCP reset before any configuration or peripheral access.
  digitalWrite(board::MCP_RESET_PIN, LOW);
  pinMode(board::MCP_RESET_PIN, OUTPUT_OPEN_DRAIN);
  for (uint8_t pin : {board::LED_GREEN, board::LED_RED, board::BUZZER}) {
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
  }
  for (uint8_t pin : {board::BUTTON_CONFIRM, board::BUTTON_PROMOTE, board::BUTTON_END}) {
    pinMode(pin, INPUT);  // External 10k pull-ups, including input-only GPIO34.
  }
  digitalWrite(board::SD_CS, HIGH);
  pinMode(board::SD_CS, OUTPUT);
  SPI.begin(board::SD_SCK, board::SD_MISO, board::SD_MOSI, board::SD_CS);
  wireReady = Wire.begin(board::SDA_PIN, board::SCL_PIN, 100000);
  Wire.setTimeOut(25);
  printHelp();
  initialiseMatrix();
  Serial.println("Ensaios: s=SD leitura; w=SD escrita; v=medicao de linha; t=indicadores.");
}

void loop() {
  if (indicatorTest && millis() - indicatorStart >= 120) {
    indicatorTest = false;
    digitalWrite(board::LED_GREEN, LOW);
    digitalWrite(board::LED_RED, matrixReady ? LOW : HIGH);
    digitalWrite(board::BUZZER, LOW);
  }
  // Keep the indicator pulse bounded: do not run blocking SD/MCP operations during it.
  if (Serial.available() && !indicatorTest) {
    switch (Serial.read()) {
      case 'm': printBoard(); break;
      case 'r': initialiseMatrix(); break;
      case 'v': toggleMeasurementColumn(); break;
      case 's': checkSD(); break;
      case 'w': testSDWrite(); break;
      case 't':
        indicatorTest = true;
        indicatorStart = millis();
        digitalWrite(board::LED_GREEN, HIGH);
        digitalWrite(board::LED_RED, HIGH);
        digitalWrite(board::BUZZER, HIGH);
        break;
      case '?': printHelp(); break;
    }
  }
  pollButtons();
  if (matrixReady && !measurementColumnHeld && !indicatorTest && millis() - lastScan >= SCAN_INTERVAL_MS) {
    lastScan = millis();
    uint64_t next = 0;
    if (scanMatrix(next)) reportSnapshot(next);
  }
  delay(1);
}
