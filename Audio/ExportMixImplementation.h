#pragma once

#include <Audio/MixProjectLoader.h>
#include <Database/Includes/MixInfo.h>
#include <Database/Includes/TrackInfo.h>
#include <Audio/Includes/IMixExporter.h>
#include <Database/TrackLibrary.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <unordered_map>

namespace jucyaudio
{
    namespace audio
    {
        using namespace database;

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
                              [[maybe_unused]] double targetSampleRate, [[maybe_unused]] int targetNumChannels);
        };

        class ExportMixImplementation : public MixProjectLoader
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

            // @brief Apply MixTrack's internal fades, volume, and crossfade logic
            bool applyMixTrackSpecs(const MixTrack &mixTrackDef, const SampleContext &context, juce::AudioBuffer<float> &masterOutputBlock,
                                    const juce::AudioBuffer<float> &sourceTrackBlock);

            bool fail(const std::string &errorMessage);

            bool prepareActiveTrackSources();

            // Renamed and takes an ActiveTrackSource
            bool contributeFromActiveSource(ActiveTrackSource &activeSource,     // Non-const to update reader pos
                                            const SampleContext &overallContext, // Overall timeline context
                                            juce::AudioBuffer<float> &masterOutputBlock);

            const MixExporterProgressCallback m_progressCallback;
            const std::filesystem::path m_targetFilepath;

            // dynamic members
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
    } // namespace audio
} // namespace jucyaudio