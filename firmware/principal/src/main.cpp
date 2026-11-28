#include <Arduino.h>
#include <atomic>
#include "SerialGame.h"
#include "../../BoardHardware.h"
#include "Display.h"
#include <esp_random.h>

SET_LOOP_TASK_STACK_SIZE(32 * 1024);

namespace {
principal::SerialGame* game = nullptr;
principal::LineReceiver receiver;
board::Hardware hardware;
std::uint32_t lastScan = 0, lastRetry = 0;
std::atomic<bool> uartError{false};
std::uint32_t lastByteMs = 0;

void metrics() {
  Serial.printf("METRICS heap_bytes=%u heap_min_bytes=%u stack_free_bytes=%u\n",
      unsigned(ESP.getFreeHeap()), unsigned(ESP.getMinFreeHeap()),
      unsigned(uxTaskGetStackHighWaterMark(nullptr)));
}
}

void setup() {
  Serial.setRxBufferSize(1024);
  Serial.begin(115200);
  Serial.onReceiveError([](hardwareSerial_error_t) { uartError.store(true); });
  enableLoopWDT();
  Serial.println("BOOT firmware=principal protocol=2 mode=physical");
  const auto bootId = (std::uint64_t{esp_random()} << 32) | esp_random();
  static principal::SerialGame instance(false, bootId);
  game = &instance;
  hardware.setup();
  if (hardware.busReady()) principal::display::begin();
  Serial.println("READY protocol=2");
}

void loop() {
  const auto now = millis();
  if (!hardware.ready() && now - lastRetry >= 1000) {
    lastRetry = now;
    hardware.initialiseMatrix();
  }
  if (now - lastScan >= board::SCAN_INTERVAL_MS) {
    lastScan = now;
    chessboard::Occupancy raw;
    if (hardware.scan(raw)) game->app().sample(raw, millis());
    else game->app().inputLost();
  }
  game->app().tick(millis());
  hardware.indicate(game->app().indicators(millis()));
  if (hardware.ready()) principal::display::update(game->app(), millis());
  if (receiver.pending() && millis() - lastByteMs > 2000) receiver.discard();
  for (unsigned budget = 0; budget < 64 && Serial.available(); ++budget) {
    if (uartError.exchange(false)) receiver.discard();
    lastByteMs = millis();
    const auto result = receiver.push(static_cast<char>(Serial.read()));
    if (result == principal::LineReceiver::Result::Pending) continue;
    if (result == principal::LineReceiver::Result::Invalid) {
      Serial.println("ERROR Line");
    } else {
      if (std::string(receiver.line()) == "metrics") metrics();
      else {
        Serial.print(game->command(receiver.line(), millis()).c_str());
        if (std::string(receiver.line()) == "status") {
          Serial.printf("OLED %s\n", principal::display::available() ? "online" : "offline");
          Serial.printf("OLED_TABULEIRO 0x%02X %s\n", board::BOARD_OLED_ADDRESS,
                        principal::display::boardAvailable() ? "online" : "offline");
          if (!principal::display::available() || !principal::display::boardAvailable())
            Serial.printf("I2C_OLED 0x3C=%u 0x3D=%u\n", principal::display::matchError(), principal::display::boardError());
        }
      }
    }
    hardware.indicate(game->app().indicators(millis()));
    Serial.println("DONE");
  }
  delay(1);
}
