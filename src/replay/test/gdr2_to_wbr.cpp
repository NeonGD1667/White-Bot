// Example: import any supported replay (here: an old GDR2 file) and export it as WBR, printing the report.
#include "gdr.hpp"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 3) { std::puts("usage: gdr2_to_wbr <in> <out.wbr>"); return 2; }

    gdr::Report report;
    gdr::ImportOptions in;                 // Format::Auto: detected from content, never from the extension
    in.report = &report;
    auto replay = gdr::importReplayFile(argv[1], in);
    if (!replay) { std::printf("import failed: %s\n", replay.error().toString().c_str()); return 1; }

    const gdr::Replay& r = replay.value();
    std::printf("read %s: bot '%s' (bot version '%s', file format version %s), level '%s', %zu inputs, %zu frame fixes\n",
                std::string(gdr::formatName(r.source.format)).c_str(), r.bot.name.c_str(), r.bot.version.c_str(),
                r.source.formatVersion.c_str(), r.level.name.c_str(), r.inputs.size(), r.frameFixes.size());

    gdr::ExportOptions out;
    out.format = gdr::Format::Wbr;         // LossPolicy::Error: refuses instead of silently dropping data
    out.report = &report;
    auto saved = gdr::exportReplayFile(r, argv[2], out);
    if (!saved) { std::printf("export failed: %s\n", saved.error().toString().c_str()); return 1; }

    for (const auto& d : report.items) std::printf("  [%s] %s: %s\n", d.severity == gdr::Severity::Warning ? "warn" : "info", d.code.c_str(), d.message.c_str());
    std::printf("wrote %s\n", argv[2]);
}
