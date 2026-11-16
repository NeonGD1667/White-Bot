#pragma once

// Geode v6 dropped std::filesystem from its public API (paths are now asp::fs::Path).
// These helpers keep the old "bool / optional" style of the v5 code on top of asp::fs,
// so call sites do not need std::error_code anymore.

#include <Geode/Geode.hpp>
#include <asp/fs.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wbfs {

using Path = asp::fs::Path;
using PathView = asp::fs::PathView;

// For APIs that still need a std path (std::ifstream / std::ofstream, FMOD, WinAPI...).
inline std::filesystem::path toStd(PathView p) {
    return std::filesystem::path(p.native());
}

// Filename including extension, empty if there is none (like std::filesystem::path::filename().string()).
inline std::string filename(PathView p) {
    auto f = p.filename();
    return f ? std::string(f->string()) : std::string();
}

// Extension INCLUDING the leading dot (std::filesystem behaviour). asp::fs::Path::extension()
// returns it WITHOUT the dot, so comparing against ".gdr" would silently break.
inline std::string extension(PathView p) {
    auto e = p.extension();
    return e ? "." + std::string(*e) : std::string();
}

inline std::string stem(PathView p) {
    auto s = p.stem();
    return s ? std::string(*s) : std::string();
}

inline Path parent(PathView p) {
    auto par = p.parent();
    return par ? Path(*par) : Path();
}

inline bool remove(PathView p) {
    return asp::fs::remove(p).isOk();
}

inline bool rename(PathView from, PathView to) {
    return asp::fs::rename(from, to).isOk();
}

inline bool createDirs(PathView p) {
    return asp::fs::createDirAll(p).isOk();
}

inline std::optional<std::uint64_t> fileSize(PathView p) {
    auto r = asp::fs::fileSize(p);
    if (r.isErr()) return std::nullopt;
    return r.unwrap();
}

// Non-recursive listing of regular files in a directory. Empty on failure.
inline std::vector<Path> listFiles(PathView dir) {
    std::vector<Path> out;
    auto it = asp::fs::iterDir(dir);
    if (it.isErr()) return out;
    auto iter = std::move(it).unwrap();
    while (auto item = iter.next()) {
        if (item->isErr()) continue;
        auto& entry = item->unwrap();
        if (entry.isFile()) out.push_back(entry.path);
    }
    return out;
}

} // namespace wbfs
