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
    // Quick check of MP3 file without full parsing.
    // Returns an empty optional only when a policy says to skip the file: larger than 50 MB, below
    // 64 kbps, or VBR and larger than 30 MB. Whether the file is readable is the decoder's call, so
    // a file this check cannot parse is returned with isValid false, not skipped.
    static std::optional<Mp3Info> checkMp3File(const std::filesystem::path& path);

    // Where the first MP3 frame is expected: 0 without an ID3v2 tag, else the 10-byte header plus the
    // size it declares. Empty if the stream is too short to tell - fewer than 3 bytes, or an "ID3"
    // header cut off before its size bytes. Reads from the stream's current position.
    static std::optional<size_t> audioStart(std::istream& stream);

private:
    // MP3 header parsing
    static bool isMp3Header(uint32_t header);
    static int getBitrate(uint32_t header);
    static bool checkVBRHeader(std::ifstream& file, size_t dataStart);
};

} // namespace background_tasks
} // namespace database
} // namespace jucyaudio