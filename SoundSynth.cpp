/**
 * @file SoundSynth.cpp
 * @brief Sine-wave synthesis and WAV encoding for the audio cues. See SoundSynth.h for the audio terminology.
 */

#include "SoundSynth.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

/// Peak loudness as a fraction of the maximum 16-bit amplitude. 0.3 is clearly audible without being jarring.
constexpr double Volume = 0.3;

/// Length of the fade at both ends of every note. Starting or stopping a wave abruptly mid-cycle makes an
/// audible "click", because the speaker cone has to jump instantly. A few milliseconds of ramp prevents that.
constexpr double FadeSeconds = 0.008;

/**
 * @brief Appends the raw little-endian bytes of an integer to a byte buffer.
 * @details WAV headers store numbers as little-endian binary. x86 and ARM (in the modes used by Windows and
 *          Linux) are little-endian too, so copying the in-memory bytes gives the right layout.
 */
template <typename T>
void AppendBytes(std::vector<char>& buffer, const T value) {
    const auto* bytes = reinterpret_cast<const char*>(&value);
    buffer.insert(buffer.end(), bytes, bytes + sizeof(T));
}

} // namespace

namespace SoundSynth {

std::vector<std::int16_t> Synthesize(const std::initializer_list<Note> notes) {
    std::vector<std::int16_t> samples;
    const int fadeSamples = static_cast<int>(FadeSeconds * SampleRate);

    for (const Note& note : notes) {
        const int noteSamples = static_cast<int>(note.durationSeconds * SampleRate);
        for (int i = 0; i < noteSamples; ++i) {
            // Envelope: ramps 0 -> 1 over the first fadeSamples, holds at 1, then ramps 1 -> 0 over the last fadeSamples.
            const double envelope = std::min({1.0,
                                              static_cast<double>(i) / fadeSamples,
                                              static_cast<double>(noteSamples - i) / fadeSamples});
            // A sine wave: the time of sample i is i / SampleRate seconds, and the wave completes frequencyHz
            // full cycles (2*pi radians each) per second.
            const double wave = std::sin(2.0 * std::numbers::pi * note.frequencyHz * i / SampleRate);
            samples.push_back(static_cast<std::int16_t>(wave * envelope * Volume * INT16_MAX));
        }
    }
    return samples;
}

std::vector<char> EncodeWav(const std::vector<std::int16_t>& samples) {
    constexpr std::uint16_t Channels = 1;
    constexpr std::uint16_t BitsPerSample = 16;
    constexpr std::uint16_t BlockAlign = Channels * BitsPerSample / 8;  // Bytes per sample frame (all channels).
    constexpr std::uint32_t ByteRate = SampleRate * BlockAlign;         // Bytes per second of audio.
    constexpr std::uint32_t HeaderBytes = 44;

    const auto dataBytes = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
    std::vector<char> wave;
    wave.reserve(HeaderBytes + dataBytes);

    // RIFF container header. The size field counts everything after itself: the rest of the header (36 bytes) + data.
    wave.insert(wave.end(), {'R', 'I', 'F', 'F'});
    AppendBytes<std::uint32_t>(wave, HeaderBytes - 8 + dataBytes);
    wave.insert(wave.end(), {'W', 'A', 'V', 'E'});

    // "fmt " chunk: describes how to interpret the sample data.
    wave.insert(wave.end(), {'f', 'm', 't', ' '});
    AppendBytes<std::uint32_t>(wave, 16);           // Size of this chunk's body.
    AppendBytes<std::uint16_t>(wave, 1);            // Audio format 1 = uncompressed PCM.
    AppendBytes<std::uint16_t>(wave, Channels);
    AppendBytes<std::uint32_t>(wave, SampleRate);
    AppendBytes<std::uint32_t>(wave, ByteRate);
    AppendBytes<std::uint16_t>(wave, BlockAlign);
    AppendBytes<std::uint16_t>(wave, BitsPerSample);

    // "data" chunk: the samples themselves.
    wave.insert(wave.end(), {'d', 'a', 't', 'a'});
    AppendBytes<std::uint32_t>(wave, dataBytes);
    const auto* sampleBytes = reinterpret_cast<const char*>(samples.data());
    wave.insert(wave.end(), sampleBytes, sampleBytes + dataBytes);

    return wave;
}

std::vector<std::int16_t> TypingStartedCue() {
    // Rising pitch reads as "on/begin". 660 Hz -> 990 Hz is a 3:2 ratio (a perfect fifth), which sounds pleasant.
    return Synthesize({{660.0, 0.07}, {990.0, 0.10}});
}

std::vector<std::int16_t> TypingStoppedCue() {
    // The mirror image: falling pitch reads as "off/end".
    return Synthesize({{990.0, 0.07}, {660.0, 0.10}});
}

} // namespace SoundSynth
