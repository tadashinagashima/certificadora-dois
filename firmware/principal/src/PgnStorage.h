#pragma once

#include <cstdio>
#include <string>

namespace principal {

// Small file adapter: SD on ESP32, a directory on the computer.
struct PgnFiles {
  virtual ~PgnFiles() = default;
  virtual bool begin() = 0;
  virtual void end() = 0;
  virtual bool exists(const char* path) = 0;
  virtual bool write(const char* path, const std::string& text) = 0;
  virtual bool equals(const char* path, const std::string& text) = 0;
  virtual bool rename(const char* from, const char* to) = 0;
};

// Never truncate an old game. An interrupted write leaves a reserved .TMP name.
// Verify the complete file before publishing .PGN. This is not power-loss atomicity.
inline std::string savePgn(PgnFiles& files, const std::string& pgn) {
  if (!files.begin()) { files.end(); return {}; }
  std::string saved;
  for (unsigned i = 1; i <= 9999; ++i) {
    char path[16], temporary[16];
    std::snprintf(path, sizeof(path), "/GAME%04u.PGN", i);
    std::snprintf(temporary, sizeof(temporary), "/GAME%04u.TMP", i);
    if (files.exists(path) || files.exists(temporary)) continue;
    if (files.write(temporary, pgn) && files.equals(temporary, pgn) && files.rename(temporary, path))
      saved = path;
    break;
  }
  files.end();
  return saved;
}

} // namespace principal
