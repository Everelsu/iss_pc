#pragma once
#include "imgui.h"
namespace TlgPowerArt {
void UpdateTexture();
void Draw(ImDrawList *draw,ImVec2 center,float side,int choice,float dt,double time,float glow);
}
