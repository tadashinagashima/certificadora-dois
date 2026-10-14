#include <Arduino.h>
#include <esp_app_desc.h>
#include <esp_arduino_version.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <atomic>
#include "SerialGame.h"

static_assert(ESP_ARDUINO_VERSION == ESP_ARDUINO_VERSION_VAL(3, 3, 12),
              "Firmware validado com Arduino-ESP32 3.3.12");
SET_LOOP_TASK_STACK_SIZE(32 * 1024);

namespace {
principal::SerialGame* game = nullptr;
principal::LineReceiver receiver;
std::atomic<bool> uartError{false};
std::uint32_t lastByteMs = 0;
std::uint32_t commands = 0, inputErrors = 0, maxCommandUs = 0;
bool timedOut = false;

void metrics() {
  constexpr auto caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  Serial.printf("METRICS uptime_ms=%lu commands=%lu input_errors=%lu max_command_us=%lu heap_bytes=%u heap_min_bytes=%u largest_block_bytes=%u stack_free_bytes=%u stack_bytes=%u heap_ok=%u watchdog=%u\n",
      static_cast<unsigned long>(millis()), static_cast<unsigned long>(commands),
      static_cast<unsigned long>(inputErrors), static_cast<unsigned long>(maxCommandUs),
      unsigned(heap_caps_get_free_size(caps)), unsigned(heap_caps_get_minimum_free_size(caps)),
      unsigned(heap_caps_get_largest_free_block(caps)), unsigned(uxTaskGetStackHighWaterMark(nullptr)),
      unsigned(getArduinoLoopTaskStackSize()), heap_caps_check_integrity_all(true) ? 1U : 0U,
      esp_task_wdt_status(nullptr) == ESP_OK ? 1U : 0U);
}
}

void setup() {
  Serial.setRxBufferSize(1024);
  Serial.begin(115200);
  Serial.onReceiveError([](hardwareSerial_error_t) { uartError.store(true); });
  enableLoopWDT();
  Serial.println("BOOT firmware=principal protocol=1 mode=simulation");
  Serial.printf("PLATFORM arduino=%s idf=%s\n", ESP_ARDUINO_VERSION_STR, esp_get_idf_version());
  Serial.print("ELF_SHA256 ");
  for (const auto byte : esp_app_get_description()->app_elf_sha256)
    Serial.printf("%02x", static_cast<unsigned>(byte));
  Serial.println();
  Serial.printf("BOARD chip=%s revision=%u cpu_mhz=%u flash_bytes=%u psram_bytes=%u reset=%d\n",
      ESP.getChipModel(), ESP.getChipRevision(), ESP.getCpuFreqMHz(), ESP.getFlashChipSize(),
      ESP.getPsramSize(), static_cast<int>(esp_reset_reason()));
  static principal::SerialGame instance;
  game = &instance;
  Serial.print(game->command("status", millis()).c_str());
  metrics();
  Serial.println("READY protocol=1");
}

void loop() {
  if (receiver.pending() && !timedOut && millis() - lastByteMs > 2000) {
    receiver.discard();
    game->inputLost();
    timedOut = true;
    ++inputErrors;
  }
  for (unsigned budget = 0; budget < 64 && Serial.available(); ++budget) {
    if (uartError.exchange(false)) {
      receiver.discard();
      game->inputLost();
      ++inputErrors;
    }
    lastByteMs = millis();
    const auto result = receiver.push(static_cast<char>(Serial.read()));
    if (result == principal::LineReceiver::Result::Pending) continue;
    timedOut = false;
    if (result == principal::LineReceiver::Result::Invalid) {
      game->inputLost();
      ++inputErrors;
      Serial.println("ERROR Line");
    } else {
      const auto start = micros();
      if (std::string(receiver.line()) == "metrics") metrics();
      else Serial.print(game->command(receiver.line(), millis()).c_str());
      const auto duration = micros() - start;
      if (duration > maxCommandUs) maxCommandUs = duration;
      ++commands;
    }
    Serial.println("DONE");
  }
  delay(1);
}
