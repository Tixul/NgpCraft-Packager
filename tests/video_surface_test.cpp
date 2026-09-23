#include "video_surface.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

void check(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
struct Target {
    HDC dc=CreateCompatibleDC(nullptr);
    HBITMAP bitmap{}; HGDIOBJ original{};
    Target() {
        BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=900; info.bmiHeader.biHeight=-800;
        info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
        void* pixels{}; bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        check(dc && bitmap,"test target allocation failed"); original=SelectObject(dc,bitmap);
    }
    ~Target() { SelectObject(dc,original); DeleteObject(bitmap); DeleteDC(dc); }
};
int wmain() {
    try {
        Target target; std::array<uint32_t,160*152> pixels{};
        DWORD handlesBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
        {
            VideoSurface surface;
            // Alternate colors and sizes while composing the next image. The display
            // must retain its old image until the single presentation operation.
            for(int i=0;i<240;++i) {
                int width=(i%3==0?640:i%3==1?853:100),height=(i%3==0?608:i%3==1?701:95);
                bool integerScale=(i%2)==0;
                COLORREF marker=RGB(255,0,255); SetPixel(target.dc,width/2,height/2,marker);
                uint32_t color=i%2?0x00ff5500:0x000088ff; pixels.fill(color);
                check(surface.compose(target.dc,width,height,pixels.data(),integerScale),"compose failed");
                check(GetPixel(target.dc,width/2,height/2)==marker,"composition touched the visible target before present");
                check(surface.present(target.dc),"present failed");
                check(GetPixel(target.dc,width/2,height/2)==RGB((color>>16)&255,(color>>8)&255,color&255),"wrong image or black flash at presentation");
                if(width==853) check(GetPixel(target.dc,0,0)==RGB(0,0,0),"letterbox margins were not cleared");
            }
            // A genuinely black emulated image must still be displayed (no frame filtering).
            pixels.fill(0); check(surface.compose(target.dc,640,608,pixels.data(),true),"black compose failed");
            check(surface.present(target.dc),"black present failed");
            check(GetPixel(target.dc,320,304)==RGB(0,0,0),"real black frame was suppressed");
            // Check top-down orientation and channels on a 1:1 image.
            pixels.fill(0x0000ff00); pixels[0]=0x00ff0000; pixels.back()=0x000000ff;
            check(surface.compose(target.dc,160,152,pixels.data(),true),"orientation compose failed");
            check(surface.present(target.dc),"orientation present failed");
            check(GetPixel(target.dc,0,0)==RGB(255,0,0),"wrong top-left pixel");
            check(GetPixel(target.dc,159,151)==RGB(0,0,255),"wrong bottom-right pixel");
            check(!surface.compose(target.dc,0,0,pixels.data(),true),"zero-sized client should be skipped");
            check(!surface.present(target.dc),"invalid composition was presented");
        }
        check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==handlesBefore,"GDI resources leaked on resize/destruction");
        std::cout<<"PASS: offscreen composition, single presentation, scaling, resize, black frames and GDI lifetime\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
