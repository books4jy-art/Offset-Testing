// Canvas entry points: Canvas calls Start() and uses the returned function to draw the mod's window.
#include "lab.h"

extern "C" {

__attribute__((visibility("default"))) void* Start() {
    lab::Log("Offset Lab started");
    return reinterpret_cast<void*>(&lab::Draw);
}

__attribute__((visibility("default"))) void InitLate() {
    lab::Log("Offset Lab: game library at 0x%zx", (size_t)lab::GameBase());
}

}
