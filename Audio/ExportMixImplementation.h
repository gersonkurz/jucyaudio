#pragma once

#include <Audio/MixProjectLoader.h>
#include <Audio/Includes/ActiveExportSettings.h>
#include <Database/Includes/MixInfo.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/Includes/Constants.h>
#include <Audio/Includes/IMixExporter.h>
#include <Database/TrackLibrary.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <unordered_map>
#include <unordered_set>

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
            ExportMixImplementation(MixId mixId, const ActiveExportSettings &settings,
                                    MixExporterProgressCallback progressCallback);
            virtual ~ExportMixImplementation() = default;
            ExportResult run();

        protected:
            // @brief Calculate timeline positions for all tracks using ATTACH model
            void calculateTrackPositions();
            
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

            /// @brief Where the render actually writes. Never the file the user asked for.
            ///
            /// The export used to open the target itself, deleting whatever was there first - and the
            /// two steps that can fail, preparing the sources and the mixing loop, both come after
            /// that. So a re-export that could not read one of its tracks destroyed the previous
            /// export and put nothing in its place, and a failure part-way through the render left a
            /// truncated file where a complete one had been. Re-exporting over the last file is the
            /// ordinary way to update one, and an offline drive is the easy way to fail.
            ///
            /// Beside the target rather than in a temporary directory: the two then resolve through
            /// the same parent and are on one filesystem whatever that parent is, so committing is a
            /// replace in place and never a copy. A system temp directory is routinely on another
            /// volume, where it would be.
            const std::filesystem::path &renderTargetPath() const
            {
                return m_renderTargetPath;
            }

            /// @brief Closes whatever the render was writing to, so the file can be moved into place,
            ///        and says whether everything actually reached the disk.
            ///
            /// The answer matters because run() commits the rendered file over the target when it is
            /// true. Closing is the last thing that can fail and the easiest place to fail silently:
            /// a buffered write returns success without touching the OS, and the flush that finally
            /// carries it out happens in a destructor whose result nobody can see. Both formats close
            /// something whose failure would otherwise be invisible, so both answer for themselves.
            virtual bool releaseOutput()
            {
                m_writer.reset();
                return true;
            }

            // @brief Apply MixTrack's internal fades, volume, and crossfade logic
            bool applyMixTrackSpecs(const MixTrack &mixTrackDef, const SampleContext &context, juce::AudioBuffer<float> &masterOutputBlock,
                                    const juce::AudioBuffer<float> &sourceTrackBlock);

            bool fail(const std::string &errorMessage);

            bool prepareActiveTrackSources();

            // Renamed and takes an ActiveTrackSource
            bool contributeFromActiveSource(size_t trackIndex,                // The index of the track in the mix
                                            const SampleContext &overallContext, // Overall timeline context
                                            juce::AudioBuffer<float> &masterOutputBlock);

            const MixExporterProgressCallback m_progressCallback;
            const ActiveExportSettings &m_settings;

            /// @brief Moves the rendered file onto the target, or throws it away. See renderTargetPath.
            bool commitRenderTarget();
            void discardRenderTarget();

            // dynamic members
            std::filesystem::path m_renderTargetPath;
            Duration_t m_totalMixDurationMs;
            juce::int64 m_totalOutputSamples;
            juce::AudioFormatManager m_formatManager;
            std::unique_ptr<juce::AudioFormatWriter> m_writer;
            std::vector<ActiveTrackSource> m_activeSources;
            
            // Calculated timeline positions for ATTACH model
            struct TrackTimelinePosition {
                Duration_t startTime{0};
                Duration_t endTime{0};
                /// @brief False when the row's track could not be resolved, so endTime means nothing.
                ///        Its startTime is still real - the ATTACH chain does not need the audio.
                bool resolved{false};
            };
            std::vector<TrackTimelinePosition> m_trackPositions;

            // Reusable buffers to avoid per-block heap allocation in contributeFromActiveSource
            mutable juce::AudioBuffer<float> m_tempReadBuffer;
            mutable juce::AudioBuffer<float> m_sourceTrackBlock;

            // Warning tracking for structured export result - tracks unique failed track IDs
            std::unordered_set<TrackId> m_failedTracks;

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