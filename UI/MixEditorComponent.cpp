#include <Database/Includes/INavigationNode.h>
#include <UI/MainComponent.h>
#include <UI/MixEditorComponent.h>
#include <Utils/StringWriter.h>
#include <Utils/UiUtils.h>
#include <Database/TrackLibrary.h>

namespace jucyaudio
{
    namespace ui
    {

        MixEditorComponent::MixEditorComponent()
            : m_timeline{m_formatManager, m_thumbnailCache}
        {
            m_formatManager.registerBasicFormats();
            setWantsKeyboardFocus(true);

            // Set the timeline as the component to be viewed by the viewport.
            m_viewport.setViewedComponent(&m_timeline, false); // false = don't delete when replaced
            m_viewport.setScrollBarsShown(true, true);
            addAndMakeVisible(m_viewport);
                        
            m_timeline.onCueAttachChanged = [this](TrackId trackId, const database::MixTrack& updatedTrack)
            {
                updateCueAttachInData(trackId, updatedTrack);
            };
            
            m_timeline.onEnvelopeChanged = [this](TrackId trackId, const std::vector<database::EnvelopePoint>& points)
            {
                updateEnvelopeInData(trackId, points);
            };

            m_timeline.onMixChanged = [this]()
            {
                saveMixChanges();
            };
            
            // Set up mix playback callbacks
            m_timeline.onMixPlaybackRequested = [this](double startTime)
            {
                handleMixPlayback(startTime);
            };
            
            // Double-click always plays (doesn't toggle)
            m_timeline.onMixPlaybackAlwaysRequested = [this](double startTime)
            {
                handleMixPlayback(startTime, true);
            };
            
#if MIX_TRANSITION_OLD_PLAYBACK_AVAILABLE
            // Initialize audio
            m_mixPlaybackEngine = std::make_unique<audio::MixPlaybackEngine>();
            m_audioDeviceManager = std::make_unique<juce::AudioDeviceManager>();
            
            // Set up audio device
            m_audioDeviceManager->initialiseWithDefaultDevices(0, 2); // 0 inputs, 2 outputs
            m_audioDeviceManager->addAudioCallback(m_mixPlaybackEngine.get());
#endif
            
            // Set up playback timer
            m_playbackTimer.owner = this;
        }
        
        MixEditorComponent::~MixEditorComponent()
        {
            // Stop playback if playing
            if (m_isPlaying)
            {
                stopMixPlayback();
            }
            
#if MIX_TRANSITION_OLD_PLAYBACK_AVAILABLE
            // Clean up audio
            if (m_audioDeviceManager && m_mixPlaybackEngine)
            {
                m_audioDeviceManager->removeAudioCallback(m_mixPlaybackEngine.get());
            }
#endif
            
            // Unload mix before cleanup
            unloadMix();
        }

        void MixEditorComponent::forceRefresh()
        {
            m_timeline.repaint();
            m_viewport.repaint();
            resized(); // Recalculate viewport content
        }

        void MixEditorComponent::setPlaybackCallback(std::function<void(const juce::File &, double)> callback)
        {
            m_timeline.onPlaybackRequested = callback;
        }

        void MixEditorComponent::setSeekCallback(std::function<void(double)> callback)
        {
            m_timeline.onSeekRequested = callback;
        }
        
        void MixEditorComponent::setMixPlaybackCallback(std::function<void(double)> callback)
        {
            m_timeline.onMixPlaybackRequested = callback;
        }

        void MixEditorComponent::paint(juce::Graphics &g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ListBox::backgroundColourId).darker());
        }

        bool MixEditorComponent::keyPressed(const juce::KeyPress &key)
        {
            if (!m_node)
                return false;

            // Ctrl+Z for undo
            if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0))
            {
                spdlog::info("MixEditorComponent: Undo requested");
                
                auto& undoManager = database::theTrackLibrary.getUndoManager();
                const auto mixId = m_node->getMixProjectLoader().getMixId();
                
                if (undoManager.canUndo(mixId))
                {
                    undoManager.undo(mixId, [this]() {
                        // Refresh the mix after undo
                        if (m_node)
                        {
                            spdlog::info("Refreshing mix after undo");
                            m_node->refreshCache(true);  // Force a complete refresh
                            m_timeline.populateFrom(&(m_node->getMixProjectLoader()));
                            m_timeline.repaint();
                            m_viewport.repaint();
                        }
                    });
                }
                else
                {
                    spdlog::info("No undo available for mix {}", mixId);
                }
                return true;
            }
            // Ctrl+Y for redo
            else if (key == juce::KeyPress('y', juce::ModifierKeys::commandModifier, 0))
            {
                spdlog::info("MixEditorComponent: Redo requested");
                
                auto& undoManager = database::theTrackLibrary.getUndoManager();
                const auto mixId = m_node->getMixProjectLoader().getMixId();
                
                if (undoManager.canRedo(mixId))
                {
                    undoManager.redo(mixId, [this]() {
                        // Refresh the mix after redo
                        if (m_node)
                        {
                            spdlog::info("Refreshing mix after redo");
                            m_node->refreshCache(true);  // Force a complete refresh
                            m_timeline.populateFrom(&(m_node->getMixProjectLoader()));
                            m_timeline.repaint();
                            m_viewport.repaint();
                        }
                    });
                }
                else
                {
                    spdlog::info("No redo available for mix {}", mixId);
                }
                return true;
            }
            
            return false;
        }

        void MixEditorComponent::unloadMix()
        {
            if (m_node)
            {
                m_timeline.releaseMixLoader();
                m_node->release(REFCOUNT_DEBUG_ARGS);
                m_node = nullptr;
            }
        }

        void MixEditorComponent::loadMix(database::MixNode *node)
        {
            assert(node != nullptr && "MixNode should not be null in loadMix()");
            if (m_node)
            {
                m_node->release(REFCOUNT_DEBUG_ARGS); // Release previous node if any
            }
            m_node = node; // Take ownership of the new node
            node->retain(REFCOUNT_DEBUG_ARGS); // Retain the node to ensure it stays valid
            node->refreshCache(false);
            m_timeline.populateFrom(&(node->getMixProjectLoader()));
            
            // Ensure timeline has keyboard focus for playback controls
            m_timeline.grabKeyboardFocus();
        }

        void MixEditorComponent::resized()
        {
            // The viewport now fills the entire editor area.
            m_viewport.setBounds(getLocalBounds());
        }

        void MixEditorComponent::updateCueAttachInData(TrackId trackId, const database::MixTrack& updatedTrack)
        {
            if (!m_node)
            {
                spdlog::error("MixEditorComponent::updateCueAttachInData - No mix node loaded");
                return;
            }
            
            spdlog::info("Updating cue/attach points for track {}", trackId);
            
            // Get access to the mix tracks
            auto& mixTracks = const_cast<audio::MixProjectLoader&>(m_node->getMixProjectLoader()).getMixTracks();
            
            // Find and update the track
            for (auto& track : mixTracks)
            {
                if (track.trackId == trackId)
                {
                    track.cueStart = updatedTrack.cueStart;
                    track.cueEnd = updatedTrack.cueEnd;
                    track.attachFrom = updatedTrack.attachFrom;
                    track.attachTo = updatedTrack.attachTo;
                    spdlog::info("Updated cue/attach points for track {}", trackId);
                    
                    // Save changes
                    saveMixChanges();
                    
                    // Tell timeline to reposition this specific track
                    m_timeline.repositionTrack(trackId);
                    break;
                }
            }
        }
        
        void MixEditorComponent::updateEnvelopeInData(TrackId trackId, const std::vector<database::EnvelopePoint>& points)
        {
            if (!m_node)
            {
                spdlog::error("MixEditorComponent::updateEnvelopeInData - No mix node loaded");
                return;
            }
            
            spdlog::info("Updating envelope for track {} with {} points", trackId, points.size());
            
            // Get access to the mix tracks
            auto& mixTracks = const_cast<audio::MixProjectLoader&>(m_node->getMixProjectLoader()).getMixTracks();
            
            // Find and update the track
            for (auto& track : mixTracks)
            {
                if (track.trackId == trackId)
                {
                    track.envelopePoints = points;
                    spdlog::info("Updated envelope points for track {}", trackId);
                    
                    // Save changes
                    saveMixChanges();
                    break;
                }
            }
        }

        void MixEditorComponent::saveMixChanges()
        {
            if (!m_node)
            {
                spdlog::error("MixEditorComponent::saveMixChanges - No mix node loaded");
                return;
            }
            spdlog::info("Saving mix changes to database");

            // Get the current mix info and tracks
            auto &mixProjectLoader = m_node->getMixProjectLoader();
            auto mixId = mixProjectLoader.getMixId();
            auto &mixTracks = mixProjectLoader.getMixTracks();

            // Get mix info from database
            auto &mixInfo = mixProjectLoader.getMixInfo();
            mixInfo.totalDuration = mixProjectLoader.calculateMixDuration();

            // Save changes back to database
            spdlog::info("About to call createOrUpdateMix for mix {} with {} tracks", mixInfo.mixId, mixTracks.size());
            if (database::theTrackLibrary.getMixManager().createOrUpdateMix(mixInfo, mixTracks))
            {
                spdlog::info("Successfully saved mix changes");
            }
            else
            {
                spdlog::error("Failed to save mix changes");
            }
        }
        
        void MixEditorComponent::handleMixPlayback(double startTime, bool alwaysPlay)
        {
#if MIX_TRANSITION_OLD_PLAYBACK_AVAILABLE
            // Special case: negative value means stop
            if (startTime < 0)
            {
                if (m_isPlaying)
                {
                    spdlog::info("MixEditorComponent::handleMixPlayback - Stopping playback");
                    stopMixPlayback();
                }
                return;
            }
            
            spdlog::info("MixEditorComponent::handleMixPlayback - {} playback at {:.2f}s", 
                        alwaysPlay ? "Starting" : "Toggling", startTime);
            
            if (!m_node)
            {
                spdlog::error("No mix loaded");
                return;
            }
            
            if (m_isPlaying && !alwaysPlay)
            {
                // Toggle off (only for space key)
                stopMixPlayback();
            }
            else
            {
                // Load the mix into the playback engine if not already loaded
                if (!m_mixPlaybackEngine->isMixLoaded() || m_mixPlaybackEngine->getMixLoader() != &m_node->getMixProjectLoader())
                {
                    if (!m_mixPlaybackEngine->loadMix(&m_node->getMixProjectLoader()))
                    {
                        spdlog::error("Failed to load mix into playback engine");
                        return;
                    }
                }
                
                // Set position and start playback
                auto positionMs = std::chrono::milliseconds(static_cast<int64_t>(startTime * 1000));
                spdlog::info("Setting playback position to {} ms", positionMs.count());
                m_mixPlaybackEngine->setPosition(positionMs);
                startMixPlayback();
            }
#else
            // Playback not available during transition
            spdlog::info("Mix playback disabled during transition");
#endif
        }
        
        void MixEditorComponent::startMixPlayback()
        {
#if MIX_TRANSITION_OLD_PLAYBACK_AVAILABLE
            spdlog::info("MixEditorComponent::startMixPlayback");
            
            if (!m_isPlaying)
            {
                // Ensure audio device is open
                if (m_audioDeviceManager && !m_audioDeviceManager->getCurrentAudioDevice())
                {
                    m_audioDeviceManager->initialiseWithDefaultDevices(0, 2);
                    m_audioDeviceManager->addAudioCallback(m_mixPlaybackEngine.get());
                }
                
                m_isPlaying = true;
                
                // Unpause the playback engine
                if (m_mixPlaybackEngine)
                {
                    m_mixPlaybackEngine->setPaused(false);
                }
                
                // Start the timer to update playback position
                m_playbackTimer.startTimer(50); // Update at 20Hz
                
                // Update timeline to show we're playing
                m_timeline.setMixPlaybackPosition(m_mixPlaybackEngine->getPosition().count() / 1000.0);
            }
#endif
        }
        
        void MixEditorComponent::stopMixPlayback()
        {
#if MIX_TRANSITION_OLD_PLAYBACK_AVAILABLE
            spdlog::info("MixEditorComponent::stopMixPlayback");
            
            if (m_isPlaying)
            {
                m_isPlaying = false;
                
                // Stop the timer
                m_playbackTimer.stopTimer();
                
                // Pause the playback engine instead of closing the device
                if (m_mixPlaybackEngine)
                {
                    m_mixPlaybackEngine->setPaused(true);
                }
                
                // Reset playback position display
                m_timeline.setMixPlaybackPosition(-1.0); // Hide the red playhead
            }
#else
            m_isPlaying = false;
#endif
        }
        
        void MixEditorComponent::updatePlaybackPosition()
        {
#if MIX_TRANSITION_OLD_PLAYBACK_AVAILABLE
            if (m_isPlaying && m_mixPlaybackEngine)
            {
                // Get current position from engine
                const auto positionMs = m_mixPlaybackEngine->getPosition();
                const double positionSeconds = positionMs.count() / 1000.0;
                
                // Update timeline display
                m_timeline.setMixPlaybackPosition(positionSeconds);
                
                // Check if we've reached the end
                if (positionMs >= m_mixPlaybackEngine->getTotalDuration())
                {
                    spdlog::info("Playback reached end of mix");
                    stopMixPlayback();
                }
            }
#endif
        }
    } // namespace ui
} // namespace jucyaudio
