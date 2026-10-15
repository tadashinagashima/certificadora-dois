#include "SerialGame.h"
#include <cstdio>

int main() {
  principal::SerialGame game;
  principal::LineReceiver receiver;
  std::uint32_t now = 0;
  for (int input; (input = std::getchar()) != EOF;) {
    const auto result = receiver.push(static_cast<char>(input));
    if (result == principal::LineReceiver::Result::Pending) continue;
    if (result == principal::LineReceiver::Result::Invalid) {
      game.inputLost();
      std::printf("ERROR Line\n");
    } else {
      const auto output = game.command(receiver.line(), now++);
      std::fwrite(output.data(), 1, output.size(), stdout);
    }
    std::printf("DONE\n");
    std::fflush(stdout);
  }
}
