#pragma once

/**
 * @file Sounds.h
 * @brief Short audio cues for typing state changes.
 *
 * The tones themselves are synthesized in memory by SoundSynth (so the app ships no sound files). Playing
 * them is platform-specific:
 * - `platform/windows/Sounds_win.cpp`: the Win32 `PlaySound` API, with the tone wrapped as an in-memory WAV file.
 * - `platform/linux/Sounds_linux.cpp`: Qt Multimedia's QAudioSink, fed raw PCM samples. If Qt Multimedia
 *   wasn't available at build time, these functions do nothing.
 *
 * Both functions return immediately, because playback is asynchronous. Starting one cue cuts off any cue
 * still playing. They must be called from the GUI thread.
 */

namespace Sounds {

/** @brief Plays a rising two-note chirp, meaning the first keystroke is about to be sent. */
void PlayTypingStarted();

/** @brief Plays a falling two-note chirp, meaning a typing run has ended (finished, stopped or failed). */
void PlayTypingStopped();

} // namespace Sounds
