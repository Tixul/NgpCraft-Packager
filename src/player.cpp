#include "options.hpp"
#include "video_surface.hpp"
#include "licenses.hpp"
#include "native_core.hpp"
#include <mmsystem.h>
#include <xinput.h>
#include <shlobj.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <array>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace {
HWND windowHandle{};
bool running=true, active=true, paused=false, fullscreen=false, blockInput=false;
bool smoke=false, uiTest=false, loaded=false, saveErrorShown=false;
NativeCore core;
Config config;
Options options,embeddedOptions;
uint32_t lastVolume=100;
HMENU gameMenu{};
enum { MenuOptions=400,MenuPause,MenuSound,MenuFullscreen,MenuControlsHelp,MenuLicences,MenuQuit };
RECT oldRect{};
std::array<uint32_t,160*152> pixels{};
VideoSurface videoSurface;
uint64_t videoCount=0,audioCount=0,inputCount=0;
uint16_t keys=0;
fs::path savePath,preferencesPath;
HANDLE saveLock=INVALID_HANDLE_VALUE;
HWAVEOUT audioDevice{};
struct AudioBuffer { WAVEHDR header{}; std::array<int16_t,8192> samples{}; bool prepared=false; };
std::array<AudioBuffer,8> audioBuffers;
unsigned audioIndex=0;
using GetPad=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
GetPad getPad=nullptr;
HMODULE xinput{};

std::string digest(const std::vector<uint8_t>& data) {
    BCRYPT_ALG_HANDLE alg{}; BCRYPT_HASH_HANDLE hash{}; std::array<UCHAR,32> bytes{};
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) throw std::runtime_error("SHA256 is unavailable.");
    auto result=BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0);
    if(result>=0) result=BCryptHashData(hash,const_cast<PUCHAR>(data.data()),static_cast<ULONG>(data.size()),0);
    if(result>=0) result=BCryptFinishHash(hash,bytes.data(),static_cast<ULONG>(bytes.size()),0);
    if(hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg,0);
    if(result<0) throw std::runtime_error("Unable to calculate SHA256.");
    std::ostringstream s; for(auto b:bytes) s<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b); return s.str();
}
void save() {
    if(!loaded) return;
    try {
        auto data=core.save();
        auto temp=savePath; temp+=L".tmp"; writeFile(temp,data.data(),data.size());
        if(!MoveFileExW(temp.c_str(),savePath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Unable to replace the save file.");
    } catch(const std::exception& e) {
        if(smoke) throw;
        if(!saveErrorShown) { saveErrorShown=true; MessageBoxW(windowHandle,wide(e.what()).c_str(),L"Save failed",MB_ICONERROR); }
    }
}
void video(const void* data,unsigned w,unsigned h,size_t pitch) {
    if(w!=160 || h!=152 || pitch<w*4) return;
    if(data) for(unsigned y=0;y<h;++y) memcpy(pixels.data()+y*w,static_cast<const uint8_t*>(data)+pitch*y,w*4);
    ++videoCount; InvalidateRect(windowHandle,nullptr,FALSE);
}
size_t audio(const int16_t* data,size_t frames) {
    audioCount+=frames;
    if(!audioDevice || !options.volume) return frames;
    auto& b=audioBuffers[audioIndex];
    if(b.prepared && !(b.header.dwFlags&WHDR_DONE)) return frames;
    if(b.prepared) waveOutUnprepareHeader(audioDevice,&b.header,sizeof(b.header));
    b.prepared=false;
    size_t count=std::min(frames,b.samples.size()/2);
    for(size_t i=0;i<count*2;++i) b.samples[i]=static_cast<int16_t>(static_cast<int32_t>(data[i])*static_cast<int32_t>(options.volume)/100);
    b.header={};
    b.header.lpData=reinterpret_cast<char*>(b.samples.data()); b.header.dwBufferLength=static_cast<DWORD>(count*4);
    if(waveOutPrepareHeader(audioDevice,&b.header,sizeof(b.header))==MMSYSERR_NOERROR) {
        b.prepared=true; waveOutWrite(audioDevice,&b.header,sizeof(b.header));
    }
    audioIndex=(audioIndex+1)%audioBuffers.size(); return frames;
}
void poll() {
    keys=0;
    if(!active || paused) return;
    if(blockInput) {
        for(auto key:options.keys) if(GetAsyncKeyState(key)&0x8000) return;
        blockInput=false;
    }
    auto bind=[](int key,int id) { if(GetAsyncKeyState(key)&0x8000) keys|=static_cast<uint16_t>(1u<<id); };
    const int ids[]={0,1,2,3,4,5,6};
    for(int i=0;i<7;++i) bind(options.keys[i],ids[i]);
    if(getPad) {
        XINPUT_STATE s{};
        for(DWORD i=0;i<4;++i) if(getPad(i,&s)==ERROR_SUCCESS) {
            auto pad=[&](WORD mask,int id) { if(s.Gamepad.wButtons&mask) keys|=static_cast<uint16_t>(1u<<id); };
            pad(XINPUT_GAMEPAD_DPAD_UP,0); pad(XINPUT_GAMEPAD_DPAD_DOWN,1);
            pad(XINPUT_GAMEPAD_DPAD_LEFT,2); pad(XINPUT_GAMEPAD_DPAD_RIGHT,3);
            pad(XINPUT_GAMEPAD_A,4); pad(XINPUT_GAMEPAD_B,5); pad(XINPUT_GAMEPAD_START,6);
            if(s.Gamepad.sThumbLX>16000) keys|=1u<<3;
            if(s.Gamepad.sThumbLX<-16000) keys|=1u<<2;
            if(s.Gamepad.sThumbLY>16000) keys|=1u<<0;
            if(s.Gamepad.sThumbLY<-16000) keys|=1u<<1;
            break;
        }
    }
}
void toggleFullscreen() {
    fullscreen=!fullscreen;
    if(fullscreen) {
        GetWindowRect(windowHandle,&oldRect); MONITORINFO mi{sizeof(mi),{},{},0}; GetMonitorInfoW(MonitorFromWindow(windowHandle,MONITOR_DEFAULTTONEAREST),&mi);
        SetMenu(windowHandle,nullptr);
        SetWindowLongPtrW(windowHandle,GWL_STYLE,WS_POPUP|WS_VISIBLE);
        SetWindowPos(windowHandle,HWND_TOP,mi.rcMonitor.left,mi.rcMonitor.top,mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,SWP_FRAMECHANGED);
    } else {
        SetMenu(windowHandle,gameMenu);
        SetWindowLongPtrW(windowHandle,GWL_STYLE,WS_OVERLAPPEDWINDOW|WS_VISIBLE);
        SetWindowPos(windowHandle,nullptr,oldRect.left,oldRect.top,oldRect.right-oldRect.left,oldRect.bottom-oldRect.top,SWP_FRAMECHANGED|SWP_NOZORDER);
    }
}
void updateMenu() {
    CheckMenuItem(gameMenu,MenuPause,MF_BYCOMMAND|(paused?MF_CHECKED:MF_UNCHECKED));
    CheckMenuItem(gameMenu,MenuSound,MF_BYCOMMAND|(!options.volume?MF_CHECKED:MF_UNCHECKED));
    CheckMenuItem(gameMenu,MenuFullscreen,MF_BYCOMMAND|(fullscreen?MF_CHECKED:MF_UNCHECKED));
}
void persistOptions() {
    try { storePreferences(preferencesPath,options); }
    catch(const std::exception& e) { MessageBoxW(windowHandle,wide(e.what()).c_str(),L"Preferences not saved",MB_ICONERROR); }
}
void showOptions() {
    bool wasPaused=paused; paused=true; keys=0;
    if(audioDevice) waveOutReset(audioDevice);
    try {
        Options candidate=options; candidate.fullscreen=fullscreen;
        if(editOptions(windowHandle,candidate,embeddedOptions)) {
            // Commit first; on a write error keep the previous running settings.
            storePreferences(preferencesPath,candidate);
            bool resize=candidate.scale!=options.scale;
            bool desiredFull=candidate.fullscreen!=0;
            if(fullscreen && (resize || !desiredFull)) toggleFullscreen();
            options=candidate;
            if(options.volume) lastVolume=options.volume;
            if(resize) {
                RECT size{0,0,int(160*options.scale),int(152*options.scale)};
                AdjustWindowRect(&size,WS_OVERLAPPEDWINDOW,TRUE);
                SetWindowPos(windowHandle,nullptr,0,0,size.right-size.left,size.bottom-size.top,SWP_NOMOVE|SWP_NOZORDER);
            }
            if(fullscreen!=desiredFull) toggleFullscreen();
            InvalidateRect(windowHandle,nullptr,FALSE);
        }
    } catch(const std::exception& e) { MessageBoxW(windowHandle,wide(e.what()).c_str(),L"Settings",MB_ICONERROR); }
    paused=wasPaused; blockInput=true; updateMenu();
}
void command(unsigned id) {
    switch(id) {
    case MenuOptions: showOptions(); break;
    case MenuPause: paused=!paused; if(audioDevice) waveOutReset(audioDevice); break;
    case MenuSound:
        if(options.volume) { lastVolume=options.volume; options.volume=0; } else options.volume=lastVolume;
        if(audioDevice) waveOutReset(audioDevice);
        persistOptions(); break;
    case MenuFullscreen: toggleFullscreen(); options.fullscreen=fullscreen; persistOptions(); break;
    case MenuControlsHelp: {
        if(audioDevice) waveOutReset(audioDevice);
        std::wstring text;
        for(int i=0;i<7;++i) { text+=actionNames[i]; text+=L" : "; text+=keyName(options.keys[i]); text+=L"\n"; }
        text+=L"\nEsc / F1: controls and settings\nF3: pause / F4: mute\nF11: fullscreen / window\nF5: help / F2: licenses\nAlt+F4: quit\n\nXInput controller: A / B / Start\nPreferences and game saves are stored automatically.";
        MessageBoxW(windowHandle,text.c_str(),L"Controls",MB_OK); blockInput=true; break;
    }
    case MenuLicences: {
        if(audioDevice) waveOutReset(audioDevice);
        try { showLicenses(windowHandle,config.title); }
        catch(const std::exception& e) { MessageBoxW(windowHandle,wide(e.what()).c_str(),L"Licenses",MB_ICONERROR); }
        blockInput=true; break;
    }
    case MenuQuit: running=false; break;
    }
    updateMenu();
}
LRESULT CALLBACK procedure(HWND h,UINT msg,WPARAM w,LPARAM l) {
    switch(msg) {
    case WM_CLOSE: running=false; return 0;
    case WM_ACTIVATEAPP: active=w!=0; keys=0; if(!active && audioDevice) waveOutReset(audioDevice); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_COMMAND: command(LOWORD(w)); return 0;
    case WM_ENTERMENULOOP: if(audioDevice) waveOutReset(audioDevice); keys=0; return 0;
    case WM_EXITMENULOOP: blockInput=true; return 0;
    case WM_KEYDOWN:
        if(l&(1LL<<30)) return 0;
        if(w==VK_F11) command(MenuFullscreen);
        else if(w==VK_ESCAPE || w==VK_F1) command(MenuOptions);
        else if(w==VK_F3) command(MenuPause);
        else if(w==VK_F4) command(MenuSound);
        else if(w==VK_F5) command(MenuControlsHelp);
        else if(w==VK_F2) command(MenuLicences);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps); RECT r; GetClientRect(h,&r);
        if(videoSurface.compose(dc,r.right,r.bottom,pixels.data(),options.integerScale!=0))
            videoSurface.present(dc);
        EndPaint(h,&ps); return 0;
    }
    } return DefWindowProcW(h,msg,w,l);
}
void cleanup() {
    if(audioDevice) { waveOutReset(audioDevice); for(auto& b:audioBuffers) if(b.prepared) waveOutUnprepareHeader(audioDevice,&b.header,sizeof(b.header)); waveOutClose(audioDevice); audioDevice=nullptr; }
    core.close(); loaded=false;
    if(saveLock!=INVALID_HANDLE_VALUE) CloseHandle(saveLock);
    if(xinput) FreeLibrary(xinput);
    if(windowHandle) { SetMenu(windowHandle,nullptr); DestroyWindow(windowHandle); }
    if(gameMenu) DestroyMenu(gameMenu);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    fs::path report;
    try {
        int argc=0; auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
        if(argc==3 && (std::wstring(argv[1])==L"--smoke" || std::wstring(argv[1])==L"--ui-test")) { smoke=true; uiTest=std::wstring(argv[1])==L"--ui-test"; report=argv[2]; } LocalFree(argv);
        auto rom=resource(ROM_RESOURCE), settings=resource(CONFIG_RESOURCE);
        if(settings.size()!=sizeof(config)) throw std::runtime_error("Invalid configuration.");
        memcpy(&config,settings.data(),sizeof(config)); config.title[127]=0;
        if(config.version!=1 || config.scale<1 || config.scale>6 || config.fullscreen>1) throw std::runtime_error("Invalid configuration version.");
        embeddedOptions=defaultOptions(config);
        if(FindResourceW(nullptr,MAKEINTRESOURCEW(OPTIONS_RESOURCE),RT_RCDATA)) {
            auto data=resource(OPTIONS_RESOURCE);
            if(data.size()!=sizeof(Options)) throw std::runtime_error("Invalid embedded settings.");
            memcpy(&embeddedOptions,data.data(),sizeof(Options));
            if(!validOptions(embeddedOptions)) throw std::runtime_error("Invalid embedded settings.");
        }
        fs::path base;
        if(smoke) base=report.parent_path()/L"smoke-saves";
        else {
            PWSTR appData=nullptr;
            if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&appData))) throw std::runtime_error("The save folder is not accessible.");
            base=fs::path(appData)/L"NgpCraft"/L"Games"; CoTaskMemFree(appData);
        }
        base/=digest(rom); fs::create_directories(base); savePath=base/L"save.ngpsav";
        saveLock=CreateFileW((base/L"session.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(saveLock==INVALID_HANDLE_VALUE) throw std::runtime_error("This game is already running, or its save folder is not accessible.");
        preferencesPath=base/L"preferences.dat";
        options=loadPreferences(preferencesPath,embeddedOptions);
        if(options.volume) lastVolume=options.volume;
        WNDCLASSW wc{}; wc.hInstance=instance; wc.lpfnWndProc=procedure; wc.lpszClassName=L"NgpCraftPlayer"; wc.hCursor=LoadCursor(nullptr,IDC_ARROW); wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));
        if(!wc.hIcon) wc.hIcon=LoadIcon(nullptr,IDI_APPLICATION);
        RegisterClassW(&wc);
        gameMenu=CreateMenu(); HMENU submenu=CreatePopupMenu();
        AppendMenuW(submenu,MF_STRING,MenuOptions,L"Controls and settings...\tF1 / Esc");
        AppendMenuW(submenu,MF_STRING,MenuPause,L"Pause\tF3");
        AppendMenuW(submenu,MF_STRING,MenuSound,L"Mute\tF4");
        AppendMenuW(submenu,MF_STRING,MenuFullscreen,L"Fullscreen\tF11");
        AppendMenuW(submenu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(submenu,MF_STRING,MenuControlsHelp,L"Help\tF5");
        AppendMenuW(submenu,MF_STRING,MenuLicences,L"Licenses\tF2");
        AppendMenuW(submenu,MF_STRING,MenuQuit,L"Quit\tAlt+F4");
        AppendMenuW(gameMenu,MF_POPUP,reinterpret_cast<UINT_PTR>(submenu),L"Game");
        RECT size{0,0,int(160*options.scale),int(152*options.scale)}; AdjustWindowRect(&size,WS_OVERLAPPEDWINDOW,TRUE);
        std::wstring title=config.title; title+=L"  |  Esc / F1: settings";
        windowHandle=CreateWindowW(wc.lpszClassName,title.c_str(),WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,size.right-size.left,size.bottom-size.top,nullptr,gameMenu,instance,nullptr);
        if(!windowHandle) throw std::runtime_error("Unable to create the window.");
        core.load(rom); loaded=true;
        if(fs::exists(savePath)) {
            if(!core.restore(readFile(savePath,NativeCore::maxSaveSize)))
                throw std::runtime_error("Invalid game save. Move or remove save.ngpsav to start a new game.");
        }
        WAVEFORMATEX fmt{}; fmt.wFormatTag=WAVE_FORMAT_PCM; fmt.nChannels=2; fmt.nSamplesPerSec=NativeCore::sampleRate; fmt.wBitsPerSample=16; fmt.nBlockAlign=4; fmt.nAvgBytesPerSec=fmt.nSamplesPerSec*4;
        if(!smoke && waveOutOpen(&audioDevice,WAVE_MAPPER,&fmt,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR) { audioDevice=nullptr; MessageBoxW(windowHandle,L"No audio output is available. The game will continue without sound.",L"Audio",MB_ICONINFORMATION); }
        xinput=LoadLibraryExW(L"xinput1_4.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!xinput) xinput=LoadLibraryExW(L"xinput9_1_0.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(xinput) { auto proc=GetProcAddress(xinput,"XInputGetState"); static_assert(sizeof(proc)==sizeof(getPad)); memcpy(&getPad,&proc,sizeof(proc)); }
        if(!smoke || uiTest) { ShowWindow(windowHandle,show); if(options.fullscreen) toggleFullscreen(); }
        updateMenu();
        using Clock=std::chrono::steady_clock;
        auto step=std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0/NativeCore::fps));
        auto next=Clock::now(),lastSave=next;
        unsigned frames=0;
        while(running) {
            MSG msg; while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { if(msg.message==WM_QUIT) running=false; TranslateMessage(&msg); DispatchMessageW(&msg); }
            if(!running) break;
            if((!smoke || uiTest) && (paused || (!uiTest && (!active || IsIconic(windowHandle))))) { MsgWaitForMultipleObjects(0,nullptr,FALSE,30,QS_ALLINPUT); next=Clock::now(); continue; }
            auto now=Clock::now();
            if((!smoke || uiTest) && now<next) { auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(next-now).count(); if(ms>0) MsgWaitForMultipleObjects(0,nullptr,FALSE,static_cast<DWORD>(ms),QS_ALLINPUT); else Sleep(0); continue; }
            poll(); ++inputCount; core.run(static_cast<uint8_t>(keys));
            video(core.video().data(),160,152,160*4); audio(core.audio(),core.audioFrames()); ++frames; next+=step;
            if(Clock::now()-next>step*4) next=Clock::now();
            if(Clock::now()-lastSave>std::chrono::seconds(5)) { save(); lastSave=Clock::now(); }
            if(smoke && !uiTest && frames>=180) break;
        }
        save();
        if(smoke) {
            bool varied=std::any_of(pixels.begin(),pixels.end(),[](uint32_t p){ return p!=pixels[0]; });
            std::string output="frames="+std::to_string(frames)+"\nvideo="+std::to_string(videoCount)+"\naudio="+std::to_string(audioCount)+"\ninput="+std::to_string(inputCount)+"\nvaried="+std::to_string(varied)+"\nsave_bytes="+std::to_string(fs::file_size(savePath))+"\n";
            output+="volume="+std::to_string(options.volume)+"\nscale="+std::to_string(options.scale)+"\nfullscreen="+std::to_string(options.fullscreen)+"\ninteger_scale="+std::to_string(options.integerScale)+"\n";
            for(int i=0;i<7;++i) output+="key"+std::to_string(i)+"="+std::to_string(options.keys[i])+"\n";
            writeFile(report,output.data(),output.size());
            auto ppm=report; ppm+=L".ppm"; std::ofstream f(ppm,std::ios::binary); f<<"P6\n160 152\n255\n"; for(auto p:pixels) { char rgb[]={char(p>>16),char(p>>8),char(p)}; f.write(rgb,3); }
            if(!uiTest && (videoCount<180 || !audioCount || !inputCount)) throw std::runtime_error("Incomplete smoke test.");
        }
        cleanup(); return 0;
    } catch(const std::exception& e) {
        cleanup();
        if(smoke) { std::string message=e.what(); try { writeFile(report,message.data(),message.size()); } catch(...) {} }
        else MessageBoxW(nullptr,wide(e.what()).c_str(),L"NgpCraft Player",MB_ICONERROR);
        return 1;
    }
}
