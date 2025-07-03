#pragma once
#include <Database/Includes/ITrackInfoScanner.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/Includes/ITagManager.h>
#include <filesystem>
#include <string>


namespace jucyaudio
{
    namespace database
    {
        namespace scanners
        {

            class Id3TagScanner final : public ITrackInfoScanner
            {
            public:
                Id3TagScanner(ITagManager& tagManager)
                    : m_tagManager{tagManager}
                {
                }
            private:
                bool processTrack(TrackInfo &trackInfo) override;

                ITagManager& m_tagManager; // Reference to the tag manager for tag operations   
            };

        } // namespace scanners
    } // namespace database
} // namespace jucyaudio