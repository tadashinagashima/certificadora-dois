#include "Display.h"
#include "ArcadeDisplay.h"
#include <Wire.h>
#include <cstring>

namespace principal {
namespace display {
namespace {
using arcade::Frame;
constexpr unsigned frameBytes = 1024;
constexpr uint32_t probeMs = 3000;

struct Panel {
  u8x8_t device{};
  uint8_t address;
  Frame target{}, sent{};
  unsigned offset = frameBytes;
  bool online = false, repaint = true;
  uint8_t error = 0;
  uint32_t probed = 0;
};
Panel left{{}, board::OLED_LEFT_ADDRESS};
Panel right{{}, board::OLED_RIGHT_ADDRESS};
uint32_t frequency = board::OLED_HZ, busAttempt = 0, frameAt = 0;
bool busReady = false;

uint8_t transfer(u8x8_t* device, uint8_t msg, uint8_t size, void* data) {
  Panel& panel = device == &left.device ? left : right;
  if (!panel.online) return 0;
  switch (msg) {
    case U8X8_MSG_BYTE_START_TRANSFER:
      Wire1.beginTransmission(panel.address);
      break;
    case U8X8_MSG_BYTE_SEND:
      if (Wire1.write(static_cast<uint8_t*>(data), size) != size) {
        Wire1.endTransmission();
        panel.error = 1;
        panel.online = false;
      }
      break;
    case U8X8_MSG_BYTE_END_TRANSFER:
      panel.error = Wire1.endTransmission();
      panel.online = panel.error == 0;
      break;
  }
  return panel.online;
}

// Four-wire modules have no reset pin. The scheduler allows power to settle
// before initialization; retrying a panel must not sleep inside the scan loop.
uint8_t noResetPin(u8x8_t*, uint8_t, uint8_t, void*) { return 1; }

void probe(Panel& panel, uint32_t now) {
  panel.probed = now;
  Wire1.beginTransmission(panel.address);
  panel.error = Wire1.endTransmission();
  if (panel.error) { panel.online = false; return; }
  if (panel.online) return;
  panel.online = true;
  u8x8_Setup(&panel.device, u8x8_d_ssd1315_128x64_noname, u8x8_cad_ssd13xx_fast_i2c,
             transfer, noResetPin);
  u8x8_SetPin_HW_I2C(&panel.device, U8X8_PIN_NONE, U8X8_PIN_NONE, U8X8_PIN_NONE);
  u8x8_SetI2CAddress(&panel.device, panel.address << 1);
  u8x8_InitDisplay(&panel.device); // SSD1315 stays dark until its first complete frame.
  panel.repaint = true;
  panel.offset = frameBytes;
}

bool pending(const Panel& panel) { return panel.online && panel.offset < frameBytes; }

void flushChunk(Panel& panel) {
  while (pending(panel)) {
    const unsigned chunkBytes = frequency == 100000 ? 32 : 64;
    const unsigned offset = panel.offset;
    panel.offset += chunkBytes;
    auto* src = &panel.target.pages[offset / 128][offset % 128];
    auto* dst = &panel.sent.pages[offset / 128][offset % 128];
    unsigned first = 0, end = chunkBytes;
    if (!panel.repaint) {
      while (first < end && std::memcmp(src + first, dst + first, 8) == 0) first += 8;
      while (end > first && std::memcmp(src + end - 8, dst + end - 8, 8) == 0) end -= 8;
    }
    if (first == end) continue;
    u8x8_DrawTile(&panel.device, (offset % 128 + first) / 8, offset / 128,
                  (end - first) / 8, src + first);
    if (!panel.online) return;
    std::memcpy(dst + first, src + first, end - first);
    if (panel.offset == frameBytes && panel.repaint) {
      u8x8_SetPowerSave(&panel.device, 0);
      panel.repaint = false;
    }
    return;
  }
}

bool openBus() {
  busAttempt = millis();
  busReady = Wire1.begin(board::OLED_SDA_PIN, board::OLED_SCL_PIN, frequency);
  if (busReady) Wire1.setTimeOut(12);
  for (Panel* p : {&left, &right}) {
    p->online = false;
    p->error = busReady ? 0 : 4;
    p->probed = busAttempt - probeMs + 300;
  }
  return busReady;
}
} // namespace

void begin() { openBus(); }

bool setFrequency(uint32_t hz) {
  if (hz != 100000 && hz != 400000) return false;
  if (busReady) Wire1.end();
  frequency = hz;
  return openBus();
}

uint32_t busFrequency() { return frequency; }
bool rightOnline() { return right.online; }
bool leftOnline() { return left.online; }
uint8_t rightError() { return right.error; }
uint8_t leftError() { return left.error; }

void update(const BoardApp& app, uint32_t now) {
  if (!busReady) {
    if (now - busAttempt >= probeMs) openBus();
    return;
  }
  for (Panel* p : {&left, &right}) {
    if (now - p->probed >= probeMs) { probe(*p, now); return; }
  }
  if (!left.online && !right.online) return;
  if (!pending(left) && !pending(right)) {
    if (now - frameAt < 40) return;
    arcade::render(left.target, right.target, app, now);
    frameAt = now;
    left.offset = right.offset = 0;
  }

  // Yield to acquisition between bursts, even at 100 kHz. A slow panel
  // stretches the frame period; neither target changes during transmission.
  const auto start = micros();
  static bool leftTurn = true;
  do {
    if (pending(left) && (leftTurn || !pending(right))) flushChunk(left);
    else if (pending(right)) flushChunk(right);
    leftTurn = !leftTurn;
  } while ((pending(left) || pending(right)) && micros() - start < 2400);
}
} // namespace display
} // namespace principal
