#include <Geode/Geode.hpp>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "gdr.hpp"

cocos2d::CCPoint dataFromString(
    std::string const& dataString
) {
    std::stringstream ss(dataString);
    std::string item;

    float xPos = 0.f;
    float yPos = 0.f;

    for (int i = 0; i < 3; i++) {
        if (!std::getline(ss, item, ','))
            break;

        if (i == 1) {
            try {
                xPos = std::stof(item);
            } catch (...) {
                xPos = 0.f;
            }
        } else if (i == 2) {
            try {
                yPos = std::stof(item);
            } catch (...) {
                yPos = 0.f;
            }
        }
    }

    return ccp(xPos, yPos);
}

std::vector<std::string> splitByChar(
    std::string const& str,
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
            str.substr(start, end - start)
        );

        start = end + 1;
        end = str.find(splitChar, start);
    }

    strs.emplace_back(str.substr(start));

    return strs;
}

geode::prelude::VersionInfo getVersion(
    std::vector<std::string> const& nums
) {
    if (nums.empty())
        return geode::prelude::VersionInfo(0, 0, 0);

    size_t major =
        geode::utils::numFromString<int>(
            nums[0]
        ).unwrapOr(0);

    size_t minor =
        nums.size() > 1
            ? geode::utils::numFromString<int>(
                  nums[1]
              ).unwrapOr(0)
            : 0;

    size_t patch =
        nums.size() > 2
            ? geode::utils::numFromString<int>(
                  nums[2]
              ).unwrapOr(0)
            : 0;

    return geode::prelude::VersionInfo(
        major,
        minor,
        patch
    );
}