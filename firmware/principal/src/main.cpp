#include <Arduino.h>
#include <atomic>
#include <algorithm>
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
std::uint32_t lastByteMs = 0, confirmAt = 0;
bool confirming = false;
std::string reply;
std::size_t sent = 0;

void acquire() {
  lastScan = millis();
  chessboard::Occupancy raw;
  if (hardware.scan(raw)) game->app().sample(raw, millis());
  else game->app().inputLost();
}

void respond(const std::string& line) {
  char info[160];
  if (line == "metrics") {
    std::snprintf(info, sizeof(info), "METRICS heap_bytes=%u heap_min_bytes=%u stack_free_bytes=%u\n",
        unsigned(ESP.getFreeHeap()), unsigned(ESP.getMinFreeHeap()),
        unsigned(uxTaskGetStackHighWaterMark(nullptr)));
    reply = info;
  } else if (line == "oled 100" || line == "oled 400") {
    const bool ready = principal::display::setFrequency(line == "oled 100" ? 100000 : 400000);
    reply = ready ? "OK oled\n" : "ERROR OledBus\n";
  } else {
    reply = game->command(line, millis());
    if (line == "status") {
      using namespace principal::display;
      std::snprintf(info, sizeof(info), "OLED_HZ %u\nOLED 0x%02X %s\nOLED_TABULEIRO 0x%02X %s\n",
                    unsigned(busFrequency()), board::OLED_RIGHT_ADDRESS, rightOnline() ? "online" : "offline",
                    board::OLED_LEFT_ADDRESS, leftOnline() ? "online" : "offline");
      reply += info;
      if (!rightOnline() || !leftOnline()) {
        std::snprintf(info, sizeof(info), "I2C_OLED 0x3C=%u 0x3D=%u\n", rightError(), leftError());
        reply += info;
      }
    }
  }
  reply += "DONE\n";
}

void serviceConsole(bool scanned) {
  if (confirming) {
    // One Enter owns one reply. Allow the normal scan/debounce to finish
    // while continuing to service the board, screens and watchdog.
    if (scanned && (game->app().settled(millis()) || !hardware.ready() || millis() - confirmAt >= 200)) {
      confirming = false;
      respond("confirm");
    }
  } else if (reply.empty()) {
    if (receiver.pending() && millis() - lastByteMs > 2000) receiver.discard();
    for (unsigned budget = 0; budget < 64 && Serial.available(); ++budget) {
      if (uartError.exchange(false)) receiver.discard();
      lastByteMs = millis();
      const auto result = receiver.push(static_cast<char>(Serial.read()));
      if (result == principal::LineReceiver::Result::Pending) continue;
      if (result == principal::LineReceiver::Result::Invalid) reply = "ERROR Line\nDONE\n";
      else if (std::string(receiver.line()) == "confirm") {
        confirming = true;
        confirmAt = millis();
      } else respond(receiver.line());
      break;
    }
  }

  if (reply.empty()) return;
  const int room = Serial.availableForWrite();
  if (room <= 0) return;
  const auto count = std::min(reply.size() - sent, static_cast<std::size_t>(room));
  sent += Serial.write(reinterpret_cast<const uint8_t*>(reply.data() + sent), count);
  if (sent == reply.size()) { reply.clear(); sent = 0; }
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
  const bool scanned = now - lastScan >= board::SCAN_INTERVAL_MS;
  if (scanned) acquire();
  game->app().tick(millis());
  serviceConsole(scanned);
  hardware.indicate(game->app().indicators(millis()));
  principal::display::update(game->app(), millis());
  delay(1);
}
