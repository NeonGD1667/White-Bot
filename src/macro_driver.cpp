// Driver compiled twice: against the OLD v3.1.1 macro (-DOLD_TREE) and against the NEW macro. Same commands, same dump.
#include "harness_prelude.hpp"
#include "macro.hpp"
#ifdef OLD_TREE
namespace wbleg = gdr;
#endif
struct Global { Macro macro; static Global& get() { static Global g; return g; } };
#include "io_block.inc"   // the GDR I/O part of macro.cpp (old: GDR2Reader/Writer + importGDR2/exportGDR2; new: BEGIN/END GDR-IO)


static unsigned fb(float f) { unsigned u; std::memcpy(&u, &f, 4); return u; }
static std::vector<std::uint8_t> readAll(char const* p) { std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
static void writeAll(char const* p, std::vector<std::uint8_t> const& b) { std::ofstream f(p, std::ios::binary); f.write((char const*)b.data(), b.size()); }

static void dump(Macro& m) {
    auto& b = static_cast<wbleg::Replay<Macro, input>&>(m);
    std::printf("meta|%s|%s|%08x|%.9g|%.9g|%d|%d|%d|%s|%s|%u|%s|%d|plat=%d|dll=%d|macroSeed=%lld\n", m.author.c_str(), m.description.c_str(), fb(m.duration),
                (double)m.gameVersion, (double)m.framerate, b.seed, m.coins, (int)m.ldm, m.botInfo.name.c_str(), m.botInfo.version.c_str(), m.levelInfo.id,
                m.levelInfo.name.c_str(), m.lastRecordedFrame, (int)m.platformer, (int)m.DllBotMacro, (long long)m.seed);
    for (auto& i : m.inputs) std::printf("in|%d|%d|%d|%d\n", i.frame, i.button, (int)i.player2, (int)i.down);
    for (auto& f : m.frameFixes)
        std::printf("fix|%d|%08x|%08x|%08x|%d|%08x|%08x|%08x|%d\n", f.frame, fb(f.p1.pos.x), fb(f.p1.pos.y), fb(f.p1.rotation), (int)f.p1.rotate,
                    fb(f.p2.pos.x), fb(f.p2.pos.y), fb(f.p2.rotation), (int)f.p2.rotate);
}
// the load path used by ui/load_macro_layer.cpp and ui/macro_cell.cpp
static bool load(char const* path, Macro& out) {
    auto data = readAll(path);
    if (Macro::isGDR2Data(data)) { auto i = Macro::importGDR2(data); if (!i) return false; out = std::move(*i); return true; }
    out = Macro::importData(data); return true;
}
static Macro make(int variant, bool plat) {
    Macro m; unsigned s = 12345 + variant; auto rnd = [&] { s = s * 1664525u + 1013904223u; return s >> 8; };
    char const* bots[] = {"White Bot", "Dll Bot", "Dll Bot", "Other"}; char const* vers[] = {"v3.1.0-Neon", "v2.3.5", "v3.0.1", "7"};
    m.botInfo.name = bots[variant % 4]; m.botInfo.version = vers[variant % 4];
    m.author = "au"; m.description = "de"; m.duration = 5.5f; m.gameVersion = 2.2081f; m.framerate = 240.f; static_cast<wbleg::Replay<Macro, input>&>(m).seed = getenv("HARNESS_SEED") ? atoi(getenv("HARNESS_SEED")) : 1234; m.coins = 2; m.ldm = true;
    m.levelInfo.id = 99; m.levelInfo.name = "L"; m.platformer = plat;
    int f = 20; for (int i = 0; i < 300; ++i) { f += 1 + rnd() % 30; m.inputs.emplace_back(f, plat ? 1 + rnd() % 3 : 1, rnd() % 5 == 0, i % 2 == 0); }
    for (int i = 0; i < 90; ++i) { wbleg::FrameFix ff{}; ff.frame = 5 + i * 6; ff.p1.pos = {1.5f * i + 3, 100.f + i}; ff.p1.rotation = 7.f * i + 1; ff.p2.pos = {2.f * i + 1, 9.f + i}; ff.p2.rotation = 3.f * i + 1; m.frameFixes.push_back(ff); }
    m.lastRecordedFrame = m.inputs.back().frame; return m;
}
int main(int argc, char** argv) {
    std::string c = argv[1];
    if (c == "dump") { Macro m; if (!load(argv[2], m)) { std::puts("LOAD-FAILED"); return 3; } dump(m); return 0; }
    if (c == "gen") {   // gen <out> <variant> <plat> <json|mp|gdr2>
        Macro m = make(atoi(argv[3]), atoi(argv[4])); std::string k = argv[5];
        writeAll(argv[2], k == "gdr2" ? m.exportGDR2() : m.exportData(k == "json")); return 0;
    }
#ifndef OLD_TREE
    if (c == "wbr") { Macro m = make(atoi(argv[3]), atoi(argv[4])); writeAll(argv[2], m.exportWBR()); return 0; }   // NEW build only
#endif
    if (c == "rt") {    // rt <in> <out> <json|mp|gdr2>
        Macro m; if (!load(argv[2], m)) return 3; std::string k = argv[4];
        writeAll(argv[3], k == "gdr2" ? m.exportGDR2() : m.exportData(k == "json")); return 0;
    }
    return 1;
}
