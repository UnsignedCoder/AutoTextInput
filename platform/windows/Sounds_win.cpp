/**
 * @file Sounds_win.cpp
 * @brief Windows implementation of Sounds.h, using the Win32 `PlaySound` API from winmm.
 *
 * PlaySound can play a WAV file straight from memory (`SND_MEMORY`), so each cue is synthesized once,
 * wrapped in a WAV header, and kept in a function-local static buffer for the rest of the run. That avoids
 * both temporary files and a dependency on Qt Multimedia (which would add many DLLs to deploy).
 */

#include "Sounds.h"

#include "SoundSynth.h"

#include <vector>
#include <Windows.h>
#include <mmsystem.h>  // PlaySoundW. WIN32_LEAN_AND_MEAN leaves it out of <Windows.h>, so it's included explicitly.

namespace {

/**
 * @brief Starts asynchronous playback of an in-memory WAV file.
 * @param wave A complete WAV file. It must stay alive until playback finishes, because with `SND_ASYNC`
 *             PlaySound keeps reading the buffer after returning. Callers pass function-local statics,
 *             which live until the program exits.
 */
void Play(const std::vector<char>& wave) {
    // SND_MEMORY: the first argument points at WAV bytes rather than a file name (hence the cast).
    // SND_ASYNC: return immediately, and stop any sound this process is already playing.
    // SND_NODEFAULT: if playback fails, stay silent instead of playing the Windows "ding".
    PlaySoundW(reinterpret_cast<LPCWSTR>(wave.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

} // namespace

namespace Sounds {

void PlayTypingStarted() {
    // Function-local statics are built on first call (thread-safe since C++11) and live until the program exits.
    static const std::vector<char> wave = SoundSynth::EncodeWav(SoundSynth::TypingStartedCue());
    Play(wave);
}

void PlayTypingStopped() {
    static const std::vector<char> wave = SoundSynth::EncodeWav(SoundSynth::TypingStoppedCue());
    Play(wave);
}

} // namespace Sounds
