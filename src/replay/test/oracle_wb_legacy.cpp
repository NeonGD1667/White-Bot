// Oracle built from White Bot's CURRENT src/gdr/gdr.{hpp,cpp} (unmodified) with Geode/cocos stubbed.
#include "gdr.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
struct In : gdr::Input { In() : gdr::Input() {} In(int f, int b, bool p, bool d) : gdr::Input(f, b, p, d) {} };
struct Rep : gdr::Replay<Rep, In> { Rep() : gdr::Replay<Rep, In>("", "") {} };
static unsigned fb(float f) { unsigned u; std::memcpy(&u, &f, 4); return u; }
static std::vector<uint8_t> readAll(const char* p) { std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
static void dump(Rep& r) {
    std::printf("meta|%s|%s|%.9g|%.9g|%08x|%.9g|%d|%d|%d|%s|%s|%u|%s|%d\n", r.author.c_str(), r.description.c_str(), r.gameVersion, r.version, fb(r.duration),
                r.framerate, r.seed, r.coins, (int)r.ldm, r.botInfo.name.c_str(), r.botInfo.version.c_str(), r.levelInfo.id, r.levelInfo.name.c_str(), r.lastRecordedFrame);
    for (auto& i : r.inputs) std::printf("in|%u|%d|%d|%d\n", i.frame, i.button, (int)i.player2, (int)i.down);
    for (auto& f : r.frameFixes)
        std::printf("fix|%d|%08x|%08x|%08x|%d|%08x|%08x|%08x|%d\n", f.frame, fb(f.p1.pos.x), fb(f.p1.pos.y), fb(f.p1.rotation), (int)f.p1.rotate,
                    fb(f.p2.pos.x), fb(f.p2.pos.y), fb(f.p2.rotation), (int)f.p2.rotate);
}
int main(int argc, char** argv) {
    std::string cmd = argc > 1 ? argv[1] : "";
    auto d = readAll(argv[2]);
    Rep r = Rep::importData(d);
    if (cmd == "dump") { dump(r); return 0; }
    if (cmd == "rt") { auto b = r.exportData(argc > 4 && argv[4][0] == '1'); std::ofstream f(argv[3], std::ios::binary); f.write((const char*)b.data(), b.size()); return 0; }
    return 1;
}
