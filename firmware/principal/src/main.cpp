#include <Arduino.h>
#include <atomic>
#include "SerialGame.h"
#include "Hardware.h"

SET_LOOP_TASK_STACK_SIZE(32 * 1024);

namespace {
principal::SerialGame* game = nullptr;
principal::LineReceiver receiver;
principal::Hardware hardware;
std::uint32_t lastScan = 0, lastRetry = 0;
std::atomic<bool> uartError{false};
std::uint32_t lastByteMs = 0;

void persist() {
  if (!game->app().takeSaveRequest()) return;
  hardware.indicate({false, true, false});
  game->app().saved(principal::savePgn(hardware, game->app().record().pgn()));
  hardware.indicate(game->app().indicators(millis()));
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
  Serial.println("BOOT firmware=principal protocol=1 mode=physical");
  static principal::SerialGame instance(false);
  game = &instance;
  hardware.setup();
  Serial.print(game->command("status", millis()).c_str());
  Serial.println("READY protocol=1");
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
  game->app().tick(millis(), hardware.buttons());
  hardware.indicate(game->app().indicators(millis()));
  persist();
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
      else Serial.print(game->command(receiver.line(), millis()).c_str());
    }
    persist();
    hardware.indicate(game->app().indicators(millis()));
    Serial.println("DONE");
  }
  delay(1);
}
