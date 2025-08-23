#include "EQSettings.h"
#include <juce_data_structures/juce_data_structures.h>

namespace jucyaudio
{
    namespace audio
    {
        namespace model
        {
            juce::String EQSettings::toJson() const
            {
                juce::DynamicObject::Ptr json = new juce::DynamicObject{};

                // Add master settings
                json->setProperty("isActive", isActive);
                json->setProperty("preampGain", preampGain);

                // Add bands array
                juce::Array<juce::var> bandsArray;
                for (const auto &band : bands)
                {
                    juce::DynamicObject::Ptr bandObj = new juce::DynamicObject{};
                    bandObj->setProperty("frequency", band.frequency);
                    bandObj->setProperty("gain", band.gainInDecibels);
                    bandObj->setProperty("q", band.quality);
                    bandObj->setProperty("active", band.isActive);
                    bandsArray.add(juce::var(bandObj.get()));
                }
                json->setProperty("bands", bandsArray);

                return juce::JSON::toString(juce::var(json.get()), true);
            }

            EQSettings EQSettings::fromJson(const juce::String &jsonString)
            {
                EQSettings settings;

                auto json = juce::JSON::parse(jsonString);
                if (json.isObject())
                {
                    settings.isActive = json.getProperty("isActive", true);
                    settings.preampGain = static_cast<float>(json.getProperty("preampGain", 0.0));

                    auto bandsArray = json.getProperty("bands", juce::var());
                    if (bandsArray.isArray())
                    {
                        auto *array = bandsArray.getArray();
                        if (array != nullptr)
                        {
                            for (size_t i = 0; i < std::min(static_cast<size_t>(array->size()), kBandCount); ++i)
                            {
                                const auto &bandObj = array->getReference(static_cast<int>(i));
                                if (bandObj.isObject())
                                {
                                    settings.bands[i].frequency = static_cast<float>(bandObj.getProperty("frequency", kDefaultFrequencies[i]));
                                    settings.bands[i].gainInDecibels = static_cast<float>(bandObj.getProperty("gain", 0.0));
                                    settings.bands[i].quality = static_cast<float>(bandObj.getProperty("q", 0.707));
                                    settings.bands[i].isActive = bandObj.getProperty("active", true);
                                }
                            }
                        }
                    }
                }

                return settings;
            }

        } // namespace model
    } // namespace audio
} // namespace jucyaudi
