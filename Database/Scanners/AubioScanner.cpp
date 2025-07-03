#include <Database/Scanners/AubioScanner.h>
#include <juce_audio_formats/juce_audio_formats.h> // For AudioFormatManager, AudioFormatReader, AudioFormatWriter
#include <Utils/UiUtils.h>
#include <Utils/AssortedUtils.h>
#include <spdlog/spdlog.h>
#include <vector> // For storing beat times

// Make sure aubio.h does not clash with other system headers like time.h for timespec
// If it does, specific guards or include order might be needed in this CPP file.

namespace jucyaudio
{
    namespace database
    {
        namespace scanners
        {
            bool AubioScanner::processTrack(TrackInfo &trackInfo)
            {
                juce::File audioFile{ui::jucePathFromFs(trackInfo.filepath)};

                // Use JUCE to read the audio file
                juce::AudioFormatManager formatManager;
                formatManager.registerBasicFormats();

                std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(audioFile));
                if (!reader)
                {
                    spdlog::error("JUCE: Could not create reader for {}", pathToString(trackInfo.filepath));
                    return false;
                }

                // Get audio properties
                uint_t samplerate = static_cast<uint_t>(reader->sampleRate);
                uint_t num_channels = static_cast<uint_t>(reader->numChannels);
                uint_t hop_size = m_defaultHopSize;

                trackInfo.samplerate = samplerate;
                trackInfo.duration = static_cast<Duration_t>(reader->lengthInSamples * 1000 / static_cast<int64_t>(reader->sampleRate));

                // Create aubio objects (without source)
                uint_t win_size = m_defaultWinSize;
                aubio_tempo_t *tempo = new_aubio_tempo("default", win_size, hop_size, samplerate);

                if (!tempo)
                {
                    spdlog::error("Aubio: Could not create tempo object");
                    return false;
                }

                fvec_t *input_buffer = new_fvec(hop_size);
                fvec_t *tempo_out = new_fvec(1);

                // Read audio in chunks and process
                juce::AudioBuffer<float> buffer(num_channels, hop_size);
                std::vector<double> beat_times_seconds;
                uint_t total_frames_processed = 0;

                while (total_frames_processed < reader->lengthInSamples)
                {
                    int frames_to_read = std::min(hop_size, static_cast<uint_t>(reader->lengthInSamples - total_frames_processed));

                    reader->read(&buffer, 0, frames_to_read, total_frames_processed, true, true);

                    // Convert to mono if needed and copy to aubio buffer
                    for (int i = 0; i < frames_to_read; ++i)
                    {
                        float sample = 0.0f;
                        for (int ch = 0; ch < static_cast<int>(num_channels); ++ch)
                        {
                            sample += buffer.getSample(ch, i);
                        }
                        input_buffer->data[i] = sample / num_channels; // Average channels for mono
                    }

                    // Fill remaining buffer with zeros if needed
                    for (uint_t i = frames_to_read; i < hop_size; ++i)
                    {
                        input_buffer->data[i] = 0.0f;
                    }

                    aubio_tempo_do(tempo, input_buffer, tempo_out);

                    if (tempo_out->data[0] != 0.f)
                    {
                        beat_times_seconds.push_back(aubio_tempo_get_last_s(tempo));
                    }

                    total_frames_processed += frames_to_read;
                }

                trackInfo.bpm = static_cast<BPM_t>(aubio_tempo_get_bpm(tempo) * BPM_NORMALIZATION);

                // Convert beat times to JSON
                if (!beat_times_seconds.empty())
                {
                    trackInfo.beat_locations_json = "{ \"beats\": [";
                    for (size_t i = 0; i < beat_times_seconds.size(); ++i)
                    {
                        trackInfo.beat_locations_json += std::to_string(beat_times_seconds[i]);
                        if (i < beat_times_seconds.size() - 1)
                            trackInfo.beat_locations_json += ",";
                    }
                    trackInfo.beat_locations_json += "] }";
                }

                // Cleanup
                del_aubio_tempo(tempo);
                del_fvec(input_buffer);
                del_fvec(tempo_out);
                return true;
            }
            
        } // namespace scanners
    } // namespace database
} // namespace jucyaudio
