// Oracle built from the REFERENCE GDR2 library (maxnut/GDReplayFormat, branch gdr2), unmodified.
#include "gdr/gdr.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
using R = gdr::Replay<void, gdr::Input<>>;

static void dump(R& r, FILE* o) {
    unsigned d; std::memcpy(&d, &r.duration, 4);
    std::fprintf(o, "meta|%s|%s|%08x|%d|%.17g|%d|%d|%d|%d|%s|%d|%u|%s\n", r.author.c_str(), r.description.c_str(), d, r.gameVersion, r.framerate,
                 r.seed, r.coins, (int)r.ldm, (int)r.platformer, r.botInfo.name.c_str(), r.botInfo.version, r.levelInfo.id, r.levelInfo.name.c_str());
    std::fprintf(o, "deaths");
    for (auto x : r.deaths) std::fprintf(o, "|%llu", (unsigned long long)x);
    std::fprintf(o, "\n");
    for (auto& i : r.inputs) std::fprintf(o, "in|%llu|%d|%d|%d\n", (unsigned long long)i.frame, (int)i.button, (int)i.player2, (int)i.down);
}
static std::vector<uint8_t> readAll(const char* p) { std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
static void writeAll(const char* p, const std::vector<uint8_t>& b) { std::ofstream f(p, std::ios::binary); f.write((const char*)b.data(), b.size()); }

int main(int argc, char** argv) {
    std::string cmd = argc > 1 ? argv[1] : "";
    if (cmd == "gen") {   // gen out n plat seed
        unsigned n = atoi(argv[3]), plat = atoi(argv[4]), s = atoi(argv[5]);
        R r; r.author = "ref-author"; r.description = "ref desc"; r.duration = 9.75f; r.gameVersion = 22074; r.framerate = 360.0;
        r.seed = (int)((s * 2654435761u) & 0x7FFFFFFF); r.coins = 3; r.ldm = s & 1; r.platformer = plat; r.botInfo = {"xdBot", 11}; r.levelInfo = gdr::Level("Lvl", 4242);
        unsigned x = s * 7 + 1; uint64_t f[2] = {0, 0};
        for (unsigned i = 0; i < n; ++i) { x = x * 1664525u + 1013904223u; bool p2 = (x >> 20) % 3 == 0; f[p2] += 1 + (x >> 8) % 50;
            r.inputs.emplace_back(f[p2], plat ? 1 + (x >> 4) % 3 : 1, p2, (x >> 3) & 1); }
        r.deaths = {12, 400};
        auto b = r.exportData(); if (b.isErr()) return 2; std::vector<uint8_t> bytes = b.unwrap(); writeAll(argv[2], bytes);
        // the reference exports in p1-then-p2 order but import re-sorts: dump what import returns
        auto back = R::importData(std::span<uint8_t>(bytes)); if (back.isErr()) { std::fprintf(stderr, "ref self-import error: %s\n", back.unwrapErr().c_str()); return 3; } R rr = back.unwrap(); dump(rr, stdout); return 0;
    }
    if (cmd == "rt" || cmd == "dump") {   // rt in out | dump in
        auto d = readAll(argv[2]); auto r = R::importData(std::span<uint8_t>(d));
        if (r.isErr()) { std::fprintf(stderr, "ref import error: %s\n", r.unwrapErr().c_str()); return 4; }
        R rr = r.unwrap();
        if (cmd == "dump") { dump(rr, stdout); return 0; }
        auto b = rr.exportData(); if (b.isErr()) return 5; std::vector<uint8_t> o = b.unwrap(); writeAll(argv[3], o); return 0;
    }
    return 1;
}
