#include "includes.hpp"
#include "ui/game_ui.hpp"
#include "ui/record_layer.hpp"

#include <array>
#include <cctype>
#include <system_error>
#include <bit>
#include <cstring>
#include <optional>
#include <span>
#include <fstream>
#include <algorithm>
#include <sstream>
#include <cmath>

#define SLC_NO_DEFAULT
#include "replay/slc/slc.hpp"

#include <Geode/modify/PlayLayer.hpp>


void Macro::recordAction(
    int frame,
    int button,
    bool player2,
    bool hold
) {
    PlayLayer* pl = PlayLayer::get();

    if (!pl)
        return;

    auto& g = Global::get();

    if (g.macro.inputs.empty())
        Macro::updateInfo(pl);

    if (g.tpsEnabled)
        g.macro.framerate = g.tps;

    if (Macro::flipControls())
        player2 = !player2;

    g.macro.inputs.emplace_back(
        frame,
        button,
        player2,
        hold
    );
}

void Macro::recordFrameFix(
    int frame,
    PlayerObject* p1,
    PlayerObject* p2
) {
    if (!p1 || !p2)
        return;

    float p1Rotation = p1->getRotation();
    float p2Rotation = p2->getRotation();

    while (p1Rotation < 0.f || p1Rotation > 360.f)
        p1Rotation += p1Rotation < 0.f ? 360.f : -360.f;

    while (p2Rotation < 0.f || p2Rotation > 360.f)
        p2Rotation += p2Rotation < 0.f ? 360.f : -360.f;

    Global::get().macro.frameFixes.push_back({
        frame,
        {p1->getPosition(), p1Rotation},
        {p2->getPosition(), p2Rotation}
    });
}

bool Macro::flipControls() {
    PlayLayer* pl = PlayLayer::get();

    if (!pl)
        return GameManager::get()->getGameVariable("0010");

    return pl->m_levelSettings->m_platformerMode
        ? false
        : GameManager::get()->getGameVariable("0010");
}

void Macro::autoSave(GJGameLevel* level, int number) {
    if (!level)
        level = PlayLayer::get()
            ? PlayLayer::get()->m_level
            : nullptr;

    if (!level)
        return;

    std::string levelname = level->m_levelName;

    auto autoSavesPath =
        Mod::get()->getSettingValue<std::filesystem::path>(
            "autosaves_folder"
        );

    auto path =
        autoSavesPath /
        fmt::format(
            "autosave_{}_{}",
            levelname,
            number
        );

    if (!std::filesystem::exists(autoSavesPath))
        return;

    std::string username =
        GJAccountManager::sharedState()
            ? GJAccountManager::sharedState()->m_username
            : "";

    // Autosaves are always written as WBR (compact, CRC-checked, keeps frame fixes).
    int result = Macro::saveWBR(
        username,
        fmt::format(
            "AutoSave {} in level {}",
            number,
            levelname
        ),
        path.string()
    );

    if (result != 0) {
        log::debug(
            "Failed to autosave macro. ID: {}. Path: {}",
            result,
            path
        );
    }
}

void Macro::tryAutosave(
    GJGameLevel* level,
    CheckpointObject* cp
) {
    auto& g = Global::get();

    if (g.state != state::recording)
        return;

    if (!g.autosaveEnabled)
        return;

    if (!g.checkpoints.contains(cp))
        return;

    if (g.checkpoints[cp].frame < g.lastAutoSaveFrame)
        return;

    if (!level)
        return;

    auto autoSavesPath =
        g.mod->getSettingValue<std::filesystem::path>(
            "autosaves_folder"
        );

    if (!std::filesystem::exists(autoSavesPath)) {
        log::debug("Failed to access auto saves path.");
        return;
    }

    std::string levelname = level->m_levelName;

    auto path =
        autoSavesPath /
        fmt::format(
            "autosave_{}_{}",
            levelname,
            g.currentSession
        );

    // Replace this session's previous autosave. Macro::saveWBR never overwrites (it appends " (n)" to an
    // existing name), so the old file must be removed first. The legacy .gdr name is removed too, for sessions
    // that were autosaving before the switch to WBR.
    std::error_code ec;

    std::filesystem::remove(
        path.string() + ".wbr",
        ec
    );

    if (ec)
        log::warn("Failed to remove previous autosave");

    std::error_code legacyEc;

    std::filesystem::remove(
        path.string() + ".gdr",
        legacyEc
    );

    autoSave(
        level,
        g.currentSession
    );
}

void Macro::updateInfo(PlayLayer* pl) {
    if (!pl)
        return;

    auto& g = Global::get();

    GJGameLevel* level = pl->m_level;

    if (!level)
        return;

    g.macro.ldm =
        level->m_lowDetailModeToggled;

    g.macro.levelInfo.id =
        level->m_levelID.value();

    g.macro.platformer =
        pl->m_levelSettings->m_platformerMode;

    g.macro.levelInfo.name =
        level->m_levelName;

    std::string author =
        GJAccountManager::sharedState()
            ? GJAccountManager::sharedState()->m_username
            : "";

    g.macro.author =
        author.empty()
            ? "N/A"
            : author;

    g.macro.botInfo.name = "Dll Bot";
    g.macro.botInfo.version = WhiteBotVersion;
    g.macro.DllBotMacro = true;
}

void Macro::updateTPS() {
    auto& g = Global::get();

    if (g.state != state::none &&
        !g.macro.inputs.empty()) {

        g.previousTpsEnabled = g.tpsEnabled;
        g.previousTps = g.tps;

        g.tpsEnabled =
            !DIF(g.macro.framerate, 240.f);

        if (g.tpsEnabled)
            g.tps = g.macro.framerate;

        g.mod->setSavedValue(
            "macro_tps",
            g.tps
        );

        g.mod->setSavedValue(
            "macro_tps_enabled",
            g.tpsEnabled
        );
    }
    else if (g.previousTps != 0.f) {
        g.tpsEnabled =
            g.previousTpsEnabled;

        g.tps = g.previousTps;
        g.previousTps = 0.f;

        g.mod->setSavedValue(
            "macro_tps",
            g.tps
        );

        g.mod->setSavedValue(
            "macro_tps_enabled",
            g.tpsEnabled
        );
    }

    if (g.layer)
        static_cast<RecordLayer*>(g.layer)->updateTPS();
}

int Macro::save(
    std::string author,
    std::string desc,
    std::string path,
    bool json
) {
    auto& g = Global::get();

    if (g.macro.inputs.empty())
        return 31;

    std::string extension =
        json ? ".gdr.json" : ".gdr";

    int iterations = 0;

    while (std::filesystem::exists(path + extension)) {
        iterations++;

        if (iterations > 1) {
            int length =
                3 +
                static_cast<int>(
                    std::to_string(iterations - 1).length()
                );

            path.erase(
                path.length() - length,
                length
            );
        }

        path += fmt::format(
            " ({})",
            iterations
        );
    }

    path += extension;

    g.macro.author = author;
    g.macro.description = desc;

    g.macro.duration =
        g.macro.inputs.back().frame /
        g.macro.framerate;

    g.macro.lastRecordedFrame =
        g.macro.inputs.back().frame;

#ifdef GEODE_IS_WINDOWS
    std::wstring widePath =
        Utils::widen(path);

    if (widePath == L"Widen Error")
        return 30;

    std::ofstream f(
        widePath,
        std::ios::binary
    );
#else
    std::ofstream f(
        path,
        std::ios::binary
    );
#endif

    if (!f)
        f.open(path, std::ios::binary);

    if (!f)
        return 20;

    auto data =
        g.macro.exportData(json);

    if (data.empty())
        return 22;

    f.write(
        reinterpret_cast<char const*>(
            data.data()
        ),
        static_cast<std::streamsize>(
            data.size()
        )
    );

    if (!f) {
        f.close();
        return 21;
    }

    f.close();

    return 0;
}

int Macro::saveGDR2(
    std::string author,
    std::string desc,
    std::string path
) {
    auto& g = Global::get();

    if (g.macro.inputs.empty())
        return 31;

    std::string extension = ".gdr2";
    int iterations = 0;

    while (std::filesystem::exists(path + extension)) {
        iterations++;

        if (iterations > 1) {
            int length =
                3 +
                static_cast<int>(
                    std::to_string(iterations - 1).length()
                );

            path.erase(
                path.length() - length,
                length
            );
        }

        path += fmt::format(
            " ({})",
            iterations
        );
    }

    path += extension;

    g.macro.author = author;
    g.macro.description = desc;

    g.macro.duration =
        g.macro.inputs.back().frame /
        g.macro.framerate;

    g.macro.lastRecordedFrame =
        g.macro.inputs.back().frame;

#ifdef GEODE_IS_WINDOWS
    std::wstring widePath =
        Utils::widen(path);

    if (widePath == L"Widen Error")
        return 30;

    std::ofstream f(
        widePath,
        std::ios::binary
    );
#else
    std::ofstream f(
        path,
        std::ios::binary
    );
#endif

    if (!f)
        f.open(path, std::ios::binary);

    if (!f)
        return 20;

    auto data =
        g.macro.exportGDR2();

    if (data.empty())
        return 22;

    f.write(
        reinterpret_cast<char const*>(
            data.data()
        ),
        static_cast<std::streamsize>(
            data.size()
        )
    );

    if (!f) {
        f.close();
        return 21;
    }

    f.close();

    return 0;
}

bool Macro::loadXDFile(
    std::filesystem::path path
) {
    Macro newMacro =
        Macro::XDtoGDR(path);

    if (newMacro.description == "fail")
        return false;

    Global::get().macro =
        std::move(newMacro);

    return true;
}

Macro Macro::XDtoGDR(
    std::filesystem::path path
) {
    Macro newMacro;

    newMacro.author = "N/A";
    newMacro.description = "N/A";
    newMacro.gameVersion = GEODE_GD_VERSION;

#ifdef GEODE_IS_WINDOWS
    std::ifstream file(
        Utils::widen(path.string())
    );
#else
    std::ifstream file(path);
#endif

    if (!file.is_open()) {
        newMacro.description = "fail";
        return newMacro;
    }

    std::string line;
    float fpsMultiplier = 1.f;

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string item;
        std::vector<std::string> action;

        while (std::getline(ss, item, '|'))
            action.push_back(item);

        if (action.empty())
            continue;

        if (action.size() < 4) {
            if (action[0] == "android") {
                fpsMultiplier = 4.f;
            }
            else {
                int fps = std::stoi(action[0]);
                fpsMultiplier = 240.f / fps;
            }

            continue;
        }

        int frame = static_cast<int>(
            std::round(
                std::stoi(action[0]) *
                fpsMultiplier
            )
        );

        int button =
            std::stoi(action[2]);

        bool hold =
            action[1] == "1";

        bool player2 =
            action[3] == "1";

        bool posOnly =
            action.size() > 4 &&
            action[4] == "1";

        if (!posOnly) {
            newMacro.inputs.emplace_back(
                frame,
                button,
                player2,
                hold
            );
        }
        else if (action.size() >= 13) {
            cocos2d::CCPoint p1Pos =
                ccp(
                    std::stof(action[5]),
                    std::stof(action[6])
                );

            cocos2d::CCPoint p2Pos =
                ccp(
                    std::stof(action[11]),
                    std::stof(action[12])
                );

            newMacro.frameFixes.push_back({
                frame,
                {p1Pos, 0.f, false},
                {p2Pos, 0.f, false}
            });
        }
    }

    return newMacro;
}

// ============================================================
// GDR1 / GDR2 / WBR serialization (unified gdr:: library)
//
// Macro keeps deriving from the old model (wbleg::Replay) so that macro.inputs / macro.frameFixes keep working
// everywhere. Reading and writing is delegated to the codecs in replay/gdr.cpp through gdr::wb::toReplay /
// fromReplay (replay/gdr_wb_adapter.hpp). The format is detected from the content, never from the file name.
// ============================================================
// BEGIN GDR-IO

namespace {

using ReplayBase = wbleg::Replay<Macro, input>;

void logReport(
    char const* what,
    gdr::Report const& report
) {
    for (auto const& d : report.items) {
        if (d.severity == gdr::Severity::Warning)
            log::warn("[{}] {}: {}", what, d.code, d.message);
    }
}

// Macro -> unified model. The base-class view is used on purpose: Macro::seed (the live RNG seed, uintptr_t)
// hides the 32-bit seed stored inside replays.
gdr::Replay toModel(
    Macro const& macro
) {
    return gdr::wb::toReplay(
        static_cast<ReplayBase const&>(macro),
        macro.platformer
    );
}

bool isGdr1(
    gdr::Format format
) {
    return format == gdr::Format::Gdr1MsgPack ||
           format == gdr::Format::Gdr1Json;
}

// Unified model -> Macro. Reproduces what the old importers did per source format:
//  - GDR1:        gameVersion is 2.2081, DllBotMacro / lastRecordedFrame come from the file only.
//  - GDR2 / WBR:  gameVersion is kept as 22081 (old importGDR2 behaviour), DllBotMacro is derived from the bot
//                 name and lastRecordedFrame falls back to the last input.
//  - all:         Macro::seed receives the file seed.
bool fromModel(
    Macro& macro,
    gdr::Replay const& replay
) {
    bool platformer = false;

    if (!gdr::wb::fromReplay(
            static_cast<ReplayBase&>(macro),
            replay,
            &platformer
        ))
        return false;

    macro.platformer = platformer;

    // The old GDR1 importer assigned the file seed to Macro::seed (the derived member hides the replay seed);
    // the replay-level seed set by fromReplay() additionally keeps it when the macro is saved again.
    macro.seed =
        static_cast<uintptr_t>(replay.seed);

    if (isGdr1(replay.source.format))
        return true;

    macro.gameVersion =
        static_cast<float>(replay.gameVersion);

    if (macro.lastRecordedFrame == 0 && !macro.inputs.empty())
        macro.lastRecordedFrame =
            macro.inputs.back().frame;

    macro.DllBotMacro =
        macro.botInfo.name == "Dll Bot";

    return true;
}

std::optional<Macro> importWith(
    std::vector<std::uint8_t> const& data,
    gdr::Format format,
    std::string* error,
    bool importInputs
) {
    gdr::Report report;
    gdr::ImportOptions options;

    options.format = format;
    options.metadataOnly = !importInputs;
    options.report = &report;

    auto replay = gdr::importReplay(data, options);

    if (!replay) {
        auto message = replay.error().toString();

        log::error("Failed to import macro: {}", message);

        if (error)
            *error = std::move(message);

        return std::nullopt;
    }

    logReport("import", report);

    Macro macro;

    if (!fromModel(macro, replay.value())) {
        if (error)
            *error = "macro contains a frame number that does not fit in an int";

        return std::nullopt;
    }

    return macro;
}

std::vector<std::uint8_t> exportWith(
    Macro const& macro,
    gdr::Format format
) {
    gdr::Report report;
    gdr::ExportOptions options;

    options.format = format;
    // Saving must not fail because a format cannot hold one field; whatever is dropped is logged.
    options.lossPolicy = gdr::LossPolicy::ReportAndDrop;
    options.report = &report;

    auto bytes = gdr::exportReplay(
        toModel(macro),
        options
    );

    logReport("export", report);

    if (!bytes) {
        log::error(
            "Failed to export macro: {}",
            bytes.error().toString()
        );

        return {};
    }

    return std::move(bytes.value());
}

std::string lowerExtension(
    std::filesystem::path const& path
) {
    std::string ext = path.extension().string();

    for (char& c : ext)
        c = static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))
        );

    return ext;
}

bool readAllBytes(
    std::filesystem::path const& path,
    std::vector<std::uint8_t>& out,
    std::string& error
) {
    std::error_code ec;

    auto const size = std::filesystem::file_size(path, ec);

    if (ec) {
        error = "cannot read the file (" + ec.message() + ")";
        return false;
    }

    if (size == 0) {
        error = "the file is empty";
        return false;
    }

    if (size > gdr::Limits{}.maxFileBytes) {
        error = "the file is too large";
        return false;
    }

    std::ifstream file(path, std::ios::binary);

    if (!file) {
        error = "cannot open the file";
        return false;
    }

    out.resize(static_cast<std::size_t>(size));

    file.read(
        reinterpret_cast<char*>(out.data()),
        static_cast<std::streamsize>(out.size())
    );

    if (static_cast<std::size_t>(file.gcount()) != out.size()) {
        error = "the file could not be read completely";
        return false;
    }

    return true;
}

} // namespace

Macro::LoadResult Macro::readFile(
    std::filesystem::path const& path,
    Macro& out
) {
    LoadResult result;
    auto& g = Global::get();
    std::string const ext = lowerExtension(path);

    // The XD / SLC loaders install the macro into Global. Keep the current macro intact so that loading for a
    // merge (or a failed load) never changes it.
    auto viaGlobal = [&](
        bool (*loader)(std::filesystem::path),
        int id,
        char const* failure
    ) {
        Macro backup = g.macro;

        bool const ok = loader(path);

        if (ok)
            out = g.macro;

        g.macro = std::move(backup);

        if (ok) {
            result.ok = true;
        } else {
            result.errorId = id;
            result.message = failure;
        }

        return result;
    };

    if (ext == ".xd")
        return viaGlobal(&Macro::loadXDFile, 45, "the XD macro could not be read");

    if (ext == ".slc2" || ext == ".slc3")
        return viaGlobal(
            ext == ".slc2" ? &Macro::loadSLC2 : &Macro::loadSLC3,
            44,
            "the SLC macro could not be read"
        );

    std::vector<std::uint8_t> data;
    std::string reason;

    if (!readAllBytes(path, data, reason)) {
        result.errorId = 50;
        result.message = std::move(reason);
        return result;
    }

    auto macro = tryImport(data, &reason);

    if (!macro) {
        result.errorId =
            gdr::detectFormat(data).format == gdr::Format::Gdr2 ? 48 : 49;
        result.message = std::move(reason);
        return result;
    }

    out = std::move(*macro);
    result.ok = true;

    return result;
}

Macro::ImportResult Macro::importToFolder(
    std::filesystem::path const& source,
    std::filesystem::path const& folder
) {
    ImportResult result;
    Macro macro;

    auto const loaded = readFile(source, macro);

    if (!loaded.ok) {
        // the import button has always reported XD failures as 46 (the macro list uses 45)
        result.errorId = loaded.errorId == 45 ? 46 : loaded.errorId;
        result.message = loaded.message;
        return result;
    }

    std::string const ext = lowerExtension(source);
    std::string extension = ".gdr.json";
    std::vector<std::uint8_t> bytes;
    std::string reason;
    bool copyAsIs = false;

    if (ext == ".slc2" || ext == ".slc3") {
        extension = ext;
        copyAsIs = true;
    }
    else if (ext != ".xd") {
        if (!readAllBytes(source, bytes, reason)) {
            result.errorId = 50;
            result.message = std::move(reason);
            return result;
        }

        switch (gdr::detectFormat(bytes).format) {
            case gdr::Format::Gdr2:
                extension = ".gdr2";
                copyAsIs = true;
                break;

            case gdr::Format::Wbr:
                extension = ".wbr";
                copyAsIs = true;
                break;

            default:
                break;
        }
    }

    if (copyAsIs && bytes.empty() && !readAllBytes(source, bytes, reason)) {
        result.errorId = 50;
        result.message = std::move(reason);
        return result;
    }

    if (!copyAsIs) {
        bytes = macro.exportData(true);

        if (bytes.empty()) {
            result.errorId = 51;
            result.message = "the macro could not be converted";
            return result;
        }
    }

    // "a.gdr.json" -> "a": the same rule the macro list uses for .json files
    std::string name = source.stem().string();

    if (ext == ".json" && name.size() > 4 &&
        name.compare(name.size() - 4, 4, ".gdr") == 0)
        name.resize(name.size() - 4);

    std::error_code ec;
    std::filesystem::create_directories(folder, ec);

    std::filesystem::path target = folder / (name + extension);

    for (int i = 1; std::filesystem::exists(target, ec); i++)
        target = folder / (name + " (" + std::to_string(i) + ")" + extension);

    std::ofstream out(target, std::ios::binary);

    if (out)
        out.write(
            reinterpret_cast<char const*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );

    if (!out) {
        out.close();
        std::filesystem::remove(target, ec);

        result.errorId = 47;
        result.message = "the macro could not be written to the macros folder";
        return result;
    }

    out.close();

    result.ok = true;
    result.createdPath = std::move(target);
    result.fromXd = ext == ".xd";

    return result;
}

Macro Macro::importData(
    std::vector<std::uint8_t> const& data,
    bool importInputs
) {
    // Same contract as the old wbleg::Replay::importData: an unreadable file yields a default Macro.
    auto macro = importWith(
        data,
        gdr::Format::Auto,
        nullptr,
        importInputs
    );

    return macro ? std::move(*macro) : Macro();
}

std::optional<Macro> Macro::tryImport(
    std::vector<std::uint8_t> const& data,
    std::string* error,
    bool importInputs
) {
    return importWith(
        data,
        gdr::Format::Auto,
        error,
        importInputs
    );
}

std::vector<std::uint8_t> Macro::exportData(
    bool exportJson
) {
    return exportWith(
        *this,
        exportJson
            ? gdr::Format::Gdr1Json
            : gdr::Format::Gdr1MsgPack
    );
}

bool Macro::isGDR2Data(
    std::vector<std::uint8_t> const& data
) {
    return gdr::detectFormat(data).format ==
           gdr::Format::Gdr2;
}

std::optional<Macro> Macro::importGDR2(
    std::vector<std::uint8_t> const& data
) {
    return importWith(
        data,
        gdr::Format::Gdr2,
        nullptr,
        true
    );
}

std::vector<std::uint8_t>
Macro::exportGDR2() {
    return exportWith(
        *this,
        gdr::Format::Gdr2
    );
}

std::vector<std::uint8_t>
Macro::exportWBR() {
    return exportWith(
        *this,
        gdr::Format::Wbr
    );
}

int Macro::saveWBR(
    std::string author,
    std::string desc,
    std::string path
) {
    auto& g = Global::get();

    if (g.macro.inputs.empty())
        return 31;

    std::string extension = ".wbr";
    int iterations = 0;

    while (std::filesystem::exists(path + extension)) {
        iterations++;

        if (iterations > 1) {
            int length =
                3 +
                static_cast<int>(
                    std::to_string(iterations - 1).length()
                );

            path.erase(
                path.length() - length,
                length
            );
        }

        path += fmt::format(
            " ({})",
            iterations
        );
    }

    path += extension;

    g.macro.author = author;
    g.macro.description = desc;

    g.macro.duration =
        g.macro.inputs.back().frame /
        g.macro.framerate;

    g.macro.lastRecordedFrame =
        g.macro.inputs.back().frame;

#ifdef GEODE_IS_WINDOWS
    std::wstring widePath =
        Utils::widen(path);

    if (widePath == L"Widen Error")
        return 30;

    std::ofstream f(
        widePath,
        std::ios::binary
    );
#else
    std::ofstream f(
        path,
        std::ios::binary
    );
#endif

    if (!f)
        f.open(path, std::ios::binary);

    if (!f)
        return 20;

    auto data =
        g.macro.exportWBR();

    if (data.empty())
        return 22;

    f.write(
        reinterpret_cast<char const*>(
            data.data()
        ),
        static_cast<std::streamsize>(
            data.size()
        )
    );

    if (!f) {
        f.close();
        return 21;
    }

    f.close();

    return 0;
}

// END GDR-IO

// ============================================================
// SLC2
// ============================================================

int Macro::saveSLC2(
    std::string author,
    std::string desc,
    std::string path
) {
    auto& g = Global::get();

    if (g.macro.inputs.empty())
        return 31;

    std::string extension = ".slc2";
    int iterations = 0;

    while (std::filesystem::exists(path + extension)) {
        ++iterations;

        if (iterations > 1) {
            int length =
                3 +
                static_cast<int>(
                    std::to_string(iterations - 1).length()
                );

            if (path.size() >= static_cast<size_t>(length)) {
                path.erase(
                    path.length() - length,
                    length
                );
            }
        }

        path += fmt::format(
            " ({})",
            iterations
        );
    }

    path += extension;

    g.macro.author = std::move(author);
    g.macro.description = std::move(desc);

    if (g.macro.framerate <= 0.f)
        g.macro.framerate = 240.f;

    g.macro.duration =
        static_cast<double>(
            g.macro.inputs.back().frame
        ) /
        static_cast<double>(
            g.macro.framerate
        );

    g.macro.lastRecordedFrame =
        g.macro.inputs.back().frame;

    slc::v2::Replay<> replay;

    replay.m_tps =
        static_cast<double>(
            g.macro.framerate
        );

    /*
     * SLC2 requires inputs to be added in non-decreasing
     * frame order.
     *
     * Macro input order is normally already chronological,
     * but sorting here makes the exporter safe.
     */
    std::vector<input const*> ordered;

    ordered.reserve(
        g.macro.inputs.size()
    );

    for (auto const& in : g.macro.inputs)
        ordered.push_back(&in);

    std::stable_sort(
        ordered.begin(),
        ordered.end(),
        [](input const* a, input const* b) {
            return a->frame < b->frame;
        }
    );

    using InputType =
        slc::v2::Input::InputType;

    for (auto const* in : ordered) {
        InputType type;

        switch (in->button) {
        case 1:
            type = InputType::Jump;
            break;

        case 2:
            type = InputType::Left;
            break;

        case 3:
            type = InputType::Right;
            break;

        default:
            log::warn(
                "SLC2: unsupported button {} at frame {}",
                in->button,
                in->frame
            );
            continue;
        }

        auto result =
            replay.addInput(
                static_cast<uint64_t>(
                    std::max(in->frame, uint32_t{0})
                ),
                type,
                in->player2,
                in->down
            );

        if (!result) {
            log::error(
                "SLC2: failed to add input at frame {}",
                in->frame
            );

            return 40;
        }
    }

#ifdef GEODE_IS_WINDOWS
    std::wstring widePath =
        Utils::widen(path);

    if (widePath == L"Widen Error")
        return 30;

    std::ofstream file(
        widePath,
        std::ios::binary
    );
#else
    std::ofstream file(
        path,
        std::ios::binary
    );
#endif

    if (!file)
        file.open(
            path,
            std::ios::binary
        );

    if (!file)
        return 20;

    replay.write(file);

    if (!file) {
        file.close();

        log::error(
            "SLC2: failed to write replay {}",
            path
        );

        return 21;
    }

    file.flush();

    if (!file) {
        file.close();
        return 21;
    }

    file.close();

    log::info(
        "SLC2 saved successfully: {}",
        path
    );

    return 0;
}

bool Macro::loadSLC2(
    std::filesystem::path path
) {
#ifdef GEODE_IS_WINDOWS
    std::ifstream file(
        Utils::widen(path.string()),
        std::ios::binary
    );
#else
    std::ifstream file(
        path,
        std::ios::binary
    );
#endif

    if (!file) {
        log::error(
            "SLC2: failed to open {}",
            path.string()
        );
        return false;
    }

    auto result =
        slc::v2::Replay<>::read(file);

    if (!result) {
        log::error(
            "SLC2: failed to read {}",
            path.string()
        );
        return false;
    }

    auto replay =
        std::move(result.value());

    Macro newMacro;

    newMacro.author = "N/A";
    newMacro.description = "N/A";
    newMacro.gameVersion =
        GEODE_GD_VERSION;

    newMacro.framerate =
        static_cast<float>(
            replay.m_tps
        );

    if (newMacro.framerate <= 0.f)
        newMacro.framerate = 240.f;

    using InputType =
        slc::v2::Input::InputType;

    auto const& inputs =
        replay.getInputs();

    newMacro.inputs.reserve(
        inputs.size()
    );

    for (auto const& action : inputs) {
        int button = 0;

        switch (action.m_button) {
        case InputType::Jump:
            button = 1;
            break;

        case InputType::Left:
            button = 2;
            break;

        case InputType::Right:
            button = 3;
            break;

        /*
         * SLC2 supports more than normal GD inputs:
         *
         * Restart
         * RestartFull
         * Death
         * TPS
         *
         * Macro currently has no matching input type for
         * these, so ignore them.
         */
        default:
            continue;
        }

        newMacro.inputs.emplace_back(
            static_cast<int>(
                action.m_frame
            ),
            button,
            action.m_player2,
            action.m_holding
        );
    }

    std::stable_sort(
        newMacro.inputs.begin(),
        newMacro.inputs.end(),
        [](input const& a, input const& b) {
            return a.frame < b.frame;
        }
    );

    newMacro.lastRecordedFrame =
        newMacro.inputs.empty()
            ? 0
            : newMacro.inputs.back().frame;

    if (!newMacro.inputs.empty()) {
        newMacro.duration =
            static_cast<double>(
                newMacro.lastRecordedFrame
            ) /
            static_cast<double>(
                newMacro.framerate
            );
    }
    else {
        newMacro.duration = 0.0;
    }

    newMacro.DllBotMacro = false;

    Global::get().macro =
        std::move(newMacro);

    log::info(
        "SLC2 loaded: {} inputs, {} TPS",
        Global::get().macro.inputs.size(),
        Global::get().macro.framerate
    );

    return true;
}

// ============================================================
// SLC3
// ============================================================

int Macro::saveSLC3(
    std::string author,
    std::string desc,
    std::string path
) {
    auto& g = Global::get();

    if (g.macro.inputs.empty())
        return 31;

    std::string extension = ".slc3";
    int iterations = 0;

    while (std::filesystem::exists(path + extension)) {
        ++iterations;

        if (iterations > 1) {
            int length =
                3 +
                static_cast<int>(
                    std::to_string(iterations - 1).length()
                );

            if (path.size() >=
                static_cast<size_t>(length)) {
                path.erase(
                    path.length() - length,
                    length
                );
            }
        }

        path += fmt::format(
            " ({})",
            iterations
        );
    }

    path += extension;

    g.macro.author = std::move(author);
    g.macro.description = std::move(desc);

    if (g.macro.framerate <= 0.f)
        g.macro.framerate = 240.f;

    g.macro.duration =
        static_cast<double>(
            g.macro.inputs.back().frame
        ) /
        static_cast<double>(
            g.macro.framerate
        );

    g.macro.lastRecordedFrame =
        g.macro.inputs.back().frame;

    slc::v3::Replay<> replay;

    replay.m_meta.m_tps =
        static_cast<double>(
            g.macro.framerate
        );

    replay.m_meta.m_seed =
        static_cast<uint64_t>(
            g.macro.seed
        );

    replay.m_meta.m_version = 2;
    replay.m_meta.m_build = 0;
    replay.m_meta.m_randomnessAlgorithm = 0;

    slc::v3::ActionAtom atom;

    using ActionType =
        slc::v3::Action::ActionType;

    /*
     * SLC actions must be sorted by frame.
     * Stable sort keeps same-frame input order.
     */
    std::vector<input const*> ordered;

    ordered.reserve(
        g.macro.inputs.size()
    );

    for (auto const& in : g.macro.inputs)
        ordered.push_back(&in);

    std::stable_sort(
        ordered.begin(),
        ordered.end(),
        [](input const* a, input const* b) {
            return a->frame < b->frame;
        }
    );

    for (auto const* in : ordered) {
        ActionType type;

        switch (in->button) {
        case 1:
            type = ActionType::Jump;
            break;

        case 2:
            type = ActionType::Left;
            break;

        case 3:
            type = ActionType::Right;
            break;

        default:
            log::warn(
                "SLC3: unsupported button {} at frame {}",
                in->button,
                in->frame
            );
            continue;
        }

        auto result =
    atom.addAction(
        static_cast<uint64_t>(in->frame),
        type,
        in->down,
        in->player2
    );

        if (!result) {
            log::error(
                "SLC3: failed to add action at frame {}: {}",
                in->frame,
                result.error().m_message
            );
            return 40;
        }
    }

    /*
     * TPS is already stored in metadata.
     * Do not create a TPS action because the Macro struct
     * has no special-action storage and metadata is enough
     * for this round-trip.
     */

    replay.m_atoms.add(
        typename slc::v3::DefaultRegistry::Variant{
            std::move(atom)
        }
    );

#ifdef GEODE_IS_WINDOWS
    std::wstring widePath =
        Utils::widen(path);

    if (widePath == L"Widen Error")
        return 30;

    std::ofstream file(
        widePath,
        std::ios::binary
    );
#else
    std::ofstream file(
        path,
        std::ios::binary
    );
#endif

    if (!file)
        file.open(
            path,
            std::ios::binary
        );

    if (!file)
        return 20;

    auto result =
        replay.write(file);

    if (!result) {
        log::error(
            "SLC3: failed to write replay: {}",
            result.error().m_message
        );

        file.close();
        return 21;
    }

    file.flush();

    if (!file) {
        file.close();
        return 21;
    }

    file.close();

    log::info(
        "SLC3 saved successfully: {}",
        path
    );

    return 0;
}

bool Macro::loadSLC3(
    std::filesystem::path path
) {
#ifdef GEODE_IS_WINDOWS
    std::ifstream file(
        Utils::widen(path.string()),
        std::ios::binary
    );
#else
    std::ifstream file(
        path,
        std::ios::binary
    );
#endif

    if (!file) {
        log::error(
            "SLC3: failed to open {}",
            path.string()
        );
        return false;
    }

    auto result =
        slc::v3::Replay<>::read(file);

    if (!result) {
        log::error(
            "SLC3: failed to read {}: {}",
            path.string(),
            result.error().m_message
        );
        return false;
    }

    auto replay =
        std::move(result.value());

    slc::v3::ActionAtom const* actionAtom =
        nullptr;

    for (auto const& atom :
         replay.m_atoms.m_atoms) {

        if (auto const* action =
            std::get_if<
                slc::v3::ActionAtom
            >(&atom)) {

            actionAtom = action;
            break;
        }
    }

    if (!actionAtom) {
        log::error(
            "SLC3: no ActionAtom found"
        );
        return false;
    }

    Macro newMacro;

    newMacro.author = "N/A";
    newMacro.description = "N/A";
    newMacro.gameVersion =
        GEODE_GD_VERSION;

    newMacro.framerate =
        static_cast<float>(
            replay.m_meta.m_tps
        );

    if (newMacro.framerate <= 0.f)
        newMacro.framerate = 240.f;

    newMacro.seed =
        static_cast<uintptr_t>(
            replay.m_meta.m_seed
        );

    static_cast<
        wbleg::Replay<Macro, input>&
    >(newMacro).seed =
        static_cast<int>(
            replay.m_meta.m_seed
        );

    using ActionType =
        slc::v3::Action::ActionType;

    newMacro.inputs.reserve(
        actionAtom->m_actions.size()
    );

    for (auto const& action :
         actionAtom->m_actions) {

        if (!action.isPlayer())
            continue;

        int button;

        switch (action.m_type) {
        case ActionType::Jump:
            button = 1;
            break;

        case ActionType::Left:
            button = 2;
            break;

        case ActionType::Right:
            button = 3;
            break;

        default:
            continue;
        }

        newMacro.inputs.emplace_back(
            static_cast<int>(
                action.m_frame
            ),
            button,
            action.m_player2,
            action.m_holding
        );
    }

    std::stable_sort(
        newMacro.inputs.begin(),
        newMacro.inputs.end(),
        [](input const& a, input const& b) {
            return a.frame < b.frame;
        }
    );

    newMacro.lastRecordedFrame =
        newMacro.inputs.empty()
            ? 0
            : newMacro.inputs.back().frame;

    if (!newMacro.inputs.empty()) {
        newMacro.duration =
            static_cast<double>(
                newMacro.lastRecordedFrame
            ) /
            static_cast<double>(
                newMacro.framerate
            );
    }
    else {
        newMacro.duration = 0.0;
    }

    newMacro.DllBotMacro = false;

    Global::get().macro =
        std::move(newMacro);

    log::info(
        "SLC3 loaded: {} inputs, {} TPS",
        Global::get().macro.inputs.size(),
        Global::get().macro.framerate
    );

    return true;
}

// ============================================================
// State
// ============================================================

void Macro::resetVariables() {
    auto& g = Global::get();

    g.ignoreFrame = -1;
    g.ignoreJumpButton = -1;

    g.delayedFrameReleaseMain[0] = -1;
    g.delayedFrameReleaseMain[1] = -1;

    g.delayedFrameInput[0] = -1;
    g.delayedFrameInput[1] = -1;

    g.addSideHoldingMembers[0] = false;
    g.addSideHoldingMembers[1] = false;

    for (int x = 0; x < 2; ++x) {
        for (int y = 0; y < 2; ++y)
            g.delayedFrameRelease[x][y] = -1;
    }
}

void Macro::resetState(bool cp) {
    auto& g = Global::get();

    g.restart = false;
    g.state = state::none;

    if (!cp)
        g.checkpoints.clear();

    Interface::updateLabels();
    Interface::updateButtons();

    Macro::resetVariables();
}

void Macro::togglePlaying() {
    if (Global::hasIncompatibleMods())
        return;

    auto& g = Global::get();

    if (g.layer) {
        auto* layer =
            static_cast<RecordLayer*>(
                g.layer
            );

        layer->playing->toggle(
            g.state != state::playing
        );

        layer->togglePlaying(nullptr);
    }
    else {
        auto* layer =
            RecordLayer::create();

        layer->togglePlaying(nullptr);
        layer->onClose(nullptr);
    }
}

void Macro::toggleRecording() {
    if (Global::hasIncompatibleMods())
        return;

    auto& g = Global::get();

    if (g.layer) {
        auto* layer =
            static_cast<RecordLayer*>(
                g.layer
            );

        layer->recording->toggle(
            g.state != state::recording
        );

        layer->toggleRecording(nullptr);
    }
    else {
        auto* layer =
            RecordLayer::create();

        layer->toggleRecording(nullptr);
        layer->onClose(nullptr);
    }
}

bool Macro::shouldStep() {
    auto& g = Global::get();

    if (g.stepFrame)
        return true;

    if (Global::getCurrentFrame() == 0)
        return true;

    return false;
}
