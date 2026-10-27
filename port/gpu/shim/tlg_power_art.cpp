#include "tlg_power_art.h"
#include "tlg_png.h"
#include "imgui_internal.h"
#include "../../src/power_wheel_visual.h"
#include "../../src/power_wheel_art_mask.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#ifdef _WIN32
asm(".section .rdata,\"dr\"\n.balign 16\n.global iss_wheel_png\niss_wheel_png:\n.incbin \"" TLG_POWER_WHEEL_PATH "\"\n.global iss_wheel_png_end\niss_wheel_png_end:\n.text\n");
#else
asm(".section .rodata\n.balign 16\n.hidden iss_wheel_png\n.global iss_wheel_png\niss_wheel_png:\n.incbin \"" TLG_POWER_WHEEL_PATH "\"\n.hidden iss_wheel_png_end\n.global iss_wheel_png_end\niss_wheel_png_end:\n.previous\n");
#endif
extern "C" const unsigned char iss_wheel_png[],iss_wheel_png_end[];
namespace TlgPowerArt {
namespace {
ImTextureData *texture=nullptr,*highlight[4][3]{};
bool attempted=false;
PwHoverFade fade{-1,0};
// The approved package palette, ordered to match runtime power IDs.
constexpr int rgb[4][3]={{245,44,54},{197,70,239},{8,199,255},{249,202,36}};
ImU32 Color(int index,float alpha){
    return IM_COL32(rgb[index][0],rgb[index][1],rgb[index][2],
        int(255*std::clamp(alpha,0.0f,1.0f)));
}
ImTextureData *Register(int width,int height,const unsigned char *rgba){
    auto *t=IM_NEW(ImTextureData)();t->Create(ImTextureFormat_RGBA32,width,height);
    std::memcpy(t->GetPixels(),rgba,size_t(width)*height*4);
    ImGui::RegisterUserTexture(t);return t;
}
}
void UpdateTexture(){
    if(attempted)return;
    attempted=true;TlgPng::Image image;
    if(!TlgPng::DecodeRgba(iss_wheel_png,size_t(iss_wheel_png_end-iss_wheel_png),image))return;
    // Display the original RGBA bytes unchanged: all baked icons, graffiti,
    // splatters and transparent pixels remain part of the approved asset.
    texture=Register(image.width,image.height,image.rgba.data());
    constexpr int size=512;
    std::vector<unsigned char> pixels(size*size*4);
    auto source=[&](float u,float v,int channel){
        int x=std::clamp(int(u*image.width),0,image.width-1);
        int y=std::clamp(int(v*image.height),0,image.height-1);
        return image.rgba[(y*image.width+x)*4+channel]/255.0f;
    };
    for(int sector=0;sector<4;sector++)for(int layer=0;layer<3;layer++){
        for(int y=0;y<size;y++)for(int x=0;x<size;x++){
            float u=(x+0.5f)/size,v=(y+0.5f)/size,dx=u-0.5f,dy=v-0.5f;
            float radius=std::hypot(dx,dy);
            float alpha=pw_art_sector_mask(sector,dx,dy)*pw_art_layer(layer,radius);
            if(layer<2){
                // Alpha clips overlays to the actual distressed painted pixels.
                alpha*=source(u,v,3);
                if(layer==1){
                    float luminance=std::max({source(u,v,0),source(u,v,1),source(u,v,2)});
                    alpha*=0.35f+0.65f*luminance;
                }
            }else if(radius>0){
                // A sparse, soft halo follows the painted rim; never crosses
                // separator angles. No rectangular sector overlay or opaque fill.
                float rimAlpha=0;
                for(int j=-2;j<=2;j++){
                    float r=0.477f+j*0.005f;
                    rimAlpha+=source(0.5f+dx/radius*r,0.5f+dy/radius*r,3);
                }
                alpha*=rimAlpha/5;
            }
            size_t i=size_t(y*size+x)*4;
            pixels[i]=pixels[i+1]=pixels[i+2]=255;
            pixels[i+3]=static_cast<unsigned char>(std::clamp(alpha,0.0f,1.0f)*255);
        }
        highlight[sector][layer]=Register(size,size,pixels.data());
    }
}
void Draw(ImDrawList *draw,ImVec2 center,float side,int choice,float dt,double time,float glow){
    if(!texture||!std::isfinite(side)||side<=0)return;
    const ImVec2 top(center.x-side*0.5f,center.y-side*0.5f);
    const ImVec2 bottom(top.x+side,top.y+side);
    draw->AddImage(texture->GetTexRef(),top,bottom);
    pw_hover_step(&fade,choice,dt);
    if(fade.shown<0||fade.amount<=0)return;
    const int index=fade.shown;
    const float pulse=0.76f+0.24f*std::sin(float(time)*4.0f);
    const float amount=fade.amount*pulse;
    const float strength=std::isfinite(glow)?std::clamp(glow,0.5f,3.0f):2.04f;
    // Only selected-sector textures are submitted; other quadrants stay unchanged.
    draw->AddImage(highlight[index][0]->GetTexRef(),top,bottom,ImVec2(0,0),ImVec2(1,1),Color(index,0.065f*amount));
    draw->AddImage(highlight[index][2]->GetTexRef(),top,bottom,ImVec2(0,0),ImVec2(1,1),Color(index,0.25f*amount*strength));
    draw->AddImage(highlight[index][1]->GetTexRef(),top,bottom,ImVec2(0,0),ImVec2(1,1),Color(index,0.42f*amount*strength));
}
}
