#include <Database/Scanners/Id3TagScanner.h>
#include <Utils/AssortedUtils.h>
#include <spdlog/spdlog.h>
#include <taglib/fileref.h>
#include <taglib/id3v2tag.h> // For specific ID3v2 access if needed
#include <taglib/tag.h>
#include <taglib/taglib.h>
#include <taglib/tpropertymap.h>

namespace jucyaudio
{
    namespace database
    {
        namespace scanners
        {

            bool Id3TagScanner::processTrack(TrackInfo &trackInfo)
            {
                // TagLib uses C-style strings for paths, and often expects system native encoding.
                // On macOS/Linux, filePath.string() (UTF-8 if locale is UTF-8) is usually fine.
                // On Windows, TagLib might prefer wide strings or have specific UTF-8 path handling.
                // For TagLib, filePath.wstring() for Windows or filePath.string() for others is common.
                // Let's assume TagLib::FileRef handles path encoding reasonably.
                // filePath.c_str() is problematic if path contains non-ASCII and not UTF-8 aware.
                // Using native() for TagLib is often recommended.

                // TagLib::FileRef f(filePath.c_str()); // Potentially problematic with non-ASCII paths
                // Using native path representation is safer with TagLib:
                TagLib::FileRef f{trackInfo.filepath.c_str(), true, TagLib::AudioProperties::Accurate};

                if (f.isNull() || !f.tag())
                {
                    spdlog::warn("TagLib: Could not read tags for: {}", pathToString(trackInfo.filepath));
                    return false;
                }

                const auto tag = f.tag();
                trackInfo.title = tag->title().to8Bit(true); // to8Bit(true) for UTF-8
                trackInfo.artist_name = tag->artist().to8Bit(true);
                trackInfo.album_title = tag->album().to8Bit(true);
                trackInfo.year = tag->year();
                trackInfo.track_number = tag->track();

                trackInfo.tag_ids.clear(); // Clear existing tag IDs before adding new ones
                const auto genreFromTaglib = tag->genre();
                if (!genreFromTaglib.isEmpty())
                {
                    const auto combinedGenreString = genreFromTaglib.to8Bit(true);
                    const auto genreNames = split_string(combinedGenreString, ";,/|"); // Using your splitter idea
                    for (const auto &genreNameWithSpaces : genreNames)
                    {
                        const auto genreName{trim_to_string(genreNameWithSpaces)};
                        if (!genreName.empty())
                        {
                            const auto tagId = m_tagManager.getOrCreateTagId(genreName, true /* create if missing */);
                            if (tagId)
                            {
                                bool found = false;
                                for (const auto existingId : trackInfo.tag_ids)
                                {
                                    if (existingId == *tagId)
                                    {
                                        found = true;
                                        break;
                                    }
                                }
                                if (!found)
                                {
                                    trackInfo.tag_ids.push_back(*tagId);
                                }
                            }
                            else
                            {
                                spdlog::warn("Id3TagScanner: Could not get/create TagId for genre '{}' from file {}", genreName, pathToString(trackInfo.filepath));
                            }
                        }
                    }
                }
                // Example: Get disc number if ID3v2
                if (const auto id3v2tag = dynamic_cast<TagLib::ID3v2::Tag *>(f.tag()))
                {
                    const auto frameListMap = id3v2tag->frameListMap();
                    if (frameListMap.contains("TPOS"))
                    {
                        TagLib::String tpos = frameListMap["TPOS"].front()->toString();
                        // TPOS is often "1/2" for disc 1 of 2. Parse if needed.
                        // For simplicity, just store raw string or parse the first number
                        // info.disc_number = tpos.toInt(); // Needs parsing logic
                    }
                }

                // Extract Bandcamp URL from comments (example)
                if (!tag->comment().isEmpty())
                {
                    std::string comment_str = tag->comment().to8Bit(true);
                    // Look for "bandcamp.com" in comment_str
                    // if (comment_str.find("bandcamp.com") != std::string::npos) {
                    //    info.user_notes += "\nBandcamp URL found in comments."; // Or extract the URL
                    // }
                }

                // Audio Properties (TagLib can also provide these)
                const auto properties = f.audioProperties();
                if (properties)
                {
                    trackInfo.duration = durationFromIntSeconds(properties->lengthInSeconds());
                    trackInfo.bitrate = properties->bitrate();
                    trackInfo.samplerate = properties->sampleRate();
                    trackInfo.channels = properties->channels();
                }

                spdlog::debug("TagLib: Extracted tags for: {}", pathToString(trackInfo.filepath));
                return true;
            }

        } // namespace scanners
    } // namespace database
} // namespace jucyaudio
