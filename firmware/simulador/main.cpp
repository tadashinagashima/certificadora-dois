#include "SerialGame.h"
#include <cstdio>
#include <random>

int main(int argc, char**) {
  if (argc != 1) {
    std::fprintf(stderr, "Uso: tabuleiro_sim (stdin/stdout). Para gravar PGN: "
                         "python firmware/terminal.py --sim EXECUTAVEL [--destino PASTA]\n");
    return 2;
  }
  std::random_device random;
  const auto bootId = (std::uint64_t{random()} << 32) | random();
  principal::SerialGame console(true, bootId);
  principal::LineReceiver receiver;
  for (int input; (input = std::getchar()) != EOF;) {
    const auto result = receiver.push(static_cast<char>(input));
    if (result == principal::LineReceiver::Result::Pending) continue;
    if (result == principal::LineReceiver::Result::Invalid) {
      console.inputLost();
      std::printf("ERROR Line\n");
    } else {
      const auto output = console.command(receiver.line(), 0);
      std::fwrite(output.data(), 1, output.size(), stdout);
    }
    std::printf("DONE\n");
    std::fflush(stdout);
  }
}
