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

void acquire() {
  lastScan = millis();
  chessboard::Occupancy raw;
  if (hardware.scan(raw)) game->app().sample(raw, millis());
  else game->app().inputLost();
}

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
  Serial.println("BOOT firmware=principal protocol=3 mode=physical");
  const auto bootId = (std::uint64_t{esp_random()} << 32) | esp_random();
  static principal::SerialGame instance(false, bootId);
  game = &instance;
  hardware.setup();
  principal::display::begin();
  Serial.println("READY protocol=3");
}

void loop() {
  const auto now = millis();
  if (!hardware.ready() && now - lastRetry >= 1000) {
    lastRetry = now;
    hardware.initialiseMatrix();
  }
  if (now - lastScan >= board::SCAN_INTERVAL_MS) acquire();
  game->app().tick(millis());
  hardware.indicate(game->app().indicators(millis()));
  principal::display::update(game->app(), millis());
  if (receiver.pending() && millis() - lastByteMs > 2000) receiver.discard();
  for (unsigned budget = 0; budget < 64 && Serial.available(); ++budget) {
    if (uartError.exchange(false)) receiver.discard();
    lastByteMs = millis();
    const auto result = receiver.push(static_cast<char>(Serial.read()));
    if (result == principal::LineReceiver::Result::Pending) continue;
    if (result == principal::LineReceiver::Result::Invalid) {
      Serial.println("ERROR Line");
    } else {
      const std::string line = receiver.line();
      if (line == "metrics") metrics();
      else if (line == "oled 100" || line == "oled 400") {
        const bool ready = principal::display::setFrequency(line == "oled 100" ? 100000 : 400000);
        Serial.println(ready ? "OK oled" : "ERROR OledBus");
      }
      else {
        if (line == "confirm") acquire();
        Serial.print(game->command(line, millis()).c_str());
        if (line == "status") {
          Serial.printf("OLED_HZ %u\n", unsigned(principal::display::busFrequency()));
          Serial.printf("OLED 0x%02X %s\n", board::OLED_RIGHT_ADDRESS, principal::display::rightOnline() ? "online" : "offline");
          Serial.printf("OLED_TABULEIRO 0x%02X %s\n", board::OLED_LEFT_ADDRESS,
                        principal::display::leftOnline() ? "online" : "offline");
          if (!principal::display::rightOnline() || !principal::display::leftOnline())
            Serial.printf("I2C_OLED 0x3C=%u 0x3D=%u\n", principal::display::rightError(), principal::display::leftError());
        }
      }
    }
    hardware.indicate(game->app().indicators(millis()));
    Serial.println("DONE");
  }
  delay(1);
}
