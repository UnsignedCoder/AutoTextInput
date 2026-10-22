#pragma once

/**
 * @file SoundSynth.h
 * @brief Generates the app's audio cues as raw PCM samples, plus a helper to wrap them in a WAV file.
 *
 * This is the platform-independent half of the sound system: it only does math and byte layout. The
 * platform files under `platform/` take these samples and hand them to the OS for playback (see Sounds.h).
 *
 * Audio terms used here:
 * - **Sample**: one measurement of the sound wave's height at an instant. Here it's a signed 16-bit
 *   integer (-32768..32767).
 * - **Sample rate**: samples per second. At 44,100 Hz (CD quality), 0.1 s of sound is 4,410 samples.
 * - **PCM** (Pulse-Code Modulation): uncompressed audio, just the raw list of samples.
 * - **Mono**: a single channel. Stereo would interleave left and right samples.
 */

#include <cstdint>
#include <initializer_list>
#include <vector>

namespace SoundSynth {

/** @brief Samples per second for every generated sound. */
constexpr int SampleRate = 44'100;

/** @brief A single tone: a sine wave at a fixed pitch for a fixed time. */
struct Note {
    double frequencyHz;     ///< Pitch. 440 Hz is the A above middle C; doubling the frequency raises it an octave.
    double durationSeconds; ///< How long the note lasts.
};

/**
 * @brief Renders a sequence of notes, played one after another, into 16-bit mono PCM samples.
 * @param notes The notes, in playback order.
 * @return The samples at SampleRate. Each note fades in and out over a few milliseconds to avoid clicks.
 */
std::vector<std::int16_t> Synthesize(std::initializer_list<Note> notes);

/**
 * @brief Wraps PCM samples in a complete WAV file (RIFF header + "fmt " chunk + "data" chunk).
 * @param samples 16-bit mono samples at SampleRate.
 * @return The bytes of a .wav file, ready to be written to disk or played straight from memory.
 */
std::vector<char> EncodeWav(const std::vector<std::int16_t>& samples);

/** @return The "typing started" cue: a rising perfect fifth (E5 then B5). */
std::vector<std::int16_t> TypingStartedCue();

/** @return The "typing stopped" cue: the same two notes, falling. */
std::vector<std::int16_t> TypingStoppedCue();

} // namespace SoundSynth
