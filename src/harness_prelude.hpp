// Minimal stand-ins for the Geode/cocos2d/GD symbols that macro.hpp mentions. Used ONLY by the harness: it compiles
// the real text of macro.hpp and of the GDR I/O part of macro.cpp without the Geode SDK.
#pragma once
#include <Geode/Geode.hpp>          // oracles/stubs: CCPoint, ccp, VersionInfo, numFromString
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace cocos2d { struct CCNode {}; struct CCDictionary {}; struct CCArray {}; }
struct PlayerObject {}; struct PlayLayer {}; struct GJGameLevel {}; struct CheckpointObject {};
struct DashRingObject {}; struct GameObject {}; struct GJPointDouble {};
namespace geode {
struct SeedValueRSV {};
namespace prelude {
namespace log {
template <class... A> void warn(std::format_string<A...> f, A&&... a) { std::cerr << "[warn] " << std::format(f, std::forward<A>(a)...) << "\n"; }
template <class... A> void error(std::format_string<A...> f, A&&... a) { std::cerr << "[error] " << std::format(f, std::forward<A>(a)...) << "\n"; }
}
}
}
namespace fmt { using std::format; }
namespace Utils { inline std::wstring widen(std::string const& s) { return std::wstring(s.begin(), s.end()); } }
