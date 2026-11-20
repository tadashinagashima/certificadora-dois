#include "SerialGame.h"
#include "PgnStorage.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

namespace {
class Directory final : public principal::PgnFiles {
 public:
  explicit Directory(std::filesystem::path root) : root_(std::move(root)) {}
  bool begin() override {
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    return !error && std::filesystem::is_directory(root_, error) && !error;
  }
  void end() override {}
  bool exists(const char* path) override {
    std::error_code error;
    const bool found = std::filesystem::exists(local(path), error);
    return found || bool(error);
  }
  bool write(const char* path, const std::string& text) override {
    std::ofstream stream(local(path), std::ios::binary);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.close();
    return bool(stream);
  }
  bool equals(const char* path, const std::string& text) override {
    std::ifstream stream(local(path), std::ios::binary);
    if (!stream) return false;
    const std::string read{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    return !stream.bad() && read == text;
  }
  bool rename(const char* from, const char* to) override {
    std::error_code error;
    std::filesystem::rename(local(from), local(to), error);
    return !error;
  }
 private:
  std::filesystem::path local(const char* path) const { return root_ / (path + 1); }
  std::filesystem::path root_;
};
}

int main(int argc, char** argv) {
  if (argc != 1 && (argc != 3 || std::string(argv[1]) != "--sd")) {
    std::fprintf(stderr, "Uso: tabuleiro_sim [--sd DIRETORIO]\nComandos: help; sair: EOF (Ctrl+Z/Enter no Windows).\n");
    return 2;
  }
  Directory storage(argc == 3 ? argv[2] : "firmware/build/sd");
  principal::SerialGame console;
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
    if (console.app().takeSaveRequest())
      console.app().saved(principal::savePgn(storage, console.app().record().pgn()));
    std::printf("DONE\n");
    std::fflush(stdout);
  }
}
