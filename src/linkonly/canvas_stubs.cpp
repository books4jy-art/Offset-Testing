// Link-only: lets the mod link against the stand-in libciphered.so. Canvas's real library provides these at runtime.
#include "canvas_api.h"

void IconLoader::getUIIcon(const std::string&, UIIcon* icon) { icon->textureId = (ImTextureID)-1; }
char* Cipher::read_asset(char*) { return nullptr; }
