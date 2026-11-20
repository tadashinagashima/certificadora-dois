#pragma once

#include "BoardApp.h"
#include "PgnStorage.h"

namespace principal {

class Hardware final : public PgnFiles {
 public:
  void setup();
  bool initialiseMatrix();
  bool scan(chessboard::Occupancy& value);
  std::uint8_t buttons() const;
  void indicate(Indicators lights);
  bool ready() const { return ready_; }
  bool begin() override;
  void end() override;
  bool exists(const char* path) override;
  bool write(const char* path, const std::string& text) override;
  bool equals(const char* path, const std::string& text) override;
  bool rename(const char* from, const char* to) override;
 private:
  bool fail();
  bool wireReady_ = false, ready_ = false;
};

} // namespace principal
