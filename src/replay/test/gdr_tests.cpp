// gdr_tests.cpp - self-contained tests (no framework). Build: see CMakeLists.txt / README.
#include "gdr.hpp"
#include <cstdio>
#include <cstring>
#include <functional>
#include <random>

using namespace gdr;
using Bytes = std::vector<std::uint8_t>;

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_ERR(res, ec) do { ++g_checks; if ((res).ok() || (res).error().code != (ec)) { ++g_fail; \
  std::printf("  FAIL %s:%d  expected %s, got %s\n", __FILE__, __LINE__, std::string(errorCodeName(ec)).c_str(), \
  (res).ok() ? "OK" : (res).error().toString().c_str()); } } while (0)

static bool bitsEq(float a, float b) { std::uint32_t x, y; std::memcpy(&x, &a, 4); std::memcpy(&y, &b, 4); return x == y; }

static bool sameReplay(const Replay& a, const Replay& b, bool checkFixes = true) {
    struct W { bool ok = false; ~W() {} };
    if (a.author != b.author || a.description != b.description || a.bot.name != b.bot.name || a.bot.version != b.bot.version ||
        a.level.id != b.level.id || a.level.name != b.level.name || a.gameVersion != b.gameVersion ||
        !bitsEq(a.duration, b.duration) || a.framerate != b.framerate || a.seed != b.seed || a.coins != b.coins ||
        a.ldm != b.ldm || a.platformer != b.platformer || a.lastRecordedFrame != b.lastRecordedFrame || a.deaths != b.deaths) {
        std::printf("   meta diff: ver '%s' vs '%s' gv %d/%d fr %g/%g seed %d/%d lrf %u/%u deaths %zu/%zu dur %g/%g\n", a.bot.version.c_str(), b.bot.version.c_str(), a.gameVersion, b.gameVersion, a.framerate, b.framerate, a.seed, b.seed, a.lastRecordedFrame, b.lastRecordedFrame, a.deaths.size(), b.deaths.size(), a.duration, b.duration);
        return false; }
    if (a.inputs.size() != b.inputs.size()) return false;
    for (size_t i = 0; i < a.inputs.size(); ++i) {
        const Input &x = a.inputs[i], &y = b.inputs[i];
        if (x.frame != y.frame || x.button != y.button || x.player2 != y.player2 || x.down != y.down) { std::printf("   input %zu diff\n", i); return false; }
        auto ex = a.inputExtension(x), ey = b.inputExtension(y);
        if (ex.size() != ey.size() || (ex.size() && std::memcmp(ex.data(), ey.data(), ex.size()))) return false;
    }
    if (checkFixes) {
        if (a.frameFixes.size() != b.frameFixes.size()) return false;
        for (size_t i = 0; i < a.frameFixes.size(); ++i) {
            const auto &x = a.frameFixes[i], &y = b.frameFixes[i];
            if (x.frame != y.frame || !bitsEq(x.p1.pos.x, y.p1.pos.x) || !bitsEq(x.p1.pos.y, y.p1.pos.y) ||
                !bitsEq(x.p1.rotation, y.p1.rotation) || !bitsEq(x.p2.pos.x, y.p2.pos.x) || !bitsEq(x.p2.pos.y, y.p2.pos.y) ||
                !bitsEq(x.p2.rotation, y.p2.rotation) || x.p1.rotate != y.p1.rotate || x.p2.rotate != y.p2.rotate)
                { std::printf("   framefix %zu diff\n", i); return false; }
        }
    }
    if (a.replayExt.kind != b.replayExt.kind || a.replayExt.data != b.replayExt.data) { std::printf("   replayExt diff kind %d/%d size %zu/%zu\n", (int)a.replayExt.kind, (int)b.replayExt.kind, a.replayExt.data.size(), b.replayExt.data.size()); return false; }
    if (a.inputExt.kind != b.inputExt.kind) { std::printf("   inputExt kind diff %d/%d\n", (int)a.inputExt.kind, (int)b.inputExt.kind); return false; }
    return true;
}

// deterministic synthetic replay shaped like a real recording: alternating press/release, smooth position
static Replay makeReplay(size_t nInputs, size_t nFixes, bool p2 = true, bool platformer = false, unsigned seed = 1) {
    std::mt19937 rng(seed);
    Replay r;
    r.author = "tester \xC3\xA9"; r.description = "synthetic"; r.bot = {"White Bot", "v3.1.0-Neon", std::nullopt};
    r.level = {128, "Stereo Madness"}; r.gameVersion = 22081; r.duration = 12.5f; r.framerate = 240.0;
    r.seed = -123456; r.coins = 2; r.ldm = true; r.platformer = platformer;
    std::uint64_t f1 = 0, f2 = 0;
    for (size_t i = 0; i < nInputs; ++i) {
        Input x; const bool two = p2 && (i % 3 == 2);
        std::uint64_t& f = two ? f2 : f1; f += 1 + rng() % 40;
        x.frame = f; x.player2 = two; x.down = (i % 2) == 0;
        x.button = platformer ? (1 + rng() % 3) : 1;
        r.inputs.push_back(x);
    }
    r.sortInputs();
    // make "down/up" alternate per player is not required by the format
    float px = 0, py = 100, rot = 0;
    for (size_t i = 0; i < nFixes; ++i) {
        px += 1.2983f * (1 + rng() % 3); py += (int(rng() % 9) - 4) * 0.37f; rot += 2.5f;
        FrameFix ff; ff.frame = (i + 1) * 7; ff.p1.pos = {px, py}; ff.p1.rotation = rot;
        if (p2) { ff.p2.pos = {px + 3.f, py - 1.5f}; ff.p2.rotation = -rot; }
        r.frameFixes.push_back(ff);
    }
    r.lastRecordedFrame = r.inputs.empty() ? 0 : (std::uint32_t)r.inputs.back().frame;
    return r;
}

static ExportOptions to(Format f, LossPolicy lp = LossPolicy::Error) { ExportOptions o; o.format = f; o.lossPolicy = lp; return o; }
static Bytes enc(const Replay& r, Format f) { auto b = exportReplay(r, to(f)); if (!b) { std::printf("  export error: %s\n", b.error().toString().c_str()); return {}; } return b.value(); }
static Bytes bytesOf(const std::string& s) { return Bytes(s.begin(), s.end()); }

struct T { const char* name; std::function<void()> fn; };
static std::vector<T>& tests() { static std::vector<T> t; return t; }
struct Reg { Reg(const char* n, std::function<void()> f) { tests().push_back({n, std::move(f)}); } };
#define TEST(n) static void n(); static Reg reg_##n(#n, n); static void n()

// ---- 1,3,4 GDR1 --------------------------------------------------------------------------------------
TEST(t01_gdr1_msgpack_roundtrip) {
    Replay r = makeReplay(500, 100); r.bot = {"White Bot", "v3.1.0-Neon", std::nullopt};
    for (Format f : {Format::Gdr1MsgPack, Format::Gdr1Json}) {
        Bytes b = enc(r, f); CHECK(!b.empty());
        auto back = importReplay(b); CHECK(back.ok());
        if (!back) continue;
        CHECK(back.value().source.format == f);
        // White Bot macros lose frame-fix rotation on import by the legacy rule; compare with PreserveAll
        ImportOptions io; io.rotation = RotationPolicy::PreserveAll;
        auto full = importReplay(b, io); CHECK(full.ok());
        CHECK(sameReplay(r, full.value()));
        Report rep; ImportOptions io2; io2.report = &rep; auto leg = importReplay(b, io2);
        CHECK(rep.has("gdr1.rotation-dropped"));
        CHECK(leg.ok() && leg.value().frameFixes[5].p1.rotation == 0.f);
    }
}
// ---- 2 GDR2 --------------------------------------------------------------------------------------------
TEST(t02_gdr2_roundtrip) {
    for (bool plat : {false, true}) {
        Replay r = makeReplay(800, 0, true, plat); r.bot = {"xdBot", "7", 7}; r.lastRecordedFrame = 0;
        r.deaths = {100, 250, 251};
        Bytes b = enc(r, Format::Gdr2); CHECK(!b.empty());
        CHECK(std::memcmp(b.data(), "GDR\x02", 4) == 0);
        auto back = importReplay(b); CHECK(back.ok());
        if (back) { CHECK(back.value().source.format == Format::Gdr2); CHECK(sameReplay(r, back.value())); }
        Bytes b2 = enc(back.value(), Format::Gdr2); CHECK(b == b2);   // byte-stable
    }
}
TEST(t02b_gdr2_input_extension_and_opaque_replay_ext) {
    Replay r = makeReplay(50, 0); r.bot = {"xdBot", "7", 7}; r.lastRecordedFrame = 0;
    r.replayExt = {ExtensionKind::GdrBinary, "", {1, 2, 3, 4, 5}};
    for (auto& in : r.inputs) { std::uint8_t e[3] = {(std::uint8_t)in.frame, 9, 8}; r.setInputExtension(in, ExtensionKind::GdrBinary, "Phys", e); }
    Bytes b = enc(r, Format::Gdr2); auto back = importReplay(b); CHECK(back.ok());
    if (back) { CHECK(sameReplay(r, back.value())); CHECK(back.value().inputExt.tag == "Phys"); CHECK(back.value().replayExt.data.size() == 5); }
}
// ---- 5,6,7 WBR + conversions --------------------------------------------------------------------------
TEST(t05_wbr_roundtrip) {
    for (int mode = 0; mode < 3; ++mode) {
        Replay r = makeReplay(2000, 700, mode != 0, mode == 2);
        r.bot.versionNumber = 3001000; r.deaths = {5, 9000};
        r.frameFixes[3].p1.rotate = false; r.frameFixes[4].p2.rotate = false;
        r.frameFixes[7].p1.pos.x = -0.0f; r.frameFixes[8].p1.rotation = std::numeric_limits<float>::quiet_NaN();
        Bytes b = enc(r, Format::Wbr); CHECK(!b.empty());
        CHECK(std::memcmp(b.data(), "WBR\x1A", 4) == 0);
        auto back = importReplay(b); CHECK(back.ok());
        if (back) { CHECK(sameReplay(r, back.value())); CHECK(back.value().bot.versionNumber == r.bot.versionNumber); CHECK(back.value().source.formatVersion == "1.0"); }
        CHECK(enc(back.value(), Format::Wbr) == b);
    }
}
TEST(t05b_wbr_unsorted_wide_buttons_extensions) {
    Replay r = makeReplay(20, 5);
    std::swap(r.inputs[3], r.inputs[9]); r.inputs[4].button = 200; r.frameFixes[2].frame = 1; // unsorted fixes
    r.replayExt = {ExtensionKind::JsonObject, "", {0x80}};
    for (auto& in : r.inputs) if (in.frame % 2) { std::uint8_t e[2] = {1, 2}; r.setInputExtension(in, ExtensionKind::GdrBinary, "Tag", e); }
    Bytes b = enc(r, Format::Wbr); auto back = importReplay(b); CHECK(back.ok());
    if (back) CHECK(sameReplay(r, back.value()));
}
TEST(t06_convert_gdr2_to_wbr_and_back) {
    Replay r = makeReplay(1000, 0, true, true); r.bot = {"xdBot", "12", 12}; r.lastRecordedFrame = 0;
    Bytes g2 = enc(r, Format::Gdr2);
    auto w = convertReplay(g2, to(Format::Wbr)); CHECK(w.ok());
    auto back = convertReplay(w.value(), to(Format::Gdr2)); CHECK(back.ok());
    CHECK(back.ok() && back.value() == g2);
    CHECK(w.value().size() < g2.size() + 64);
}
TEST(t07_metadata_and_inputs_preserved_all_pairs) {
    Replay r = makeReplay(300, 60, true, false); r.bot = {"White Bot", "v3.1.0-Neon", std::nullopt}; r.deaths = {10, 20};
    r.platformer = false;
    ImportOptions pa; pa.rotation = RotationPolicy::PreserveAll;
    const Format fs[] = {Format::Gdr1MsgPack, Format::Gdr1Json, Format::Gdr2, Format::Wbr};
    for (Format a : fs) for (Format b : fs) {
        auto x = exportReplay(r, to(a)); CHECK(x.ok()); if (!x) continue;
        auto ra = importReplay(x.value(), pa); CHECK(ra.ok()); if (!ra) continue;
        auto y = exportReplay(ra.value(), to(b)); CHECK(y.ok());
        if (!y) { std::printf("   %s -> %s: %s\n", std::string(formatName(a)).c_str(), std::string(formatName(b)).c_str(), y.error().toString().c_str()); continue; }
        auto rb = importReplay(y.value(), pa); CHECK(rb.ok());
        if (rb) { CHECK(sameReplay(r, rb.value())); }
    }
}
// ---- 8 FrameFix + extension --------------------------------------------------------------------------------
TEST(t08_framefix_and_extension_survive) {
    Replay r = makeReplay(40, 30); ImportOptions pa; pa.rotation = RotationPolicy::PreserveAll;
    r.replayExt = {ExtensionKind::JsonObject, "", {0x81, 0xa3, 'f', 'o', 'o', 0x01}};   // {"foo":1}
    std::uint8_t e[] = {0x81, 0xa1, 'k', 0x02};
    r.setInputExtension(r.inputs[2], ExtensionKind::JsonObject, "", e);
    r.frameFixes[1].p1.rotate = false;
    for (Format f : {Format::Gdr1MsgPack, Format::Gdr1Json, Format::Gdr2, Format::Wbr}) {
        auto b = exportReplay(r, to(f)); CHECK(b.ok()); if (!b) continue;
        auto back = importReplay(b.value(), pa); CHECK(back.ok());
        if (back) { CHECK(sameReplay(r, back.value())); CHECK(back.value().frameFixes.size() == 30); }
    }
}
// ---- 9 DLL Bot offset ---------------------------------------------------------------------------------------
static Bytes dllJson(const char* ver) {
    return bytesOf(std::string(R"({"bot":{"name":"Dll Bot","version":")") + ver + R"("},"inputs":[{"frame":10,"btn":1,"2p":false,"down":true},{"frame":20,"btn":1,"2p":false,"down":false}],)"
        R"("frameFixes":[{"frame":10,"p1":{"x":1.5,"y":2.5,"r":30.0}}]})");
}
TEST(t09_dllbot_offset_and_rotation) {
    struct C { const char* v; int off; bool rot; } cases[] = {
        {"v2.3.5", 1, true}, {"2.3.5", 1, true}, {"v2.3.6", 0, true}, {"v2.4.0", 0, true}, {"v3.0.1", 0, true},
        {"v3.0.0-beta.2", 1, false},  /* legacy quirk: 4 dot-parts -> offset stays 1; "beta." -> no rotation */
        {"", 1, true}, {"v1.9", 1, true}};
    for (auto& c : cases) {
        auto r = importReplay(dllJson(c.v)); CHECK(r.ok()); if (!r) continue;
        CHECK(r.value().inputs[0].frame == (std::uint64_t)(10 + c.off));
        CHECK(r.value().frameFixes[0].frame == (std::uint64_t)(10 + c.off));
        CHECK((r.value().frameFixes[0].p1.rotation == 30.f) == c.rot);
        // export undoes the offset: GDR1 -> model -> GDR1 -> model is the identity
        auto j = exportReplay(r.value(), to(Format::Gdr1Json)); CHECK(j.ok());
        auto r2 = importReplay(j.value()); CHECK(r2.ok());
        if (r2) CHECK(r2.value().inputs[0].frame == r.value().inputs[0].frame);
    }
    ImportOptions off; off.applyLegacyDllBotFrameOffset = false;
    CHECK(importReplay(dllJson("v2.3.5"), off).value().inputs[0].frame == 10);
    // White Bot macros: rotation ignored by the legacy rule, kept with PreserveAll
    auto wb = bytesOf(R"({"bot":{"name":"White Bot","version":"v3"},"frameFixes":[{"frame":1,"p1":{"x":1,"y":2,"r":9}}]})");
    CHECK(importReplay(wb).value().frameFixes[0].p1.rotation == 0.f);
    ImportOptions pa; pa.rotation = RotationPolicy::PreserveAll; CHECK(importReplay(wb, pa).value().frameFixes[0].p1.rotation == 9.f);
    // legacy string and X/Y frame-fix encodings
    auto old1 = bytesOf(R"({"frameFixes":[{"frame":3,"player1":"3,10.5,20.5","player2":"3,1,2"},{"frame":4,"player1X":5,"player1Y":6}]})");
    auto o = importReplay(old1); CHECK(o.ok());
    if (o) { CHECK(o.value().frameFixes[0].p1.pos.x == 10.5f && o.value().frameFixes[0].p2.pos.y == 2.f && !o.value().frameFixes[0].p1.rotate); CHECK(o.value().frameFixes[1].p1.pos.y == 6.f); }
}
// ---- 10,11 hostile input --------------------------------------------------------------------------------
TEST(t10_empty_truncated_bad_header) {
    CHECK_ERR(importReplay(Bytes{}), ErrorCode::Empty);
    CHECK_ERR(importReplay(bytesOf("hello world")), ErrorCode::UnknownFormat);
    CHECK_ERR(importReplay(Bytes{0x00, 0x01, 0x02}), ErrorCode::UnknownFormat);
    ImportOptions o; o.format = Format::Wbr; CHECK_ERR(importReplay(bytesOf("GDRxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"), o), ErrorCode::UnknownFormat);
    Replay r = makeReplay(200, 50);
    for (Format f : {Format::Gdr1MsgPack, Format::Gdr1Json, Format::Gdr2, Format::Wbr}) {
        Bytes b = enc(r, f); ImportOptions io; io.format = f;
        size_t bad = 0;
        for (size_t n = 0; n < b.size(); ++n) {   // every strict prefix must fail cleanly
            auto res = importReplay(std::span<const std::uint8_t>(b.data(), n), io);
            if (res.ok()) ++bad;
        }
        // GDR2 may accept a prefix only if it is a valid shorter file: not possible because counts are declared
        CHECK(bad == 0);
    }
    Bytes w = enc(r, Format::Wbr); w[w.size() / 2] ^= 0x40; CHECK_ERR(importReplay(w), ErrorCode::ChecksumMismatch);
    Bytes t = enc(r, Format::Wbr); t.push_back(0); CHECK_ERR(importReplay(t), ErrorCode::Corrupt);
    Bytes g = enc(r, Format::Gdr2); g[3] = 3; CHECK_ERR(importReplay(g), ErrorCode::NewerVersion);
    g[3] = 1; CHECK_ERR(importReplay(g), ErrorCode::UnsupportedVersion);
    Bytes n = enc(r, Format::Wbr); n[4] = 2; CHECK_ERR(importReplay(n), ErrorCode::NewerVersion);
    Bytes fl = enc(r, Format::Wbr); fl[11] = 0x01; CHECK_ERR(importReplay(fl), ErrorCode::UnsupportedFeature);   // required-feature bit
    Bytes cp = enc(r, Format::Wbr); cp[6] = 7; CHECK_ERR(importReplay(cp), ErrorCode::UnsupportedFeature);
}
// build a WBR file around a hand-made payload (valid CRC) to hit the section parser
static Bytes wbrWrap(const Bytes& payload, std::uint8_t minor = 0) {
    Bytes h = {'W', 'B', 'R', 0x1A, 1, minor, 0, 0, 0, 0, 0, 0};
    auto le = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) h.push_back((std::uint8_t)(v >> (8 * i))); };
    le((std::uint32_t)payload.size()); le((std::uint32_t)payload.size());
    std::uint32_t c = 0xFFFFFFFF; for (auto b : payload) { c ^= b; for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1); }
    le(~c); h.insert(h.end(), payload.begin(), payload.end()); return h;
}
static Bytes metaSection() {   // minimal valid META section
    Bytes m = {0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xF0, 0x3F /*dummy*/};
    m = {0x00, 0,0,0,0, 0,0,0,0,0,0,0,0, 0, 0, 0, 0, 0, 0, 0, 0, 0};   // bits, f32, f64, 3 svarint, level id, 5 empty strings
    m = {0x00, 0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0, 0, 0,0,0,0,0};
    Bytes s = {0x01, (std::uint8_t)m.size()}; s.insert(s.end(), m.begin(), m.end()); return s;
}
TEST(t11_invalid_counts_and_sizes) {
    Bytes p = metaSection();
    CHECK(importReplay(wbrWrap(p)).ok());
    { Bytes q = p; q.insert(q.end(), {0x02, 0x04, 0xFF, 0xFF, 0x0F, 0x00}); CHECK_ERR(importReplay(wbrWrap(q)), ErrorCode::Corrupt); }       // huge input count
    { Bytes q = p; q.insert(q.end(), {0x02, 0x0A, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7F}); CHECK(!importReplay(wbrWrap(q)).ok()); } // varint overflow
    { Bytes q = p; q.insert(q.end(), {0x05, 0x00}); CHECK_ERR(importReplay(wbrWrap(q)), ErrorCode::UnsupportedFeature); }              // unknown critical
    { Bytes q = p; q.insert(q.end(), {0x85, 0x01, 0x00}); CHECK(importReplay(wbrWrap(q)).ok()); }                                          // unknown optional: skipped
    { Bytes q = p; q.insert(q.end(), p.begin(), p.end()); CHECK_ERR(importReplay(wbrWrap(q)), ErrorCode::Corrupt); }                    // duplicate META
    { Bytes q = p; q.insert(q.end(), {0x81, 0x05, 0x01, 0x00, 0xFF, 0xFF, 0x0F}); CHECK(!importReplay(wbrWrap(q)).ok()); }              // ext length past end
    { Bytes q = p; q.push_back(0x02); q.push_back(0x7F); CHECK(!importReplay(wbrWrap(q)).ok()); }                                         // section longer than data
    { Bytes q = p; q.insert(q.end(), {0x03, 0x03, 0x02, 0x00, 0x00}); CHECK(!importReplay(wbrWrap(q)).ok()); }                          // frame fixes: count > data
    { // limits: tiny maxInputs
      Replay r = makeReplay(100, 0); ImportOptions o; o.limits.maxInputs = 10;
      CHECK_ERR(importReplay(enc(r, Format::Wbr), o), ErrorCode::LimitExceeded);
      CHECK_ERR(importReplay(enc(r, Format::Gdr2), o), ErrorCode::LimitExceeded);
      CHECK_ERR(importReplay(enc(r, Format::Gdr1MsgPack), o), ErrorCode::LimitExceeded); }
    // GDR2: absurd extension size / input count / string length
    Replay r = makeReplay(3, 0); r.bot = {"xdBot", "1", 1}; r.lastRecordedFrame = 0;
    Bytes g = enc(r, Format::Gdr2);
    Bytes big = {'G', 'D', 'R', 2, 0, 0, 0}; big.insert(big.end(), {0xFF, 0xFF, 0xFF, 0xFF, 0x0F}); CHECK(!importReplay(big).ok());
    // GDR2 with declared input count far above data
    Bytes tail = enc(makeReplay(0, 0), Format::Gdr2); // ... inputCount=0,p1Count=0 are the last two bytes
    tail[tail.size() - 2] = 0xFF; tail.insert(tail.end() - 1, {0xFF, 0xFF, 0x7F}); CHECK(!importReplay(tail).ok());
    (void)g;
    // GDR1 hostile JSON
    CHECK_ERR(importReplay(bytesOf(R"({"inputs":[{"frame":-5,"btn":1,"2p":false,"down":true}]})")), ErrorCode::InvalidValue);
    CHECK_ERR(importReplay(bytesOf(R"({"inputs":[{"frame":5,"btn":999,"2p":false,"down":true}]})")), ErrorCode::InvalidValue);
    CHECK_ERR(importReplay(bytesOf(R"({"inputs":"nope"})")), ErrorCode::InvalidValue);
    CHECK_ERR(importReplay(bytesOf(R"({"inputs":[{"frame":5}]})")), ErrorCode::InvalidValue);
    CHECK_ERR(importReplay(bytesOf("{\"a\":")), ErrorCode::Corrupt);
    ImportOptions jd; jd.limits.maxJsonDepth = 8; CHECK_ERR(importReplay(bytesOf(std::string(100, '[') .insert(0, "{\"x\":") ), jd), ErrorCode::LimitExceeded);
}
// ---- 12 ordering / players ---------------------------------------------------------------------------------
TEST(t12_input_order_and_players) {
    Replay r; r.bot = {"xdBot", "1", 1};
    auto add = [&](std::uint64_t f, bool p2, bool down) { Input x; x.frame = f; x.player2 = p2; x.down = down; r.inputs.push_back(x); };
    add(5, false, true); add(5, true, true); add(9, true, false); add(9, false, false); add(9, false, true); add(30, true, true);
    for (Format f : {Format::Wbr, Format::Gdr1Json, Format::Gdr1MsgPack}) {   // single chronological stream keeps ties exactly
        auto b = exportReplay(r, to(f)); auto back = importReplay(b.value());
        CHECK(back.ok() && sameReplay(r, back.value()));
    }
    auto g = importReplay(enc(r, Format::Gdr2)); CHECK(g.ok());   // GDR2: p1 then p2 on ties
    if (g) {
        std::vector<std::pair<std::uint64_t, bool>> got; for (auto& i : g.value().inputs) got.push_back({i.frame, i.player2});
        CHECK((got == std::vector<std::pair<std::uint64_t, bool>>{{5, false}, {5, true}, {9, false}, {9, false}, {9, true}, {30, true}}));
    }
    Replay u = r; std::swap(u.inputs[0], u.inputs[5]);   // unsorted input -> GDR2 export sorts and reports
    Report rep; ExportOptions eo = to(Format::Gdr2); eo.report = &rep; CHECK(exportReplay(u, eo).ok()); CHECK(rep.has("gdr2.inputs-reordered"));
}
// ---- loss policy, detection, HLR, registry --------------------------------------------------------------
TEST(t13_loss_policy) {
    Replay r = makeReplay(10, 0); r.bot = {"xdBot", "1", 1}; r.lastRecordedFrame = 0; r.inputs[2].button = 3;
    CHECK_ERR(exportReplay(r, to(Format::Gdr2)), ErrorCode::LossyConversion);
    Report rep; ExportOptions o = to(Format::Gdr2, LossPolicy::ReportAndDrop); o.report = &rep;
    CHECK(exportReplay(r, o).ok()); CHECK(rep.has("gdr2.button-dropped"));
    Replay f = makeReplay(10, 5); f.bot = {"SomeOtherBot", "1", 1};   // frame fixes cannot be stored for a foreign bot
    CHECK_ERR(exportReplay(f, to(Format::Gdr2)), ErrorCode::LossyConversion);
    Replay b = makeReplay(3, 0); b.replayExt = {ExtensionKind::GdrBinary, "", {1}};
    CHECK_ERR(exportReplay(b, to(Format::Gdr1Json)), ErrorCode::LossyConversion);
    CHECK(exportReplay(b, to(Format::Wbr)).ok());   // WBR holds everything
}
TEST(t14_detection) {
    Replay r = makeReplay(20, 5);
    CHECK(detectFormat(enc(r, Format::Wbr)).format == Format::Wbr);
    CHECK(detectFormat(enc(r, Format::Gdr2)).format == Format::Gdr2);
    CHECK(detectFormat(enc(r, Format::Gdr2)).confidence == Detection::Confidence::Structure);
    CHECK(detectFormat(enc(r, Format::Gdr1Json)).format == Format::Gdr1Json);
    CHECK(detectFormat(enc(r, Format::Gdr1MsgPack)).format == Format::Gdr1MsgPack);
    CHECK(detectFormat(Bytes{}).format == Format::Unknown);
    CHECK(detectFormat(bytesOf("PK\x03\x04junk")).format == Format::Unknown);
    Bytes g = enc(r, Format::Gdr2); g[3] = 9; auto d = detectFormat(g); CHECK(d.confidence == Detection::Confidence::MagicOnly && d.note.find("unsupported version 9") != std::string::npos);
    Bytes jb = {0xEF, 0xBB, 0xBF}; auto js = enc(r, Format::Gdr1Json); jb.insert(jb.end(), js.begin(), js.end());   // UTF-8 BOM
    CHECK(importReplay(jb).ok());
}
TEST(t15_hlr_stub_and_custom_codec) {
    ImportOptions o; o.format = Format::Hlr; Bytes x = {1, 2, 3};
    CHECK_ERR(importReplay(x, o), ErrorCode::NotImplemented);
    CHECK_ERR(exportReplay(Replay{}, to(Format::Hlr)), ErrorCode::NotImplemented);
    struct Fake : ICodec {
        Format format() const noexcept override { return Format::Hlr; }
        std::string_view name() const noexcept override { return "fake"; }
        bool sniff(std::span<const std::uint8_t> d) const noexcept override { return d.size() >= 4 && !std::memcmp(d.data(), "HLR!", 4); }
        Result<Replay> decode(std::span<const std::uint8_t>, const ImportOptions&, Report&) const override { Replay r; r.author = "from-fake"; return r; }
        Result<Bytes> encode(const Replay&, const ExportOptions&, Report&) const override { return bytesOf("HLR!"); }
    };
    registerCodec(std::make_shared<Fake>());
    auto r = importReplay(bytesOf("HLR!zzzz")); CHECK(r.ok() && r.value().author == "from-fake");
    CHECK(importReplay(enc(makeReplay(5, 0), Format::Wbr)).ok());   // built-ins unaffected
}
TEST(t16_file_io_and_metadata_only) {
    auto path = std::filesystem::temp_directory_path() / "gdr_test_file.wbr";
    Replay r = makeReplay(100, 20);
    CHECK(exportReplayFile(r, path, to(Format::Wbr)).ok());
    ImportOptions mo; mo.metadataOnly = true; auto m = importReplayFile(path, mo);
    CHECK(m.ok() && m.value().inputs.empty() && m.value().level.name == "Stereo Madness");
    CHECK(sameReplay(r, importReplayFile(path).value()));
    std::filesystem::remove(path);
    CHECK_ERR(importReplayFile(path), ErrorCode::Io);
}
// ---- fuzz: bit flips / truncations / random bytes must never crash (run under ASan+UBSan) -----------------
TEST(t17_fuzz_mutations) {
    std::mt19937 rng(42);
    Replay r = makeReplay(300, 80); r.deaths = {3, 99}; r.replayExt = {ExtensionKind::JsonObject, "", {0x80}};
    for (Format f : {Format::Gdr1MsgPack, Format::Gdr1Json, Format::Gdr2, Format::Wbr}) {
        Replay rr = r; if (f == Format::Gdr2) { rr.replayExt = {}; rr.bot = {"xdBot", "1", 1}; rr.frameFixes.clear(); rr.lastRecordedFrame = 0; }
        Bytes base = enc(rr, f); size_t ok = 0, bad = 0;
        for (int it = 0; it < 4000; ++it) {
            Bytes m = base; int k = 1 + rng() % 4;
            while (k--) m[rng() % m.size()] ^= (std::uint8_t)(1u << (rng() % 8));
            if (rng() % 5 == 0) m.resize(rng() % m.size());
            ImportOptions io; io.limits.maxInputs = 100000; io.limits.maxFileBytes = 1 << 24;
            (importReplay(m, io).ok() ? ok : bad)++;
        }
        std::printf("    fuzz %-18s accepted=%zu rejected=%zu\n", std::string(formatName(f)).c_str(), ok, bad);
    }
    for (int it = 0; it < 3000; ++it) { Bytes m(rng() % 64); for (auto& b : m) b = rng(); if (it % 3 == 0 && m.size() > 4) std::memcpy(m.data(), it % 2 ? "GDR\x02" : "WBR\x1A", 4); (void)importReplay(m); }
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0); const char* only = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (auto& t : tests()) {
        if (only && !std::strstr(t.name, only)) continue;
        int before = g_fail; std::printf("[ RUN ] %s\n", t.name); t.fn(); ++ran;
        std::printf("[ %s ] %s\n", g_fail == before ? " OK " : "FAIL", t.name);
    }
    std::printf("\n%d test(s), %d check(s), %d failure(s)\n", ran, g_checks, g_fail);
    return g_fail ? 1 : 0;
}
