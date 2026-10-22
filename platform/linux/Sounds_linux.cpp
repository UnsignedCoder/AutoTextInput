/**
 * @file Sounds_linux.cpp
 * @brief Linux implementation of Sounds.h, using Qt Multimedia's QAudioSink.
 *
 * Linux has no single system call like Windows' PlaySound: audio goes through PulseAudio, PipeWire or ALSA,
 * depending on the distribution. Qt Multimedia hides those differences. A QAudioSink takes raw PCM samples in
 * a declared format (QAudioFormat) and streams them to the default output device.
 *
 * Qt Multimedia is optional at build time (CMake defines AUTOTEXTINPUT_HAVE_QT_MULTIMEDIA when it finds the
 * module). Without it, both functions compile to no-ops and the app still works, silently.
 */

#include "Sounds.h"

#ifdef AUTOTEXTINPUT_HAVE_QT_MULTIMEDIA

#include "SoundSynth.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QBuffer>
#include <QCoreApplication>
#include <QMediaDevices>
#include <QPointer>

namespace {

/// The sink currently playing, if any. QPointer clears itself automatically when the sink is deleted,
/// so it never dangles.
QPointer<QAudioSink> activeSink;

/**
 * @brief Streams 16-bit mono samples to the default audio output, replacing any cue still playing.
 * @details Ownership: the sink is parented to the application object, and the QBuffer holding the samples is
 *          parented to the sink, so deleting the sink frees both. Each sink deletes itself when its data runs
 *          out (IdleState) or when the next cue replaces it.
 */
void Play(const std::vector<std::int16_t>& samples) {
    if (activeSink) {
        activeSink->stop();
        activeSink->deleteLater();  // Deferred delete: safe even when called from one of the sink's own signals.
    }

    // Describe the data: 44.1 kHz, one channel, signed 16-bit integers. This must match what SoundSynth produced.
    QAudioFormat format;
    format.setSampleRate(SoundSynth::SampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull() || !device.isFormatSupported(format)) {
        return;  // No sound card, or it can't play this format. Stay silent rather than fail.
    }

    auto* sink = new QAudioSink(device, format, QCoreApplication::instance());
    auto* buffer = new QBuffer(sink);
    buffer->setData(reinterpret_cast<const char*>(samples.data()),
                    static_cast<qsizetype>(samples.size() * sizeof(std::int16_t)));
    buffer->open(QIODevice::ReadOnly);

    // IdleState means the sink has played everything in the buffer.
    QObject::connect(sink, &QAudioSink::stateChanged, sink, [sink](const QAudio::State state) {
        if (state == QAudio::IdleState) {
            sink->stop();
            sink->deleteLater();
        }
    });

    sink->start(buffer);  // "Pull mode": the sink reads from the buffer as the audio device needs data.
    activeSink = sink;
}

} // namespace

namespace Sounds {

void PlayTypingStarted() {
    // Synthesized once, on first use, then reused.
    static const std::vector<std::int16_t> samples = SoundSynth::TypingStartedCue();
    Play(samples);
}

void PlayTypingStopped() {
    static const std::vector<std::int16_t> samples = SoundSynth::TypingStoppedCue();
    Play(samples);
}

} // namespace Sounds

#else // !AUTOTEXTINPUT_HAVE_QT_MULTIMEDIA

namespace Sounds {

// Built without Qt Multimedia: audio cues are unavailable, so these do nothing.
void PlayTypingStarted() {}
void PlayTypingStopped() {}

} // namespace Sounds

#endif
