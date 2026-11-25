#pragma once

// Single entry point the renderer uses to talk to ffmpeg libraries.
//
// Android, iOS and macOS (WB_FFMPEG_EMBEDDED, defined by CMake): ffmpeg is embedded into White Bot (recorder.cpp / audio_mixer.cpp are
// compiled into the mod and the static libs are linked in), so the classes are called directly.
// No external mod (eclipse.ffmpeg-api) is needed anywhere.
//
// Windows renders through the ffmpeg.exe set in the mod settings, so there is no library
// backend there (isAvailable() is false). A future libs-based Windows backend only has to
// provide the same Encoder / Mixer / isAvailable() trio.

#include <Geode/Geode.hpp>

#ifdef WB_FFMPEG_EMBEDDED

#include "audio_mixer.hpp"
#include "recorder.hpp"
#include "render_settings.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#endif

namespace wbffmpeg {

// True when an ffmpeg library backend can actually be used right now.
inline bool isAvailable() {
#ifdef WB_FFMPEG_EMBEDDED
    return true;
#else
    return false;
#endif
}

#ifdef WB_FFMPEG_EMBEDDED

// Video encoder: init() once, writeFrame() per frame, stop() to finalize the file.
class Encoder {
public:
    geode::Result<> init(::ffmpeg::RenderSettings const& settings) {
        return m_recorder.init(settings);
    }

    geode::Result<> writeFrame(std::span<uint8_t const> frame) {
        return m_recorder.writeFrame(frame);
    }

    void stop() {
        m_recorder.stop();
    }

    // Names of the video encoders that can be used (h264, hevc, vp9, ...).
    static std::vector<std::string> getAvailableCodecs() {
        return ::ffmpeg::Recorder::getAvailableCodecs();
    }

private:
    ::ffmpeg::Recorder m_recorder;
};

// Audio/video helpers that produce the final mp4.
class Mixer {
public:
    static geode::Result<> mixVideoAudio(
        std::filesystem::path const& videoFile,
        std::filesystem::path const& audioFile,
        std::filesystem::path const& outputMp4File
    ) {
        return ::ffmpeg::AudioMixer::mixVideoAudio(videoFile, audioFile, outputMp4File);
    }

    static geode::Result<> mixVideoRaw(
        std::filesystem::path const& videoFile,
        std::span<float> raw,
        std::filesystem::path const& outputMp4File
    ) {
        return ::ffmpeg::AudioMixer::mixVideoRaw(videoFile, raw, outputMp4File);
    }
};

#endif

} // namespace wbffmpeg
