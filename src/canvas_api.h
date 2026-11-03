// The parts of Canvas's own API (exported by its libciphered.so) that Offset Lab uses: the game's UI icons and
// reading a file from the game's assets. Declared here to match Canvas's exports; nothing of Canvas is included.
#pragma once

#include <string>

#include "imgui.h"

// An icon from the game's UI atlas (UiPackedAtlas): texture and its corners. textureId == (ImTextureID)-1: no icon.
class UIIcon {
public:
    ImTextureID textureId;
    ImVec2 uv0;
    ImVec2 uv1;
};

class IconLoader {
public:
    static void getUIIcon(const std::string& name, UIIcon* icon);
};

class Cipher {
public:
    // Contents of a file in the game's assets (e.g. "Data/Resources/OutfitDefs.json"), or null.
    static char* read_asset(char* path);
};
