#include "hud_renderer.h"

namespace hud {

// ======== 轻量 5x7 字体 ========
static const uint8_t FONT5x7[][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x1E,0x11,0x11,0x1F,0x11,0x11,0x11}, // A
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // C
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, // D
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // E
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // F
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // H
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}, // I
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // P
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, // S
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // T
    {0x11,0x11,0x15,0x15,0x15,0x1B,0x11}, // W
};
static int glyph_index(char c){
    switch(c){
        case ' ': return 0; case 'A': return 1; case 'C': return 2; case 'D': return 3;
        case 'E': return 4; case 'F': return 5; case 'H': return 6; case 'I': return 7;
        case 'P': return 8; case 'S': return 9; case 'T': return 10; case 'W': return 11;
        default:  return 0;
    }
}

static inline uint8_t u8clamp(int v){ return (uint8_t)(v<0?0:(v>255?255:v)); }

// ---- BGRA 像素/矩形/文字 ----
static inline void blend_px_bgra(uint8_t* img,int W,int H,int x,int y,
                                 uint8_t r,uint8_t g,uint8_t b,uint8_t a){
    if(x<0||y<0||x>=W||y>=H) return;
    size_t i=((size_t)y*(size_t)W+(size_t)x)*4;
    float af=a/255.f, ia=1.f-af;
    img[i+0]=u8clamp((int)(af*b + ia*img[i+0]));
    img[i+1]=u8clamp((int)(af*g + ia*img[i+1]));
    img[i+2]=u8clamp((int)(af*r + ia*img[i+2]));
    img[i+3]=255;
}
static void fill_rect_bgra(uint8_t* img,int W,int H,int x0,int y0,int w,int h,
                           uint8_t r,uint8_t g,uint8_t b,uint8_t a=255){
    for(int y=y0;y<y0+h;++y) for(int x=x0;x<x0+w;++x) blend_px_bgra(img,W,H,x,y,r,g,b,a);
}
static void rect_outline_bgra(uint8_t* img,int W,int H,int x0,int y0,int w,int h,
                              uint8_t r,uint8_t g,uint8_t b,int thick=2,uint8_t a=255){
    for(int t=0;t<thick;++t){
        int x1=x0+w-1-t, y1=y0+h-1-t;
        for(int x=x0+t;x<=x1;++x){ blend_px_bgra(img,W,H,x,y0+t,r,g,b,a); blend_px_bgra(img,W,H,x,y1,r,g,b,a); }
        for(int y=y0+t;y<=y1;++y){ blend_px_bgra(img,W,H,x0+t,y,r,g,b,a); blend_px_bgra(img,W,H,x1,y,r,g,b,a); }
    }
}
static void draw_char_bgra(uint8_t* img,int W,int H,int x,int y,char c,int scale,
                           uint8_t r,uint8_t g,uint8_t b,uint8_t a=255){
    const uint8_t* g7 = FONT5x7[glyph_index(c)];
    for(int gy=0; gy<7; ++gy){
        uint8_t row=g7[gy];
        for(int gx=0; gx<5; ++gx){
            if(row & (1<<(4-gx))){
                for(int yy=0; yy<scale; ++yy)
                    for(int xx=0; xx<scale; ++xx)
                        blend_px_bgra(img,W,H,x+gx*scale+xx,y+gy*scale+yy,r,g,b,a);
            }
        }
    }
}
static void draw_text_bgra(uint8_t* img,int W,int H,int x,int y,const char* s,int scale,
                           uint8_t r,uint8_t g,uint8_t b,uint8_t a=255){
    int cx=x; for(const char* p=s; *p; ++p){ draw_char_bgra(img,W,H,cx,y,*p,scale,r,g,b,a); cx += (5*scale + scale); }
}

// ---- Gray 像素/矩形/文字 ----
static inline void blend_px_gray(uint8_t* img,int W,int H,int x,int y,uint8_t v,uint8_t a){
    if(x<0||y<0||x>=W||y>=H) return;
    size_t i=(size_t)y*(size_t)W+(size_t)x;
    float af=a/255.f, ia=1.f-af;
    img[i]=u8clamp((int)(af*v + ia*img[i]));
}
static void fill_rect_gray(uint8_t* img,int W,int H,int x0,int y0,int w,int h,uint8_t v,uint8_t a=255){
    for(int y=y0;y<y0+h;++y) for(int x=x0;x<x0+w;++x) blend_px_gray(img,W,H,x,y,v,a);
}
static void rect_outline_gray(uint8_t* img,int W,int H,int x0,int y0,int w,int h,uint8_t v,int thick=2,uint8_t a=255){
    for(int t=0;t<thick;++t){
        int x1=x0+w-1-t, y1=y0+h-1-t;
        for(int x=x0+t;x<=x1;++x){ blend_px_gray(img,W,H,x,y0+t,v,a); blend_px_gray(img,W,H,x,y1,v,a); }
        for(int y=y0+t;y<=y1;++y){ blend_px_gray(img,W,H,x0+t,y,v,a); blend_px_gray(img,W,H,x1,y,v,a); }
    }
}
static void draw_char_gray(uint8_t* img,int W,int H,int x,int y,char c,int scale,uint8_t v,uint8_t a=255){
    const uint8_t* g7 = FONT5x7[glyph_index(c)];
    for(int gy=0; gy<7; ++gy){
        uint8_t row=g7[gy];
        for(int gx=0; gx<5; ++gx){
            if(row & (1<<(4-gx))){
                for(int yy=0; yy<scale; ++yy)
                    for(int xx=0; xx<scale; ++xx)
                        blend_px_gray(img,W,H,x+gx*scale+xx,y+gy*scale+yy,v,a);
            }
        }
    }
}
static void draw_text_gray(uint8_t* img,int W,int H,int x,int y,const char* s,int scale,uint8_t v,uint8_t a=255){
    int cx=x; for(const char* p=s; *p; ++p){ draw_char_gray(img,W,H,cx,y,*p,scale,v,a); cx += (5*scale + scale); }
}

// ---- HUD ----
void draw_keys_bgra(uint8_t* img,int W,int H,uint32_t keymask){
    const int pad   = std::max(20, W/64);
    const int box   = std::max(64, W/24);
    const int gap   = std::max(10, W/160);
    const int thick = std::max(3,  W/480);
    const int font  = std::max(3,  box/10);
    const int hudW = box*3 + gap*2;
    const int hudH = box*2 + gap*2 + box/2 + gap + box/2;
    int ox = pad, oy = H - pad - hudH;

    fill_rect_bgra(img,W,H, ox-6,oy-6, hudW+12, hudH+12, 0,0,0,140);

    auto key = [&](int x,int y,const char* label,bool on){
        fill_rect_bgra(img,W,H,x,y,box,box, on?230:64, on?230:64, on?230:64, 220);
        rect_outline_bgra(img,W,H,x,y,box,box, 255,255,255, thick, 200);
        int tx = x + (box - 5*font)/2, ty = y + (box - 7*font)/2;
        draw_text_bgra(img,W,H, tx,ty, label, font, 0,0,0,255);
    };

    int xW = ox + box + gap;   int yW = oy;
    int xA = ox;               int yA = oy + box + gap;
    int xS = ox + box + gap;   int yS = yA;
    int xD = ox + (box+gap)*2; int yD = yA;
    key(xW,yW,"W", (keymask&KM_W)!=0);
    key(xA,yA,"A", (keymask&KM_A)!=0);
    key(xS,yS,"S", (keymask&KM_S)!=0);
    key(xD,yD,"D", (keymask&KM_D)!=0);

    auto bar = [&](int x,int y,int w,int h,const char* label,bool on){
        fill_rect_bgra(img,W,H,x,y,w,h, on?230:64, on?230:64, on?230:64, 220);
        rect_outline_bgra(img,W,H,x,y,w,h,255,255,255, thick,200);
        int tx = x + 8, ty = y + (h - 7*font)/2;
        draw_text_bgra(img,W,H, tx,ty, label, font, 0,0,0,255);
    };
    int shY = yA + box + gap, shH = box/2;
    int spY = shY + shH + gap, spH = box/2;
    bar(ox, shY, hudW, shH, "Shift", (keymask&KM_SHIFT)!=0);
    bar(ox, spY, hudW, spH, "Space", (keymask&KM_SPACE)!=0);
}

void draw_keys_gray(uint8_t* img,int W,int H,uint32_t keymask){
    const int pad   = std::max(20, W/64);
    const int box   = std::max(64, W/24);
    const int gap   = std::max(10, W/160);
    const int thick = std::max(3,  W/480);
    const int font  = std::max(3,  box/10);
    const int hudW = box*3 + gap*2;
    const int hudH = box*2 + gap*2 + box/2 + gap + box/2;
    int ox = pad, oy = H - pad - hudH;

    fill_rect_gray(img,W,H, ox-6,oy-6, hudW+12, hudH+12, 0, 140);

    auto key = [&](int x,int y,const char* label,bool on){
        fill_rect_gray(img,W,H,x,y,box,box, on?230:64, 220);
        rect_outline_gray(img,W,H,x,y,box,box, 255, thick, 200);
        int tx = x + (box - 5*font)/2, ty = y + (box - 7*font)/2;
        draw_text_gray(img,W,H, tx,ty, label, font, 0,255);
    };

    int xW = ox + box + gap;   int yW = oy;
    int xA = ox;               int yA = oy + box + gap;
    int xS = ox + box + gap;   int yS = yA;
    int xD = ox + (box+gap)*2; int yD = yA;
    key(xW,yW,"W", (keymask&KM_W)!=0);
    key(xA,yA,"A", (keymask&KM_A)!=0);
    key(xS,yS,"S", (keymask&KM_S)!=0);
    key(xD,yD,"D", (keymask&KM_D)!=0);

    auto bar = [&](int x,int y,int w,int h,const char* label,bool on){
        fill_rect_gray(img,W,H,x,y,w,h, on?230:64, 220);
        rect_outline_gray(img,W,H,x,y,w,h,255, thick,200);
        int tx = x + 8, ty = y + (h - 7*font)/2;
        draw_text_gray(img,W,H, tx,ty, label, font, 0,255);
    };
    int shY = yA + box + gap, shH = box/2;
    int spY = shY + shH + gap, spH = box/2;
    bar(ox, shY, hudW, shH, "Shift", (keymask&KM_SHIFT)!=0);
    bar(ox, spY, hudW, spH, "Space", (keymask&KM_SPACE)!=0);
}

} // namespace hud
