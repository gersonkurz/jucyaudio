#pragma once

#include <string>

#include <Database/Includes/Constants.h>
#include <Database/Includes/IRefCounted.h>

namespace jucyaudio
{
    namespace database
    {
        struct IBackgroundTask : public RefCountImpl
        {
            explicit IBackgroundTask(std::string taskName)
                : m_taskName{std::move(taskName)}
            {
            }
            virtual void processWork() = 0;

            const std::string m_taskName;
        };
    } // namespace database
} // namespace jucyaudio
