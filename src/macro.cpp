#include "includes.hpp"
#include "ui/game_ui.hpp"
#include "ui/record_layer.hpp"

#include <array>
#include <bit>
#include <cstring>
#include <optional>
#include <span>
#include <fstream>
#include <algorithm>
#include <sstream>
#include <cmath>

#define SLC_NO_DEFAULT
#include "gdr/slc/slc.hpp"

#include <Geode/modify/PlayLayer.hpp>

namespace {

class GDR2Reader {
public:
    explicit GDR2Reader(std::span<std::uint8_t const> data) : m_data(data) {}

    bool empty() const { return m_data.empty(); }

    bool readBytes(void* out, size_t size) {
        if (size > m_data.size())
            return false;
        std::memcpy(out, m_data.data(), size);
        m_data = m_data.subspan(size);
        return true;
    }

    bool skip(size_t size) {
        if (size > m_data.size())
            return false;
        m_data = m_data.subspan(size);
        return true;
    }

    template <typename T>
    bool readVarint(T& out) {
        static_assert(std::is_integral_v<T>);
        uint64_t value = 0;
        int shift = 0;

        while (true) {
            if (m_data.empty() || shift > 63)
                return false;

            auto byte = m_data.front();
            m_data = m_data.subspan(1);

            value |= static_cast<uint64_t>(byte & 0x7F) << shift;

            if ((byte & 0x80) == 0)
                break;

            shift += 7;
        }

        out = static_cast<T>(value);
        return true;
    }

    template <typename T>
    bool readBE(T& out) {
        std::array<std::uint8_t, sizeof(T)> bytes{};

        if (!readBytes(bytes.data(), bytes.size()))
            return false;

        if constexpr (std::endian::native == std::endian::little)
            std::reverse(bytes.begin(), bytes.end());

        std::memcpy(&out, bytes.data(), sizeof(T));
        return true;
    }

    bool readBool(bool& out) {
        uint8_t value = 0;

        if (!readVarint(value))
            return false;

        out = value != 0;
        return true;
    }

    bool readString(std::string& out) {
        size_t size = 0;

        if (!readVarint(size))
            return false;

        if (size > m_data.size())
            return false;

        out.assign(
            reinterpret_cast<char const*>(m_data.data()),
            size
        );

        m_data = m_data.subspan(size);
        return true;
    }

private:
    std::span<std::uint8_t const> m_data;
};

class GDR2Writer {
public:
    void writeBytes(const void* data, size_t size) {
        auto const* bytes =
            static_cast<std::uint8_t const*>(data);

        m_data.insert(
            m_data.end(),
            bytes,
            bytes + size
        );
    }

    template <typename T>
    void writeVarint(T value) {
        static_assert(std::is_integral_v<T>);

        uint64_t v = static_cast<uint64_t>(value);

        do {
            std::uint8_t byte =
                static_cast<std::uint8_t>(v & 0x7F);

            v >>= 7;

            if (v != 0)
                byte |= 0x80;

            m_data.push_back(byte);
        } while (v != 0);
    }

    template <typename T>
    void writeBE(T value) {
        std::array<std::uint8_t, sizeof(T)> bytes{};

        std::memcpy(
            bytes.data(),
            &value,
            sizeof(T)
        );

        if constexpr (std::endian::native == std::endian::little)
            std::reverse(bytes.begin(), bytes.end());

        writeBytes(bytes.data(), bytes.size());
    }

    void writeBool(bool value) {
        writeVarint<std::uint8_t>(
            value ? 1 : 0
        );
    }

    void writeString(std::string const& value) {
        writeVarint(value.size());
        writeBytes(value.data(), value.size());
    }

    std::vector<std::uint8_t> const& data() const {
        return m_data;
    }

private:
    std::vector<std::uint8_t> m_data;
};

} // namespace

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

    int result = Macro::save(
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

    std::error_code ec;

    std::filesystem::remove(
        path.string() + ".gdr",
        ec
    );

    if (ec)
        log::warn("Failed to remove previous autosave");

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

bool Macro::isGDR2Data(
    std::vector<std::uint8_t> const& data
) {
    return data.size() >= 3 &&
           data[0] == 'G' &&
           data[1] == 'D' &&
           data[2] == 'R';
}

std::optional<Macro> Macro::importGDR2(
    std::vector<std::uint8_t> const& data
) {
    if (!isGDR2Data(data))
        return std::nullopt;

    GDR2Reader reader(data);

    char magic[3]{};

    if (!reader.readBytes(
        magic,
        sizeof(magic)
    ))
        return std::nullopt;

    uint64_t version = 0;

    if (!reader.readVarint(version) ||
        version != 2)
        return std::nullopt;

    Macro macro;
    std::string inputTag;

    int gameVersion = 0;
    double framerate = 240.0;
    int replaySeed = 0;

    if (!reader.readString(inputTag) ||
        !reader.readString(macro.author) ||
        !reader.readString(macro.description) ||
        !reader.readBE(macro.duration) ||
        !reader.readVarint(gameVersion) ||
        !reader.readBE(framerate) ||
        !reader.readVarint(replaySeed) ||
        !reader.readVarint(macro.coins) ||
        !reader.readBool(macro.ldm))
        return std::nullopt;

    macro.gameVersion =
        static_cast<float>(gameVersion);

    macro.framerate =
        static_cast<float>(framerate);

    macro.seed =
        static_cast<uintptr_t>(replaySeed);

    static_cast<
        gdr::Replay<Macro, input>&
    >(macro).seed = replaySeed;

    if (!reader.readBool(macro.platformer) ||
        !reader.readString(macro.botInfo.name))
        return std::nullopt;

    int botVersion = 0;

    if (!reader.readVarint(botVersion) ||
        !reader.readVarint(macro.levelInfo.id) ||
        !reader.readString(macro.levelInfo.name))
        return std::nullopt;

    macro.botInfo.version =
        std::to_string(botVersion);

    size_t extensionSize = 0;

    if (!reader.readVarint(extensionSize) ||
        !reader.skip(extensionSize))
        return std::nullopt;

    size_t deathCount = 0;

    if (!reader.readVarint(deathCount))
        return std::nullopt;

    uint64_t deathFrame = 0;

    for (size_t i = 0; i < deathCount; ++i) {
        uint64_t delta = 0;

        if (!reader.readVarint(delta))
            return std::nullopt;

        deathFrame += delta;
    }

    size_t inputCount = 0;
    size_t p1InputCount = 0;

    if (!reader.readVarint(inputCount) ||
        !reader.readVarint(p1InputCount) ||
        p1InputCount > inputCount)
        return std::nullopt;

    macro.inputs.reserve(inputCount);

    uint64_t p1Frame = 0;
    uint64_t p2Frame = 0;

    bool hasInputExtensions =
        !inputTag.empty();

    for (size_t i = 0; i < inputCount; ++i) {
        uint64_t packed = 0;

        if (!reader.readVarint(packed))
            return std::nullopt;

        bool player2 =
            i >= p1InputCount;

        uint64_t& frameBase =
            player2 ? p2Frame : p1Frame;

        uint64_t delta =
            macro.platformer
                ? (packed >> 3)
                : (packed >> 1);

        int button =
            macro.platformer
                ? static_cast<int>(
                    (packed >> 1) & 0b11
                )
                : 1;

        bool down =
            (packed & 1) != 0;

        frameBase += delta;

        macro.inputs.emplace_back(
            static_cast<int>(frameBase),
            button,
            player2,
            down
        );

        if (hasInputExtensions) {
            size_t inputExtensionSize = 0;

            if (!reader.readVarint(
                    inputExtensionSize
                ) ||
                !reader.skip(
                    inputExtensionSize
                ))
                return std::nullopt;
        }
    }

    macro.lastRecordedFrame =
        macro.inputs.empty()
            ? 0
            : macro.inputs.back().frame;

    macro.DllBotMacro =
        macro.botInfo.name == "Dll Bot";

    return macro;
}

std::vector<std::uint8_t>
Macro::exportGDR2() {
    GDR2Writer writer;

    writer.writeBytes("GDR", 3);
    writer.writeVarint<uint64_t>(2);

    writer.writeString("");

    writer.writeString(author);
    writer.writeString(description);
    writer.writeBE(duration);

    writer.writeVarint(
        static_cast<int>(gameVersion)
    );

    writer.writeBE(
        static_cast<double>(framerate)
    );

    int replaySeed =
        static_cast<
            gdr::Replay<Macro, input>&
        >(*this).seed;

    writer.writeVarint(replaySeed);
    writer.writeVarint(coins);
    writer.writeBool(ldm);
    writer.writeBool(platformer);

    writer.writeString(botInfo.name);

    int botVersion = 0;

    try {
        botVersion =
            std::stoi(botInfo.version);
    }
    catch (...) {
        botVersion = 0;
    }

    writer.writeVarint(botVersion);
    writer.writeVarint(levelInfo.id);
    writer.writeString(levelInfo.name);

    writer.writeVarint<size_t>(0);
    writer.writeVarint<size_t>(0);

    std::vector<input const*> p1Inputs;
    std::vector<input const*> p2Inputs;

    for (auto const& in : inputs) {
        if (in.player2)
            p2Inputs.push_back(&in);
        else
            p1Inputs.push_back(&in);
    }

    writer.writeVarint(inputs.size());
    writer.writeVarint(p1Inputs.size());

    auto writeGroup =
        [&](std::vector<input const*> const& group) {
            uint64_t prevFrame = 0;

            for (auto const* in : group) {
                uint64_t frame =
                    static_cast<uint64_t>(
                       std::max(static_cast<int>(in->frame), 0)
                    );

                uint64_t delta =
                    frame - prevFrame;

                prevFrame = frame;

                uint64_t packed = 0;

                if (platformer) {
                    packed =
                        (delta << 3) |
                        (
                            (
                                static_cast<uint64_t>(
                                    in->button
                                ) & 0b11
                            ) << 1
                        ) |
                        (in->down ? 1ull : 0ull);
                }
                else {
                    packed =
                        (delta << 1) |
                        (in->down ? 1ull : 0ull);
                }

                writer.writeVarint(packed);
            }
        };

    writeGroup(p1Inputs);
    writeGroup(p2Inputs);

    return writer.data();
}

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
        gdr::Replay<Macro, input>&
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
