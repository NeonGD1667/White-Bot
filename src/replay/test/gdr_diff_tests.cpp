// Differential tests: the new library vs the ORIGINAL code of both sources, run as external oracle programs
// (oracle_wb_legacy = White Bot src/gdr/gdr.cpp, oracle_ref2 = maxnut GDR2 reference). Usage: gdr_diff_tests <oracle_wb> <oracle_ref2>
#include "gdr.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
using namespace gdr;
using Bytes = std::vector<std::uint8_t>;
static int g_fail = 0, g_checks = 0;
#define CHECK(c, ...) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("  FAIL line %d: %s  ", __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static std::string run(const std::string& cmd, int* rc = nullptr) {
    std::string out; FILE* p = popen((cmd + " 2>/dev/null").c_str(), "r"); char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
    int st = pclose(p); if (rc) *rc = WEXITSTATUS(st); return out;
}
static void writeFile(const std::string& p, const Bytes& b) { std::ofstream f(p, std::ios::binary); f.write((const char*)b.data(), b.size()); }
static Bytes readFile(const std::string& p) { std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
static unsigned fb(float f) { unsigned u; std::memcpy(&u, &f, 4); return u; }
static std::string fmt(const char* f, ...) { char b[1024]; va_list a; va_start(a, f); vsnprintf(b, sizeof b, f, a); va_end(a); return b; }

// canonical dumps identical in layout to the oracles'
static std::string dumpGdr1(const Replay& r) {
    std::string s = fmt("meta|%s|%s|%.9g|%.9g|%08x|%.9g|%d|%d|%d|%s|%s|%u|%s|%d\n", r.author.c_str(), r.description.c_str(), (double)(float(r.gameVersion) / 10000.f),
                        (double)(float)r.gdr1FormatVersion, fb(r.duration), (double)(float)r.framerate, r.seed, r.coins, (int)r.ldm, r.bot.name.c_str(), r.bot.version.c_str(), r.level.id, r.level.name.c_str(), (int)r.lastRecordedFrame);
    for (auto& i : r.inputs) s += fmt("in|%u|%d|%d|%d\n", (unsigned)i.frame, i.button, (int)i.player2, (int)i.down);
    for (auto& f : r.frameFixes)
        s += fmt("fix|%d|%08x|%08x|%08x|%d|%08x|%08x|%08x|%d\n", (int)f.frame, fb(f.p1.pos.x), fb(f.p1.pos.y), fb(f.p1.rotation), (int)f.p1.rotate, fb(f.p2.pos.x), fb(f.p2.pos.y), fb(f.p2.rotation), (int)f.p2.rotate);
    return s;
}
static std::string dumpGdr2(const Replay& r) {
    std::string s = fmt("meta|%s|%s|%08x|%d|%.17g|%d|%d|%d|%d|%s|%d|%u|%s\n", r.author.c_str(), r.description.c_str(), fb(r.duration), r.gameVersion, r.framerate, r.seed, r.coins,
                        (int)r.ldm, (int)r.platformer, r.bot.name.c_str(), (int)r.bot.versionNumber.value_or(0), r.level.id, r.level.name.c_str());
    s += "deaths"; for (auto d : r.deaths) s += fmt("|%llu", (unsigned long long)d); s += "\n";
    for (auto& i : r.inputs) s += fmt("in|%llu|%d|%d|%d\n", (unsigned long long)i.frame, i.button, (int)i.player2, (int)i.down);
    return s;
}
static std::string firstDiff(const std::string& a, const std::string& b) {
    std::istringstream x(a), y(b); std::string l, m; int n = 1;
    while (true) { bool gx = !!std::getline(x, l), gy = !!std::getline(y, m); if (!gx && !gy) return "(identical)"; if (!gx || !gy || l != m) return fmt("line %d:\n   new   : %s\n   oracle: %s", n, gx ? l.c_str() : "<eof>", gy ? m.c_str() : "<eof>"); ++n; }
}

static Replay gen1(const char* bot, const char* ver, unsigned seed, size_t nIn, size_t nFix, bool p2) {
    std::mt19937 g(seed); Replay r;
    r.author = "au"; r.description = "d"; r.bot = {bot, ver, std::nullopt}; r.level = {77, "Lvl"}; r.gameVersion = 22081; r.duration = 3.25f; r.framerate = 240;
    r.seed = (int)(g() & 0x7FFFFFFF); r.coins = 1; r.ldm = seed & 1;
    std::uint64_t f = 10; for (size_t i = 0; i < nIn; ++i) { f += 1 + g() % 30; Input x; x.frame = f; x.button = 1 + g() % 3; x.player2 = p2 && g() % 4 == 0; x.down = i % 2 == 0; r.inputs.push_back(x); }
    float px = 5, py = 90;
    for (size_t i = 0; i < nFix; ++i) { px += 1.3f * (1 + g() % 3); py += (int(g() % 7) - 3) * 0.41f; FrameFix ff; ff.frame = 3 + i * 5; ff.p1.pos = {px, py}; ff.p1.rotation = 1.0f + (g() % 360);
        if (p2) { ff.p2.pos = {px + 2, py + 7.5f}; ff.p2.rotation = 2.0f + (g() % 90); } r.frameFixes.push_back(ff); }
    r.lastRecordedFrame = r.inputs.empty() ? 0 : (std::uint32_t)r.inputs.back().frame;
    return r;
}

int main(int argc, char** argv) {
    if (argc < 3) { std::puts("usage: gdr_diff_tests <oracle_wb_legacy> <oracle_ref2>"); return 2; }
    setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string wb = argv[1], ref = argv[2], dir = "/tmp/gdrdiff/";
    run("mkdir -p " + dir);
    // ---------------- A. White Bot legacy GDR1 ----------------
    struct Case { const char* bot; const char* ver; bool p2; };
    const Case cases[] = {{"White Bot", "v3.1.0-Neon", true}, {"White Bot", "v3.1.0-Neon", false}, {"Dll Bot", "v3.0.1", true}, {"Dll Bot", "v2.3.6", true},
                          {"Dll Bot", "v2.3.5", true}, {"Dll Bot", "2.3.5", false}, {"Dll Bot", "v3.0.0-beta.2", true}, {"Dll Bot", "v2.4.0-alpha.1", false}, {"Other", "1", true}, {"Dll Bot", "", true}};
    int n = 0;
    for (const Case& c : cases) for (Format f : {Format::Gdr1MsgPack, Format::Gdr1Json}) for (size_t sz : {0u, 1u, 250u}) {
        ++n; const std::string tag = fmt("%s %s p2=%d %s n=%zu", c.bot, c.ver, (int)c.p2, f == Format::Gdr1Json ? "json" : "msgpack", (size_t)sz);
        std::printf("case %s\n", tag.c_str());
        Replay r = gen1(c.bot, c.ver, 100 + n, sz, sz / 2, c.p2);
        ExportOptions eo; eo.format = f;
        auto b = exportReplay(r, eo); CHECK(b.ok(), "export %s", tag.c_str()); if (!b) continue;
        const std::string f1 = dir + "a1.bin", f2 = dir + "a2.bin"; writeFile(f1, b.value());
        // (1) new writer -> old reader must equal new reader
        int rc; std::string legacy1 = run(wb + " dump " + f1, &rc); CHECK(rc == 0, "legacy import rc=%d (%s)", rc, tag.c_str());
        auto m1 = importReplay(b.value()); CHECK(m1.ok(), "new import %s", tag.c_str()); if (!m1) continue;
        std::string mine1 = dumpGdr1(m1.value()); CHECK(mine1 == legacy1, "[%s] new-writer file: %s", tag.c_str(), firstDiff(mine1, legacy1).c_str());
        // (2) old writer -> new reader must equal old reader
        run(wb + " rt " + f1 + " " + f2 + (f == Format::Gdr1Json ? " 1" : " 0"));
        Bytes old = readFile(f2); CHECK(!old.empty(), "legacy export %s", tag.c_str()); if (old.empty()) continue;
        std::string legacy2 = run(wb + " dump " + f2);
        auto m2 = importReplay(old); CHECK(m2.ok(), "new import of legacy file %s: %s", tag.c_str(), m2.ok() ? "" : m2.error().toString().c_str()); if (!m2) continue;
        std::string mine2 = dumpGdr1(m2.value()); CHECK(mine2 == legacy2, "[%s] legacy-writer file: %s", tag.c_str(), firstDiff(mine2, legacy2).c_str());
        // (3) writing what we read must reproduce the legacy writer's bytes (only when the legacy offset does not drift)
        auto again = exportReplay(m1.value(), eo);
        if (legacyDllBotFrameOffset(r.bot) == 0 && c.p2 == false) CHECK(again.ok() && again.value() == old || sz == 0, "[%s] bytes differ from legacy re-export", tag.c_str());
        // (4) legacy drift: old exporter adds +1 on every re-save of an old Dll Bot macro, ours must not
        if (legacyDllBotFrameOffset(r.bot) == 1 && sz) { auto z = importReplay(again.value()); CHECK(z.ok() && z.value().inputs[0].frame == m1.value().inputs[0].frame, "[%s] offset drift", tag.c_str()); }
    }
    std::printf("A. White Bot legacy GDR1: %d configurations compared\n", n);

    // ---------------- B. reference GDR2 ----------------
    int m = 0;
    for (int plat = 0; plat < 2; ++plat) for (unsigned s : {1u, 2u, 3u, 9u}) for (unsigned cnt : {0u, 1u, 7u, 500u}) {
        ++m; const std::string tag = fmt("plat=%d seed=%u n=%u", plat, s, cnt), p = dir + "b.gdr2";
        int rc; std::string refDump = run(ref + " gen " + p + " " + std::to_string(cnt) + " " + std::to_string(plat) + " " + std::to_string(s), &rc);
        CHECK(rc == 0, "ref gen rc=%d %s", rc, tag.c_str()); if (rc) continue;
        Bytes fileBytes = readFile(p);
        auto mine = importReplay(fileBytes); CHECK(mine.ok(), "new import of reference file %s: %s", tag.c_str(), mine.ok() ? "" : mine.error().toString().c_str()); if (!mine) continue;
        CHECK(mine.value().source.format == Format::Gdr2, "format");
        std::string md = dumpGdr2(mine.value()); CHECK(md == refDump, "[%s] reading reference file: %s", tag.c_str(), firstDiff(md, refDump).c_str());
        // new writer must reproduce the reference writer byte for byte
        ExportOptions eo; eo.format = Format::Gdr2; auto back = exportReplay(mine.value(), eo);
        CHECK(back.ok() && back.value() == fileBytes, "[%s] new GDR2 writer differs from reference bytes (%zu vs %zu)", tag.c_str(), back.ok() ? back.value().size() : 0, fileBytes.size());
        // reference reader must accept our output and re-export identical bytes
        writeFile(dir + "b2.gdr2", back.value()); int rc2; run(ref + " rt " + dir + "b2.gdr2 " + dir + "b3.gdr2", &rc2); CHECK(rc2 == 0, "reference reader rejected our file %s", tag.c_str());
        CHECK(readFile(dir + "b3.gdr2") == back.value(), "[%s] reference round trip changed bytes", tag.c_str());
    }
    std::printf("B. reference GDR2: %d configurations compared\n", m);

    // ---------------- C. known reference limitation: negative seed ----------------
    {
        Replay r = gen1("xdBot", "11", 5, 10, 0, false); r.bot.versionNumber = 11; r.seed = -77; r.lastRecordedFrame = 0; for (auto& i : r.inputs) i.button = 1;
        ExportOptions eo; eo.format = Format::Gdr2; auto b = exportReplay(r, eo); writeFile(dir + "c.gdr2", b.value());
        int rc; run(ref + " dump " + dir + "c.gdr2", &rc);
        auto back = importReplay(b.value());
        CHECK(back.ok() && back.value().seed == -77, "negative seed round trip");
        std::printf("C. negative seed (sign-extended varint): reference reader exit code %d (%s); new reader %s\n", rc, rc ? "REJECTS it" : "accepts it", back.ok() ? "accepts it" : "rejects it");
    }
    std::printf("\n%d check(s), %d failure(s)\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
