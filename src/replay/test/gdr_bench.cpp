// Benchmark on SYNTHETIC replays (no real macros were available). Usage: gdr_bench <inputs> <frameFixes> <format> [dumpfile]
// format: gdr2 | gdr2wbx | json | msgpack | wbr
#include "gdr.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <sys/resource.h>
using namespace gdr;
static double ms(std::chrono::steady_clock::time_point a) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count(); }
static long rssKb() { rusage u; getrusage(RUSAGE_SELF, &u); return u.ru_maxrss; }

static Replay make(size_t nIn, size_t nFix, bool wb) {
    std::mt19937 g(2024); Replay r;
    r.author = "bench"; r.description = "synthetic"; r.bot = wb ? Bot{"White Bot", "v3.1.0-Neon", std::nullopt} : Bot{"xdBot", "12", 12};
    r.level = {1, "Synthetic"}; r.gameVersion = 22081; r.framerate = 240; r.duration = float(nIn) / 4.f;
    std::uint64_t f = 0; for (size_t i = 0; i < nIn; ++i) { f += 1 + g() % 45; Input x; x.frame = f; x.button = 1; x.down = i % 2 == 0; x.player2 = (g() % 6) == 0; r.inputs.push_back(x); }
    // physically plausible track: constant speed with float rounding, parabolic y with jumps, rotation spin
    double X = 0, Y = 105, vy = 0; float rot = 0;
    for (size_t i = 0; i < nFix; ++i) {
        X += 10.38623 * (1 + (g() % 4)) / 4.0 * 0.25 * 4; vy += (g() % 40 == 0) ? 11.2 : -0.9; Y += vy * 0.25; if (Y < 105) { Y = 105; vy = 0; }
        rot += (Y > 105) ? 6.f : 0.f; if (rot > 360) rot -= 360;
        FrameFix ff; ff.frame = (i + 1) * 3; ff.p1 = {{float(X), float(Y)}, rot, true}; ff.p2 = {{float(X) + 30.f, float(Y)}, rot, true}; r.frameFixes.push_back(ff);
    }
    r.lastRecordedFrame = r.inputs.empty() ? 0 : (std::uint32_t)r.inputs.back().frame;
    return r;
}

int main(int argc, char** argv) {
    if (argc < 4) return 2;
    const size_t nIn = std::stoul(argv[1]), nFix = std::stoul(argv[2]); const std::string fm = argv[3];
    ExportOptions eo; eo.lossPolicy = LossPolicy::ReportAndDrop;
    if (fm == "gdr2") eo.format = Format::Gdr2, eo.gdr2.emitWhiteBotExtension = false;
    else if (fm == "gdr2wbx") eo.format = Format::Gdr2;
    else if (fm == "json") eo.format = Format::Gdr1Json;
    else if (fm == "msgpack") eo.format = Format::Gdr1MsgPack;
    else eo.format = Format::Wbr;
    Replay r = make(nIn, nFix, fm != "gdr2");   // plain gdr2 is the "other bot" case: frame fixes are dropped (reported)
    const long base = rssKb();
    // warm up + measure: best of N
    const int reps = nIn >= 500000 ? 3 : (nIn >= 50000 ? 5 : 30);
    double bestE = 1e18, bestD = 1e18; std::vector<std::uint8_t> out;
    for (int i = 0; i < reps; ++i) { auto t = std::chrono::steady_clock::now(); auto b = exportReplay(r, eo); double e = ms(t); if (!b) { std::printf("export failed: %s\n", b.error().toString().c_str()); return 1; } if (i == 0) out = b.value(); bestE = std::min(bestE, e); }
    ImportOptions io; io.rotation = RotationPolicy::PreserveAll; io.limits.maxInputs = 1 << 27; io.limits.maxFrameFixes = 1 << 27;
    for (int i = 0; i < reps; ++i) { auto t = std::chrono::steady_clock::now(); auto b = importReplay(out, io); double d = ms(t); if (!b) { std::printf("import failed: %s\n", b.error().toString().c_str()); return 1; } bestD = std::min(bestD, d); }
    if (argc > 4) { std::ofstream f(argv[4], std::ios::binary); f.write((const char*)out.data(), out.size()); }
    std::printf("%-8s inputs=%-8zu fixes=%-8zu size=%10zu B  enc=%9.3f ms  dec=%9.3f ms  peakRSS+%ld MB\n", fm.c_str(), nIn, nFix, out.size(), bestE, bestD, (rssKb() - base) / 1024);
}
