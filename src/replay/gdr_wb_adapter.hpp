// gdr_wb_adapter.hpp - glue between White Bot's existing replay classes and gdr::Replay.
//
// White Bot's `Macro` derives from the OLD gdr::Replay<Macro, input> template. That old template is renamed to
// `wbleg::Replay` (see docs, step 2); this header converts any such type to/from the new model WITHOUT touching
// the hundreds of call sites that use macro.inputs / macro.frameFixes directly. It only needs the member names that
// the old template has, so it also compiles against test doubles (no Geode / cocos2d required).
#pragma once
#include "gdr.hpp"

#include <cmath>
#include <limits>

namespace gdr::wb {

/// Old model -> new model. Pass the BASE view (`static_cast<const wbleg::Replay<Macro,input>&>(macro)`), because
/// Macro declares its own `seed` (uintptr_t) that hides the replay seed; `platformer` is a Macro member and is passed in.
template <class OldReplay>
Replay toReplay(const OldReplay& m, bool platformer = false) {
    Replay r;
    r.author = m.author;
    r.description = m.description;
    r.duration = m.duration;
    r.gameVersion = static_cast<std::int32_t>(std::llround(static_cast<double>(m.gameVersion) * 10000.0));
    r.gdr1FormatVersion = m.version;
    r.framerate = m.framerate;
    r.seed = m.seed;
    r.coins = m.coins;
    r.ldm = m.ldm;
    r.platformer = platformer;
    r.bot = {m.botInfo.name, m.botInfo.version, std::nullopt};
    r.level = {m.levelInfo.id, m.levelInfo.name};
    r.lastRecordedFrame = m.lastRecordedFrame > 0 ? static_cast<std::uint32_t>(m.lastRecordedFrame) : 0u;
    r.inputs.reserve(m.inputs.size());
    for (const auto& in : m.inputs) {
        Input x;
        x.frame = static_cast<std::uint64_t>(in.frame);
        x.button = static_cast<std::uint8_t>(in.button);
        x.player2 = in.player2;
        x.down = in.down;
        r.inputs.push_back(x);
    }
    r.frameFixes.reserve(m.frameFixes.size());
    for (const auto& f : m.frameFixes) {
        FrameFix ff;
        ff.frame = static_cast<std::uint64_t>(f.frame);
        ff.p1 = {{f.p1.pos.x, f.p1.pos.y}, f.p1.rotation, f.p1.rotate};
        ff.p2 = {{f.p2.pos.x, f.p2.pos.y}, f.p2.rotation, f.p2.rotate};
        r.frameFixes.push_back(ff);
    }
    return r;
}

/// New model -> old model. Overwrites replay data; leaves Macro-only members (canChangeFPS, DllBotMacro, the
/// uintptr_t seed...) to the caller. Returns false when a frame does not fit the old model's `int`.
template <class OldReplay>
bool fromReplay(OldReplay& m, const Replay& r, bool* platformerOut = nullptr) {
    constexpr std::uint64_t kMax = std::numeric_limits<int>::max();
    m.author = r.author;
    m.description = r.description;
    m.duration = r.duration;
    m.gameVersion = static_cast<float>(r.gameVersion) / 10000.f;
    m.version = static_cast<float>(r.gdr1FormatVersion);
    m.framerate = static_cast<float>(r.framerate);
    m.seed = r.seed;
    m.coins = r.coins;
    m.ldm = r.ldm;
    m.botInfo.name = r.bot.name;
    m.botInfo.version = r.bot.version;
    m.levelInfo.id = r.level.id;
    m.levelInfo.name = r.level.name;
    m.lastRecordedFrame = static_cast<int>(r.lastRecordedFrame);
    if (platformerOut) *platformerOut = r.platformer;
    m.inputs.clear();
    m.inputs.reserve(r.inputs.size());
    for (const Input& x : r.inputs) {
        if (x.frame > kMax) return false;
        m.inputs.emplace_back(static_cast<int>(x.frame), static_cast<int>(x.button), x.player2, x.down);
    }
    m.frameFixes.clear();
    m.frameFixes.reserve(r.frameFixes.size());
    for (const FrameFix& f : r.frameFixes) {
        if (f.frame > kMax) return false;
        typename std::decay_t<decltype(m.frameFixes)>::value_type ff{};
        ff.frame = static_cast<int>(f.frame);
        ff.p1.pos.x = f.p1.pos.x; ff.p1.pos.y = f.p1.pos.y; ff.p1.rotation = f.p1.rotation; ff.p1.rotate = f.p1.rotate;
        ff.p2.pos.x = f.p2.pos.x; ff.p2.pos.y = f.p2.pos.y; ff.p2.rotation = f.p2.rotation; ff.p2.rotate = f.p2.rotate;
        m.frameFixes.push_back(ff);
    }
    return true;
}

}  // namespace gdr::wb
