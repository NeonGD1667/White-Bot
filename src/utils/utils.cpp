#include "utils.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <asp/fs/std.hpp>

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif

std::string Utils::narrow(const wchar_t* str) {
    if (!str) {
        return "";
    }

#ifndef GEODE_IS_WINDOWS

    std::string result;
    size_t len = wcslen(str);

    if (len == 0) {
        return result;
    }

    result.reserve(len);

    for (size_t i = 0; i < len; ++i) {
        if (str[i] > 0x7F) {
            return "";
        }

        result.push_back(static_cast<char>(str[i]));
    }

    return result;

#else

    int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        str,
        -1,
        nullptr,
        0,
        nullptr,
        nullptr
    );

    if (size <= 0) {
        return "";
    }

    auto buffer = new char[size];

    if (!buffer) {
        return "";
    }

    WideCharToMultiByte(
        CP_UTF8,
        0,
        str,
        -1,
        buffer,
        size,
        nullptr,
        nullptr
    );

    std::string result(
        buffer,
        static_cast<size_t>(size) - 1
    );

    delete[] buffer;

    return result;

#endif
}

std::wstring Utils::widen(const char* str) {
#ifndef GEODE_IS_WINDOWS

    if (!str) {
        return L"";
    }

    std::wstring result;
    size_t len = strlen(str);

    result.reserve(len);

    for (size_t i = 0; i < len; ++i) {
        result.push_back(
            static_cast<wchar_t>(str[i])
        );
    }

    return result;

#else

    if (!str) {
        return L"Widen Error";
    }

    int size = MultiByteToWideChar(
        CP_UTF8,
        0,
        str,
        -1,
        nullptr,
        0
    );

    if (size <= 0) {
        return L"Widen Error";
    }

    auto buffer = new wchar_t[size];

    if (!buffer) {
        return L"Widen Error";
    }

    if (MultiByteToWideChar(
            CP_UTF8,
            0,
            str,
            -1,
            buffer,
            size
        ) <= 0) {

        delete[] buffer;
        return L"Widen Error";
    }

    std::wstring result(
        buffer,
        static_cast<size_t>(size) - 1
    );

    delete[] buffer;

    return result;

#endif
}

std::string Utils::toLower(std::string str) {
    std::transform(
        str.begin(),
        str.end(),
        str.begin(),
        [](unsigned char c) {
            return static_cast<char>(
                std::tolower(c)
            );
        }
    );

    return str;
}

std::time_t Utils::getFileCreationTime(
    const asp::fs::Path& path
) {
#ifdef GEODE_IS_WINDOWS
auto stdPath = path.toStdPath();
if (!stdPath) {
    return 0;
}

HANDLE hFile = CreateFileW(
    stdPath->wstring().c_str(),
    GENERIC_READ,
    FILE_SHARE_READ,
    nullptr,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    nullptr
);

    if (hFile == INVALID_HANDLE_VALUE) {
        return 0;
    }

    FILETIME creationTime{};
    FILETIME lastAccessTime{};
    FILETIME lastWriteTime{};

    if (!GetFileTime(
            hFile,
            &creationTime,
            &lastAccessTime,
            &lastWriteTime
        )) {

        CloseHandle(hFile);
        return 0;
    }

    CloseHandle(hFile);

    ULARGE_INTEGER ull{};
    ull.LowPart = creationTime.dwLowDateTime;
    ull.HighPart = creationTime.dwHighDateTime;

    return static_cast<std::time_t>(
        ull.QuadPart / 10000000ULL
        - 11644473600ULL
    );

#else

    return 0;

#endif
}

std::string Utils::formatTime(std::time_t time) {
    std::tm tm{};

#ifdef GEODE_IS_WINDOWS

    localtime_s(&tm, &time);

#else

    localtime_r(&time, &tm);

#endif

    std::ostringstream oss;

    oss << std::put_time(
        &tm,
        "%Y-%m-%d %H:%M:%S"
    );

    return oss.str();
}

int Utils::copyFile(
    const std::string& sourcePath,
    const std::string& destinationPath
) {
    std::ifstream source(
        sourcePath,
        std::ios::binary
    );

    std::ofstream destination(
        destinationPath,
        std::ios::binary
    );

    if (!source) {
        return 1;
    }

    if (!destination) {
        return 2;
    }

    destination << source.rdbuf();

    return 0;
}

std::vector<std::string> Utils::splitByChar(
    std::string str,
    char splitChar
) {
    std::vector<std::string> strs;

    strs.reserve(
        std::count(
            str.begin(),
            str.end(),
            splitChar
        ) + 1
    );

    size_t start = 0;
    size_t end = str.find(splitChar);

    while (end != std::string::npos) {
        strs.emplace_back(
            str.substr(
                start,
                end - start
            )
        );

        start = end + 1;
        end = str.find(
            splitChar,
            start
        );
    }

    strs.emplace_back(
        str.substr(start)
    );

    return strs;
}

std::string Utils::getTexture() {
    auto color =
        geode::Mod::get()->getSettingValue<
            cocos2d::ccColor3B
        >("background_color");

    return color == cocos2d::ccc3(
        51,
        68,
        153
    )
        ? "GJ_square02.png"
        : "GJ_square06.png";
}

std::string Utils::getSimplifiedString(
    std::string str
) {
    if (str.find('.') == std::string::npos) {
        return str;
    }

    while (
        !str.empty() &&
        str.back() == '0'
    ) {
        str.pop_back();
    }

    if (
        !str.empty() &&
        str.back() == '.'
    ) {
        str.pop_back();
    }

    return str;
}

void Utils::setBackgroundColor(
    cocos2d::extension::CCScale9Sprite* bg
) {
    auto color =
        geode::Mod::get()->getSettingValue<
            cocos2d::ccColor3B
        >("background_color");

    if (color == cocos2d::ccc3(
        51,
        68,
        153
    )) {
        color = cocos2d::ccc3(
            255,
            255,
            255
        );
    }

    bg->setColor(color);
}