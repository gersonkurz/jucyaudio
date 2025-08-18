#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <unordered_map>
#include <vector>
#include <functional>
#include <cstdint>

namespace jucyaudio
{
    namespace ui
    {
        class TimerMultiplexer
        {
        public:
            using TimerCallback = std::function<void()>;
            using ClientId = std::size_t;
            
            TimerMultiplexer();
            ~TimerMultiplexer();
            
            // Register a component for timer callbacks at specified frequency
            // Returns a unique client ID for later removal
            ClientId registerClient(juce::Component* component, float desiredHz, TimerCallback callback);
            
            // Unregister a client by ID
            void unregisterClient(ClientId clientId);
            
            // Unregister all callbacks for a specific component (useful in destructors)
            void unregisterComponent(juce::Component* component);
            
            // Called by the main 60Hz timer
            void tick();
            
            // Get statistics for debugging
            struct Stats
            {
                std::size_t totalClients{0};
                std::size_t callbacksThisFrame{0};
                std::uint64_t totalFrames{0};
            };
            Stats getStats() const;
            
        private:
            struct TimerClient
            {
                ClientId id{0};
                juce::Component* component{nullptr};
                TimerCallback callback;
                float desiredHz{0.0f};
                std::uint32_t frameInterval{0};  // How many frames between callbacks
                std::uint32_t frameCounter{0};   // Current frame count for this client
            };
            
            std::unordered_map<ClientId, TimerClient> m_clients;
            ClientId m_nextClientId{1};
            std::uint64_t m_frameCount{0};
            
            // Dirty rectangle tracking
            std::vector<juce::Rectangle<int>> m_dirtyRegions;
            
            static constexpr float BASE_FREQUENCY_HZ{60.0f};
            
            // Convert desired Hz to frame interval
            std::uint32_t calculateFrameInterval(float desiredHz) const;
        };
    }
}