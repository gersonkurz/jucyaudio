#pragma once

#include <Audio/Includes/IMixExporter.h>
#include <Audio/MixExporter.h>
#include <Database/Includes/Constants.h>
#include <Database/Includes/IMixManager.h>
#include <Database/Includes/ITrackDatabase.h>
#include <Database/Includes/MixInfo.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/TrackLibrary.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <Utils/AssortedUtils.h>
#include <filesystem>
#include <spdlog/fmt/chrono.h>
#include <spdlog/spdlog.h>
#include <lame/lame.h>

namespace jucyaudio
{
    namespace audio
    {
        using namespace jucyaudio::database;

        struct SampleContext
        {
            juce::int64 samplesWrittenTotal{0};
            juce::int64 readStartInOutputTimeline{0};
            juce::int64 readEndInOutputTimeline{0};
            juce::int64 numSamplesToReadFromSource{0};
            juce::int64 trackMixStartSamples{0};
            juce::int64 currentBlockStartTimeSamples{0};
            juce::int64 currentBlockEndTimeSamples{0};
            juce::int64 samplesToProcessInThisBlock{0};
            juce::int64 trackFileEffectiveDurationSamples{0};
        };

        // Inside Implementation class
        struct ActiveTrackSource
        {
            TrackId trackId;
            const TrackInfo *trackInfoPtr;  // From m_trackInfosMap
            const MixTrack *mixTrackDefPtr; // From m_mixTracks

            std::unique_ptr<juce::AudioFormatReader> reader;
            std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
            // Optional: std::unique_ptr<juce::ResamplingAudioSource> resamplerSource;
            // Add other state needed per active track, e.g., current read position in source file

            // Constructor to open reader, setup sources
            ActiveTrackSource(TrackId id, const TrackInfo *ti, const MixTrack *mtd, juce::AudioFormatManager &formatManager,
                              [[maybe_unused]] double targetSampleRate, [[maybe_unused]] int targetNumChannels)
                : trackId{id},
                  trackInfoPtr{ti},
                  mixTrackDefPtr{mtd}
            {
                juce::File sourceFile{pathToString(trackInfoPtr->filepath)};
                reader.reset(formatManager.createReaderFor(sourceFile));
                if (reader)
                {
                    readerSource = std::make_unique<juce::AudioFormatReaderSource>(reader.get(), false /*don't delete reader*/);

                    // TODO: Setup resampling if reader->sampleRate != targetSampleRate or reader->numChannels != targetNumChannels
                    // if (reader->sampleRate != targetSampleRate || reader->numChannels != targetNumChannels) {
                    //    resamplerSource = std::make_unique<juce::ResamplingAudioSource>(readerSource.get(), false, targetNumChannels);
                    //    resamplerSource->setResamplingRatio(reader->sampleRate / targetSampleRate);
                    //    // The 'source' to call getNextAudioBlock on would then be resamplerSource.get()
                    // }
                    // For now, assume readerSource is what we use.
                    // Prepare readerSource for reading
                    // readerSource->prepareToPlay(processingBlockSize, targetSampleRate); // Example block size
                }
                else
                {
                    spdlog::error("MTE: Failed to create reader for track ID {} ({})", id, pathToString(trackInfoPtr->filepath));
                }
            }

            // Method to get audio, handles internal fades, volume etc.
            // void getAudioBlock(juce::AudioBuffer<float>& targetBuffer, juce::int64 startSampleInSource, int numSamples) { ... }
            // Or, more JUCE-like: have it conform to AudioSource interface and use getNextAudioBlock
        };

        class ExportMixImplementation
        {
        public:
            ExportMixImplementation(MixId mixId, const TrackLibrary &trackLibrary, const std::filesystem::path &targetFilepath,
                                    MixExporterProgressCallback progressCallback);
            virtual ~ExportMixImplementation() = default;
            bool run();

        protected:
            // @brief Calculate the total mix duration based on the last track's effective duration.
            // This is needed to create the main output buffer or to know how much to write.
            // The end time of the last track (lastTrack.mixStartTime + lastTrackEffectiveDuration)
            // where lastTrackEffectiveDuration is its length after cutoffTime and internal fades.
            bool calculateMixDuration();

            // @brief Calculate the total number of output samples based on the mix duration and output sample rate.
            // Analyze tracks to determine actual max SR/Channels or if resampling is needed.
            // For now, we'll assume sources will be resampled/mixed to outputSampleRate()/outputNumChannels() by JUCE if needed.
            bool calculateTotalOutputSamples();

            // @brief Setup the JUCE AudioFormatManager and create an AudioFormatWriter for the target file.
            bool setupAudioFormatManagerAndWriter();

            // @brief This is a pure virtual function to be implemented by derived classes.
            // It is *not* the same as setupAudioFormatManagerAndWriter because the base run()
            // implementation retains a member-function pointer to the former.
            virtual bool onSetupAudioFormatManagerAndWriter() = 0;

            // @brief This is more complex than the A/B stitch. We need to manage multiple sources
            // potentially active at the same time, placed correctly on the output timeline.
            bool runMixingLoop();
            virtual bool onRunMixingLoop() = 0;

            const TrackInfo *getTrackInfoById(TrackId trackId) const
            {
                const auto it = m_trackInfosMap.find(trackId);
                if (it != m_trackInfosMap.end())
                {
                    return it->second; // Return cached TrackInfo pointer
                }
                return nullptr; // Not found
            }

            TrackQueryArgs getMixTrackQueryArgs(MixId mixId) const
            {
                TrackQueryArgs args;
                args.mixId = mixId;
                args.usePaging = false; // We want all tracks in the mix
                return args;
            }

            // @brief Apply MixTrack's internal fades, volume, and crossfade logic
            bool applyMixTrackSpecs(const MixTrack &mixTrackDef, const SampleContext &context, juce::AudioBuffer<float> &masterOutputBlock,
                                    const juce::AudioBuffer<float> &sourceTrackBlock);

            bool fail(const std::string &errorMessage);

            bool prepareActiveTrackSources();

            // Renamed and takes an ActiveTrackSource
            bool contributeFromActiveSource(ActiveTrackSource &activeSource,     // Non-const to update reader pos
                                            const SampleContext &overallContext, // Overall timeline context
                                            juce::AudioBuffer<float> &masterOutputBlock);

            const TrackLibrary &m_trackLibrary;
            const std::vector<MixTrack> m_mixTracks;
            const std::vector<TrackInfo> m_trackInfos;
            const MixId m_mixId;
            const MixExporterProgressCallback m_progressCallback;
            const std::filesystem::path m_targetFilepath;

            // dynamic members
            std::unordered_map<TrackId, const TrackInfo *> m_trackInfosMap; // Cache for track infos by ID
            Duration_t m_totalMixDurationMs;
            juce::int64 m_totalOutputSamples;
            juce::AudioFormatManager m_formatManager;
            std::unique_ptr<juce::AudioFormatWriter> m_writer;
            std::vector<ActiveTrackSource> m_activeSources;

            // TBD: Determine Output Format Properties (Sample Rate, Channels)
            //    - Iterate through tracks, find the highest sample rate, max channels, or enforce a standard.
            //    - For simplicity, let's try to use the sample rate/channels of the first track,
            //      or enforce a standard (e.g., 44100 Hz, 2 Channels) and resample if necessary.
            //      This needs careful consideration.

            // @brief Output sample rate for the target format is 44100 Hz
            constexpr double outputSampleRate()
            {
                return 44100.0;
            }

            // @brief Output number of channels for the target format is Stereo (2 channels)
            constexpr unsigned int outputNumChannels()
            {
                return 2;
            }

            // @brief Output bit depth for the target format is 16-bit PCM
            constexpr int outputBitDepth()
            {
                return 16;
            }
        };

        class ExportMp3MixImplementation final : public ExportMixImplementation
        {
        public:
            ExportMp3MixImplementation(MixId mixId, const TrackLibrary &trackLibrary, const std::filesystem::path &targetFilepath,
                                       MixExporterProgressCallback progressCallback)
                : ExportMixImplementation(mixId, trackLibrary, targetFilepath, progressCallback)
            {
            }
            JUCE_DECLARE_NON_COPYABLE(ExportMp3MixImplementation)
        private:
            bool onSetupAudioFormatManagerAndWriter() override;
            bool onRunMixingLoop() override; // Override to use LAME instead of JUCE writer

            // LAME-specific members
            lame_global_flags *m_lameFlags = nullptr;
            std::unique_ptr<juce::FileOutputStream> m_outputStream;
            unsigned char *m_mp3Buffer = nullptr;
            int m_mp3BufferSize = 0;

            // Override destructor to clean up LAME
            ~ExportMp3MixImplementation() override;
        };
        class ExportWavMixImplementation final : public ExportMixImplementation
        {
        public:
            ExportWavMixImplementation(MixId mixId, const TrackLibrary &trackLibrary, const std::filesystem::path &targetFilepath,
                                       MixExporterProgressCallback progressCallback)
                : ExportMixImplementation(mixId, trackLibrary, targetFilepath, progressCallback)
            {
            }
            JUCE_DECLARE_NON_COPYABLE(ExportWavMixImplementation)

        private:
            bool onSetupAudioFormatManagerAndWriter() override;
            bool onRunMixingLoop() override;
        };

    } // namespace audio
} // namespace jucyaudio
