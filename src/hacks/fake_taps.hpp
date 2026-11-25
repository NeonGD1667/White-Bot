#pragma once

#include <Geode/Geode.hpp>

namespace FakeTaps {

void press(int player, cocos2d::CCPoint const& position);
void release(int player);

// Re-reads "faketaps_scale" / "faketaps_color" (call after changing them).
void reloadSettings();

}