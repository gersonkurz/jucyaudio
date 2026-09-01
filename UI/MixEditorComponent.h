#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Includes/INavigationNode.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/TrackLibrary.h>
#include <Database/Nodes/MixNode.h>
#include <UI/DynamicColumnManager.h>
#include <UI/TimelineComponent.h>
#include <UI/MarkerRulerComponent.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <Audio/MixProjectLoader.h>
#include <Audio/MixPlaybackEngine.h>
#include <memory>

namespace jucyaudio
{
    namespace ui
    {
        // Forward declarations
        class PlaybackController;
        
        // Lightweight overlay component that only draws the playhead
        class PlayheadOverlay : public juce::Component
        {
        public:
            PlayheadOverlay() { setInterceptsMouseClicks(false, false); }
            
            void setPlayheadPosition(double positionSeconds, double pixelsPerSecond)
            {
                constexpr double epsilon = 1e-9;
                if (std::abs(m_positionSeconds - positionSeconds) > epsilon || std::abs(m_pixelsPerSecond - pixelsPerSecond) > epsilon)
                {
                    // Calculate old and new x positions
                    const int oldX = (m_positionSeconds >= 0) ? static_cast<int>(m_positionSeconds * m_pixelsPerSecond) : -1;
                    const int newX = (positionSeconds >= 0) ? static_cast<int>(positionSeconds * pixelsPerSecond) : -1;
                    
                    m_positionSeconds = positionSeconds;
                    m_pixelsPerSecond = pixelsPerSecond;
                    
                    // Only repaint the areas that changed
                    if (oldX >= 0)
                        repaint(oldX - 7, 0, 14, getHeight());
                    if (newX >= 0)
                        repaint(newX - 7, 0, 14, getHeight());
                }
            }
            
            void paint(juce::Graphics& g) override
            {
                if (m_positionSeconds >= 0.0 && m_pixelsPerSecond > 0)
                {
                    const float playheadX = static_cast<float>(m_positionSeconds * m_pixelsPerSecond);
                    
                    // Draw the red playhead line
                    g.setColour(juce::Colours::red);
                    g.fillRect(playheadX - 1.0f, 0.0f, 2.0f, static_cast<float>(getHeight()));
                    
                    // Draw triangle at top
                    juce::Path playheadMarker;
                    playheadMarker.addTriangle(playheadX - 6, 0, playheadX + 6, 0, playheadX, 12);
                    g.fillPath(playheadMarker);
                }
            }
            
        private:
            double m_positionSeconds = -1.0;
            double m_pixelsPerSecond = 100.0;
        };
        
        class MixEditorComponent : public juce::Component, 
                                    private juce::ScrollBar::Listener
        {
        public:
            MixEditorComponent();
            ~MixEditorComponent() override;

            void paint(juce::Graphics &g) override;
            void resized() override;
            bool keyPressed(const juce::KeyPress &key) override;

            /// @brief What removeUndecodableTracks() actually did, so the user can be told accurately.
            enum class RemovalOutcome
            {
                NothingToRemove,   ///< No track was proven undecodable.
                Removed,           ///< The mix was shortened and persisted.
                SkippedReadOnly,   ///< The mix is exported; it must not be edited.
                SkippedWouldEmpty, ///< Every row was decode-rejected; something systemic, not the files.
                PersistFailed,     ///< The database rejected the change; the mix is unchanged.
                ReloadFailed       ///< Persisted, but the in-memory mix could not be refreshed.
            };

            void loadMix(database::MixNode* node);
            void unloadMix();
            void forceRefresh();

            /**
             * @brief Rebuilds the timeline after the node's cache has been reloaded from outside.
             *
             * MixNode::refreshCache(true) makes MixProjectLoader replace its track vector, and the
             * timeline holds raw pointers into that vector - so every reload leaves the visible
             * timeline pointing at destroyed objects, whether the reload succeeded or not. Anything
             * that refreshes a node the editor is showing has to call this straight afterwards.
             *
             * @param node The node that was reloaded; ignored unless it is the one on screen.
             */
            void onNodeCacheReloaded(const database::MixNode *node);

            // Set the playback controller for unified playback
            void setPlaybackController(PlaybackController* controller);
            PlaybackController* getPlaybackController() { return m_playbackController; }
        
            // Get the current mix node
            database::MixNode* getCurrentMixNode() const { return m_node; }

            auto &getTimeline()
            {
                return m_timeline;
            }

            // Set callback for when mix export status changes (for navigation tree refresh)
            void setOnMixExportStatusChangedCallback(std::function<void()> callback)
            {
                m_onMixExportStatusChanged = std::move(callback);
            }

            void setOnMixSummaryChangedCallback(std::function<void()> callback)
            {
                m_onMixSummaryChanged = std::move(callback);
            }

            void setOnShowTrackInLibraryCallback(std::function<void(TrackId)> callback)
            {
                m_onShowTrackInLibrary = std::move(callback);
            }

            void setOnShowTrackDetailsCallback(std::function<void(TrackId)> callback)
            {
                m_onShowTrackDetails = std::move(callback);
            }

            // Playback is now handled by PlaybackController

            void handleDeleteSelectedTrack();
            void handlePasteTracks(const std::vector<database::MixTrack>& tracks, int position, bool before);
            void handleRemoveFollowingTracks(int afterOrder);
            void updatePlayhead();

        private:
            void updateCueAttachInData(int orderInMix, const database::MixTrack& updatedTrack);
            void updateEnvelopeInData(int orderInMix, const std::vector<database::EnvelopePoint>& points);
            void updateGainAdjustmentInData(int orderInMix, float newGain, bool saveToDatabase);
            void updateCuePointsInData(int orderInMix, jucyaudio::Duration_t cueStart, jucyaudio::Duration_t cueEnd);
            void saveMixChanges();
            void handleMixPlayback(double startTime, bool alwaysPlay = false);
            // Called by TimerMultiplexer for smooth playhead updates
            void loadMixMarkers();
            void saveMixMarker(const database::MixMarker& marker);
            void handleMarkerClick(MarkerId markerId);
            void handleMarkerAdd(std::chrono::milliseconds position);
            void showMoveBackDialog();
            /**
             * @brief Refresh the node's cached track count/duration, then tell the outside world.
             *
             * Both halves matter: the node caches the summary the status bar and mix list read, so
             * firing the outer callback on its own leaves those showing pre-change figures.
             */
            void notifyMixSummaryChanged();

            // Waveform loading helpers  
            std::vector<std::pair<int, bool>> collectWaveformRequests(audio::MixProjectLoader* loader);
            /**
             * @brief Drops tracks whose audio the decoder rejected from the mix.
             *
             * Only failures that prove the content is undecodable are eligible - see
             * WaveformLoadingTask::provesAudioUnusable(). A missing file or a timeout says nothing about
             * the audio and must never cost the user a track.
             *
             * @param loader The mix being opened; reloaded from the database if anything was removed.
             * @param undecodableTrackIds Tracks the decoder rejected.
             * @return What happened, for reporting.
             */
            RemovalOutcome removeUndecodableTracks(audio::MixProjectLoader &loader, const std::vector<TrackId> &undecodableTrackIds);
            void populateTimeline(audio::MixProjectLoader* loader);
            
            // ScrollBar::Listener callbacks
            void scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) override;
            void updatePlayheadOverlayPosition();
            
            juce::AudioFormatManager m_formatManager;
            juce::AudioThumbnailCache m_thumbnailCache{200}; // 200 items in the cache - enough for large mixes

            MarkerRulerComponent m_markerRuler;
            TimelineComponent m_timeline;
            PlayheadOverlay m_playheadOverlay;  // Separate component for playhead
            juce::Viewport m_viewport;
            database::MixNode *m_node{nullptr};

            // Read-only mode for exported mixes
            bool m_isReadOnly{false};

            /// @brief Why the mix is read-only, when it is. Enforcement does not care - m_isReadOnly
            ///        decides that - but the user is owed the right reason: a mix that could not be
            ///        read is not a mix that was exported, and telling them to unlock it in the tree
            ///        sends them after a lock that is not there.
            ///
            /// Asked of the node rather than remembered. A second flag would have to be set everywhere
            /// m_isReadOnly is, and the first version of this was already wrong in both directions -
            /// stale true after an unreadable mix was replaced by a readable exported one, and never
            /// set at all by the reload path. The node knows; there is nothing to keep in step.
            bool isReadOnlyBecauseUnreadable() const
            {
                return m_node != nullptr && !m_node->isCacheLoaded();
            }

            // Playback controller reference
            PlaybackController* m_playbackController{nullptr};

            // Scrolling detection to avoid update conflicts
            int64_t m_lastScrollTime{0};
            static constexpr int64_t SCROLL_PAUSE_DURATION_MS = 100; // Pause updates for 100ms after scroll

            // Track the currently loaded mix ID to detect when switching mixes
            MixId m_currentMixId{0};

            // Callback for when mix export status changes
            std::function<void()> m_onMixExportStatusChanged;
            std::function<void()> m_onMixSummaryChanged;
            std::function<void(TrackId)> m_onShowTrackInLibrary;
            std::function<void(TrackId)> m_onShowTrackDetails;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixEditorComponent)
        };

    } // namespace ui
} // namespace jucyaudio
