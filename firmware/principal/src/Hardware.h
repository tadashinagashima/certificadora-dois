#pragma once

#include "BoardApp.h"

namespace principal {

class Hardware {
 public:
  void setup();
  bool initialiseMatrix();
  bool scan(chessboard::Occupancy& value);
  void indicate(Indicators lights);
  bool ready() const { return ready_; }
  bool busReady() const { return wireReady_; }
 private:
  bool fail();
  bool wireReady_ = false, ready_ = false;
};

} // namespace principal
