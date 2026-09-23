#pragma once
#include <windows.h>
#include <algorithm>
#include <cstdint>

// Compose the image and its margins offscreen. Never clear the window itself:
// a compositor/display update between a visible clear and StretchDIBits would
// expose a black image that was never emitted by the emulation core.
class VideoSurface {
    HDC memory_{};
    HBITMAP bitmap_{};
    HGDIOBJ original_{};
    int width_=0,height_=0;
    bool ready_=false;

    bool resize(HDC reference,int width,int height) {
        if(!memory_) memory_=CreateCompatibleDC(reference);
        if(!memory_) return false;
        if(bitmap_ && width_==width && height_==height) return true;
        HBITMAP replacement=CreateCompatibleBitmap(reference,width,height);
        if(!replacement) return false;
        HGDIOBJ previous=SelectObject(memory_,replacement);
        if(!previous || previous==HGDI_ERROR) { DeleteObject(replacement); return false; }
        if(bitmap_) DeleteObject(bitmap_); else original_=previous;
        bitmap_=replacement; width_=width; height_=height;
        return true;
    }
public:
    VideoSurface()=default;
    VideoSurface(const VideoSurface&)=delete;
    VideoSurface& operator=(const VideoSurface&)=delete;
    ~VideoSurface() {
        if(memory_) {
            if(original_) SelectObject(memory_,original_);
            if(bitmap_) DeleteObject(bitmap_);
            DeleteDC(memory_);
        }
    }
    bool compose(HDC reference,int width,int height,const uint32_t* pixels,bool integerScale) {
        ready_=false;
        if(!pixels || width<=0 || height<=0 || !resize(reference,width,height)) return false;
        RECT bounds{0,0,width,height};
        if(!FillRect(memory_,&bounds,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)))) return false;
        int factor=std::min(width/160,height/152),imageWidth,imageHeight;
        if(integerScale && factor>=1) { imageWidth=160*factor; imageHeight=152*factor; }
        else {
            double scale=std::min(width/160.0,height/152.0);
            imageWidth=std::max(1,int(160*scale)); imageHeight=std::max(1,int(152*scale));
        }
        BITMAPINFO info{};
        info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=160; info.bmiHeader.biHeight=-152;
        info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
        info.bmiHeader.biCompression=BI_RGB;
        SetStretchBltMode(memory_,COLORONCOLOR);
        int drawn=StretchDIBits(memory_,(width-imageWidth)/2,(height-imageHeight)/2,imageWidth,imageHeight,
                              0,0,160,152,pixels,&info,DIB_RGB_COLORS,SRCCOPY);
        ready_=drawn!=0 && static_cast<DWORD>(drawn)!=GDI_ERROR;
        return ready_;
    }
    bool present(HDC target) const {
        return ready_ && BitBlt(target,0,0,width_,height_,memory_,0,0,SRCCOPY)!=0;
    }
};
