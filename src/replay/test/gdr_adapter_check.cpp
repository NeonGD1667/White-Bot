// Compile + behaviour check of gdr_wb_adapter.hpp against the REAL old White Bot model (its namespace renamed to
// wbleg, Geode/cocos stubbed). Builds with: -Ireplay -Ioracles/stubs (see run_adapter_check.sh)
#include "gdr_wb_adapter.hpp"
#include "legacy/legacy_gdr.hpp"
#include <cstdio>
#include <random>
struct In : wbleg::Input { In() : wbleg::Input() {} In(int f, int b, bool p, bool d) : wbleg::Input(f, b, p, d) {} };
struct Rep : wbleg::Replay<Rep, In> { Rep() : wbleg::Replay<Rep, In>("White Bot", "v3.1.0-Neon") {} };

int main() {
    std::mt19937 g(7); gdr::Replay r;
    r.bot = {"Dll Bot", "v3.0.1", std::nullopt}; r.author = "a"; r.level = {5, "L"}; r.gameVersion = 22081; r.duration = 4.5f; r.seed = 99; r.lastRecordedFrame = 500;
    std::uint64_t f = 10; for (int i = 0; i < 300; ++i) { f += 1 + g() % 20; gdr::Input x; x.frame = f; x.button = 1; x.player2 = g() % 5 == 0; x.down = i % 2 == 0; r.inputs.push_back(x); }
    for (int i = 0; i < 120; ++i) { gdr::FrameFix ff; ff.frame = 3 + i * 4; ff.p1 = {{1.5f * i, 100.f + i}, 3.f * i + 1, true}; ff.p2 = {{2.f * i, 5.f + i}, 7.f, true}; r.frameFixes.push_back(ff); }
    gdr::ExportOptions eo; eo.format = gdr::Format::Gdr1MsgPack; auto file = gdr::exportReplay(r, eo);
    if (!file) return 10;
    // old importer -> adapter -> new model, versus the new importer reading the same bytes
    Rep old = Rep::importData(file.value());
    gdr::Replay viaOld = gdr::wb::toReplay(static_cast<const wbleg::Replay<Rep, In>&>(old));
    auto direct = gdr::importReplay(file.value()); if (!direct) return 11;
    gdr::ExportOptions w; w.format = gdr::Format::Wbr;
    auto a = gdr::exportReplay(viaOld, w), b = gdr::exportReplay(direct.value(), w);
    bool same = a.ok() && b.ok() && a.value() == b.value();
    std::printf("old->adapter vs direct import (WBR bytes equal): %s  (%zu inputs, %zu fixes)\n", same ? "YES" : "NO", viaOld.inputs.size(), viaOld.frameFixes.size());
    // adapter round trip into the old model: old exporter output must be unchanged
    Rep copy; bool plat = false;
    bool ok = gdr::wb::fromReplay(copy, viaOld, &plat);
    auto o1 = old.exportData(false), o2 = copy.exportData(false);
    std::printf("fromReplay ok=%d, old exportData identical after round trip through the new model: %s\n", ok, o1 == o2 ? "YES" : "NO");
    return same && ok && o1 == o2 ? 0 : 1;
}
