#include "TimerMultiplexer.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace jucyaudio
{
    namespace ui
    {
        TimerMultiplexer::TimerMultiplexer()
        {
            spdlog::info("TimerMultiplexer created with base frequency {}Hz", BASE_FREQUENCY_HZ);
        }
        
        TimerMultiplexer::~TimerMultiplexer()
        {
            spdlog::info("TimerMultiplexer destroyed with {} active clients", m_clients.size());
        }
        
        TimerMultiplexer::ClientId TimerMultiplexer::registerClient(
            juce::Component* component, 
            float desiredHz, 
            TimerCallback callback)
        {
            if (!component || !callback)
            {
                spdlog::error("TimerMultiplexer::registerClient called with null component or callback");
                return 0;
            }
            
            if (desiredHz <= 0.0f || desiredHz > BASE_FREQUENCY_HZ)
            {
                spdlog::warn("TimerMultiplexer::registerClient invalid frequency {}Hz, clamping to valid range", desiredHz);
                desiredHz = std::clamp(desiredHz, 1.0f, BASE_FREQUENCY_HZ);
            }
            
            const auto clientId = m_nextClientId++;
            auto& client = m_clients[clientId];
            client.id = clientId;
            client.component = component;
            client.callback = std::move(callback);
            client.desiredHz = desiredHz;
            client.frameInterval = calculateFrameInterval(desiredHz);
            client.frameCounter = 0;
            
            spdlog::debug("TimerMultiplexer registered client {} for component at {}Hz (interval: {} frames)", 
                         clientId, desiredHz, client.frameInterval);
            
            return clientId;
        }
        
        void TimerMultiplexer::unregisterClient(ClientId clientId)
        {
            if (m_clients.erase(clientId) > 0)
            {
                spdlog::debug("TimerMultiplexer unregistered client {}", clientId);
            }
        }
        
        void TimerMultiplexer::unregisterComponent(juce::Component* component)
        {
            if (!component) return;
            
            auto it = m_clients.begin();
            while (it != m_clients.end())
            {
                if (it->second.component == component)
                {
                    spdlog::debug("TimerMultiplexer unregistering client {} for component", it->first);
                    it = m_clients.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
        
        void TimerMultiplexer::tick()
        {
            m_frameCount++;
            std::size_t callbacksThisFrame = 0;
            
            for (auto& [clientId, client] : m_clients)
            {
                client.frameCounter++;
                
                // Check if this client should fire on this frame
                if (client.frameCounter >= client.frameInterval)
                {
                    client.frameCounter = 0;
                    
                    // Ensure component is still valid before calling
                    if (client.component && client.callback)
                    {
                        try
                        {
                            client.callback();
                            callbacksThisFrame++;
                        }
                        catch (const std::exception& e)
                        {
                            spdlog::error("TimerMultiplexer callback error for client {}: {}", clientId, e.what());
                        }
                    }
                }
            }
            
            // Log every second (60 frames) in debug mode
            if (m_frameCount % 60 == 0)
            {
                spdlog::trace("TimerMultiplexer: frame {}, {} clients, {} callbacks this second", 
                             m_frameCount, m_clients.size(), callbacksThisFrame);
            }
        }
        
        TimerMultiplexer::Stats TimerMultiplexer::getStats() const
        {
            Stats stats;
            stats.totalClients = m_clients.size();
            stats.totalFrames = m_frameCount;
            
            // Count how many callbacks would fire this frame
            for (const auto& [clientId, client] : m_clients)
            {
                if (client.frameCounter + 1 >= client.frameInterval)
                {
                    stats.callbacksThisFrame++;
                }
            }
            
            return stats;
        }
        
        std::uint32_t TimerMultiplexer::calculateFrameInterval(float desiredHz) const
        {
            // Calculate how many 60Hz frames between callbacks
            // For example: 30Hz = every 2 frames, 20Hz = every 3 frames
            const float interval = BASE_FREQUENCY_HZ / desiredHz;
            
            // Round to nearest integer frame count
            // This gives us the closest approximation to the desired frequency
            const auto frameInterval = static_cast<std::uint32_t>(std::round(interval));
            
            // Ensure at least 1 frame interval
            return std::max(1u, frameInterval);
        }
    }
}