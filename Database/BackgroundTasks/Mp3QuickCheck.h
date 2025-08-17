#pragma once

#include <filesystem>
#include <optional>
#include <fstream>

namespace jucyaudio {
namespace database {
namespace background_tasks {

struct Mp3Info {
    bool isVBR = false;
    int bitrate = 0;
    size_t fileSize = 0;
    bool isValid = false;
};

class Mp3QuickCheck {
public:
    // Quick check of MP3 file without full parsing
    // Returns empty optional if file should be skipped
    static std::optional<Mp3Info> checkMp3File(const std::filesystem::path& path);
    
private:
    // MP3 header parsing
    static bool isMp3Header(uint32_t header);
    static int getBitrate(uint32_t header);
    static bool checkVBRHeader(std::ifstream& file, size_t dataStart);
};

} // namespace background_tasks
} // namespace database
} // namespace jucyaudio