#pragma once

#include <string>

#include <Database/Includes/Constants.h>
#include <Database/Includes/INavigationNode.h>

namespace jucyaudio
{
    namespace database
    {
        struct IBackgroundTask : public database::IRefCounted
        {
            virtual void processWork() = 0;

            virtual std::string getName() const = 0;
        };
    } // namespace database
} // namespace jucyaudio
