#include "Mp3QuickCheck.h"
#include <array>
#include <cstring>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
        namespace background_tasks
        {

            // MP3 bitrates table [version][layer][bitrate_index]
            static const int mp3_bitrates[2][3][16] = {{
                                                           // MPEG-1
                                                           {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0}, // Layer I
                                                           {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0},    // Layer II
                                                           {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0}      // Layer III
                                                       },
                {
                    // MPEG-2 & 2.5
                    {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0}, // Layer I
                    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},      // Layer II
                    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0}       // Layer III
                }};

            bool Mp3QuickCheck::isMp3Header(uint32_t header)
            {
                // Check frame sync (11 bits set)
                if ((header & 0xFFE00000) != 0xFFE00000)
                    return false;

                // Check MPEG version (not reserved)
                int version = (header >> 19) & 3;
                if (version == 1)
                    return false; // Reserved

                // Check layer (not reserved)
                int layer = (header >> 17) & 3;
                if (layer == 0)
                    return false; // Reserved

                // Check bitrate (not bad)
                int bitrate_index = (header >> 12) & 0xF;
                if (bitrate_index == 0xF)
                    return false; // Bad

                // Check sample rate (not reserved)
                int sample_rate = (header >> 10) & 3;
                if (sample_rate == 3)
                    return false; // Reserved

                return true;
            }

            int Mp3QuickCheck::getBitrate(uint32_t header)
            {
                int version = (header >> 19) & 3;
                int layer = (header >> 17) & 3;
                int bitrate_index = (header >> 12) & 0xF;

                // Convert to array indices
                int version_idx = (version == 3) ? 0 : 1; // MPEG-1 or MPEG-2/2.5
                int layer_idx = 3 - layer;                // Layer I, II, or III (layer is 2 bits, so layer_idx ∈ [0,3])

                if (layer_idx > 2)
                    return 0;

                return mp3_bitrates[version_idx][layer_idx][bitrate_index];
            }

            bool Mp3QuickCheck::checkVBRHeader(std::ifstream &file, size_t dataStart)
            {
                // Look for Xing/Info or VBRI headers
                file.seekg(dataStart);

                // Read potential VBR header location
                std::array<char, 200> buffer{};  // Zero-initialize
                file.read(buffer.data(), buffer.size());
                const auto bytesRead = static_cast<size_t>(file.gcount());

                if (bytesRead < 40)  // Minimum needed for VBRI check at offset 36
                {
                    return false;
                }

                // Check for Xing or Info tag (only in bytes actually read)
                for (size_t i = 0; i + 4 <= bytesRead; ++i)
                {
                    if (memcmp(buffer.data() + i, "Xing", 4) == 0 || memcmp(buffer.data() + i, "Info", 4) == 0)
                    {
                        return true; // VBR detected
                    }
                }

                // Check for VBRI tag (usually at offset 36 from frame start)
                // Note: bytesRead is guaranteed >= 40 due to check above
                if (memcmp(buffer.data() + 36, "VBRI", 4) == 0)
                {
                    return true; // VBR detected
                }

                return false;
            }

            std::optional<size_t> Mp3QuickCheck::audioStart(std::istream &stream)
            {
                // Both reads are checked before a byte of them is looked at: a truncated file would
                // otherwise leave the arrays partly unwritten, and the tag size be computed from
                // uninitialized memory (#79).
                std::array<char, 3> magic{};
                stream.read(magic.data(), magic.size());
                if (stream.gcount() != static_cast<std::streamsize>(magic.size()))
                {
                    return std::nullopt;
                }
                if (memcmp(magic.data(), "ID3", magic.size()) != 0)
                {
                    return size_t{0};
                }

                // Version (2 bytes) and flags (1 byte), then the size.
                std::array<char, 3> versionAndFlags{};
                stream.read(versionAndFlags.data(), versionAndFlags.size());
                std::array<unsigned char, 4> sizeBytes{};
                stream.read(reinterpret_cast<char *>(sizeBytes.data()), sizeBytes.size());
                if (!stream || stream.gcount() != static_cast<std::streamsize>(sizeBytes.size()))
                {
                    return std::nullopt;
                }

                // ID3v2 size is a syncsafe integer: seven bits per byte.
                const size_t tagSize = (static_cast<size_t>(sizeBytes[0] & 0x7F) << 21) | (static_cast<size_t>(sizeBytes[1] & 0x7F) << 14) |
                    (static_cast<size_t>(sizeBytes[2] & 0x7F) << 7) | static_cast<size_t>(sizeBytes[3] & 0x7F);
                return 10 + tagSize;
            }

            std::optional<Mp3Info> Mp3QuickCheck::checkMp3File(const std::filesystem::path &path)
            {
                Mp3Info info;

                try
                {
                    // Get file size
                    info.fileSize = std::filesystem::file_size(path);

                    // Skip very large MP3s (likely DJ mixes)
                    if (info.fileSize > 50 * 1024 * 1024)
                    { // 50MB
                        spdlog::debug("Skipping large MP3 file ({}MB): {}", info.fileSize / (1024 * 1024), path.string());
                        return std::nullopt;
                    }

                    // Open file for quick header check. From here on, anything this check cannot read is
                    // left to the decoder: only the size and bitrate rules are reasons to skip a file,
                    // and the decoder is what knows whether it is readable.
                    std::ifstream file(path, std::ios::binary);
                    if (!file)
                    {
                        return info;
                    }

                    const auto start = audioStart(file);
                    if (!start.has_value())
                    {
                        spdlog::debug("MP3 file too short for its ID3v2 header, leaving it to the decoder: {}", path.string());
                        return info;
                    }
                    const size_t dataStart = *start;

                    // Read first MP3 frame header
                    file.clear();
                    file.seekg(static_cast<std::streamoff>(dataStart));
                    unsigned char header_bytes[4] = {0};
                    file.read(reinterpret_cast<char *>(header_bytes), 4);

                    if (file.gcount() < 4)
                    {
                        spdlog::debug("MP3 file too short for a header, leaving it to the decoder: {}", path.string());
                        return info;
                    }

                    uint32_t header = (header_bytes[0] << 24) | (header_bytes[1] << 16) | (header_bytes[2] << 8) | header_bytes[3];

                    // No frame where the ID3v2 tag says the audio starts. Usually padding the tag did
                    // not declare, which the decoder scans past (#77): rejecting here turned playable
                    // files into bad_format. The bitrate and VBR rules need a header, so they cannot
                    // be applied; the size rule above already was.
                    if (!isMp3Header(header))
                    {
                        spdlog::debug("No MP3 frame at the declared start, leaving it to the decoder: {}", path.string());
                        return info;
                    }

                    // Get bitrate from first frame
                    info.bitrate = getBitrate(header);

                    // Skip very low bitrate files (likely spoken word/podcasts)
                    // Lowered threshold to 64kbps to allow more music files
                    if (info.bitrate > 0 && info.bitrate < 64)
                    {
                        spdlog::debug("Skipping low bitrate MP3 ({}kbps): {}", info.bitrate, path.string());
                        return std::nullopt;
                    }

                    // Check for VBR
                    info.isVBR = checkVBRHeader(file, dataStart + 4);

                    // For now, DON'T skip VBR files - most modern MP3s are VBR
                    // We'll only skip if they're BOTH VBR AND large
                    if (info.isVBR && info.fileSize > 30 * 1024 * 1024)  // 30MB
                    {
                        spdlog::debug("Skipping large VBR MP3 ({}MB): {}", 
                                     info.fileSize / (1024 * 1024), path.string());
                        return std::nullopt;
                    }

                    info.isValid = true;
                    return info;
                }
                catch (const std::exception &e)
                {
                    spdlog::debug("Error checking MP3 file {}, leaving it to the decoder: {}", path.string(), e.what());
                    return info;
                }
            }
        } // namespace background_tasks
    } // namespace database
} // namespace jucyaudio
