#include "options.hpp"
#include "licenses.hpp"
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>

namespace {
HWND mainWindow{},romEdit{},nameEdit{},iconEdit{},scaleBox{},fullCheck{},statusText{},buildButton{};
HFONT font{},headingFont{};
enum { BrowseRom=10,BrowseIcon,Build,RomField,EditOptions,EditGameLicense };
Options gameOptions;
GameLicense gameLicense;
std::wstring textOf(HWND h) { int n=GetWindowTextLengthW(h); std::wstring s(n+1,0); GetWindowTextW(h,s.data(),n+1); s.resize(n); return s; }
std::wstring chooseFile(HWND parent,bool save,const wchar_t* filter,const wchar_t* extension,const std::wstring& initial=L"") {
    std::vector<wchar_t> path(32768); if(initial.size()<path.size()) std::copy(initial.begin(),initial.end(),path.begin());
    OPENFILENAMEW d{}; d.lStructSize=sizeof(d); d.hwndOwner=parent; d.lpstrFilter=filter; d.lpstrFile=path.data(); d.nMaxFile=static_cast<DWORD>(path.size()); d.lpstrDefExt=extension;
    d.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    if(save?GetSaveFileNameW(&d):GetOpenFileNameW(&d)) return path.data();
    return {};
}
void update(HANDLE h,LPCWSTR type,int id,const void* p,size_t n) {
    if(n>0xffffffffu || !UpdateResourceW(h,type,MAKEINTRESOURCEW(id),MAKELANGID(LANG_NEUTRAL,SUBLANG_NEUTRAL),const_cast<void*>(p),static_cast<DWORD>(n))) throw std::runtime_error("Unable to update executable resources.");
}
uint16_t u16(const uint8_t* p) { return uint16_t(p[0])|uint16_t(p[1])<<8; }
uint32_t u32(const uint8_t* p) { return uint32_t(u16(p))|uint32_t(u16(p+2))<<16; }
void addIcon(HANDLE handle,const fs::path& path) {
    auto ico=readFile(path,4*1024*1024);
    if(ico.size()<6 || u16(ico.data())!=0 || u16(ico.data()+2)!=1) throw std::runtime_error("The icon must be a valid .ico file.");
    unsigned count=u16(ico.data()+4);
    if(!count || count>128 || ico.size()<6+count*16) throw std::runtime_error("Invalid ICO directory.");
    std::vector<uint8_t> group(6+count*14); memcpy(group.data(),ico.data(),6);
    for(unsigned i=0;i<count;++i) {
        auto entry=ico.data()+6+i*16; uint32_t size=u32(entry+8),offset=u32(entry+12);
        if(!size || offset<6+count*16 || offset>ico.size() || size>ico.size()-offset) throw std::runtime_error("Truncated ICO image.");
        update(handle,RT_ICON,1+i,ico.data()+offset,size);
        auto out=group.data()+6+i*14; memcpy(out,entry,12); out[12]=uint8_t(i+1); out[13]=0;
    }
    update(handle,RT_GROUP_ICON,1,group.data(),group.size());
}
void package(const fs::path& romPath,const fs::path& destination,const Config& cfg,const Options& options,const fs::path& icon,bool overwrite) {
    if(cfg.version!=1 || cfg.scale<1 || cfg.scale>6 || cfg.fullscreen>1 || !cfg.title[0]) throw std::runtime_error("Invalid settings.");
    if(!validOptions(options)) throw std::runtime_error("Invalid keys or settings (duplicate or reserved key).");
    auto licenseData=serializeGameLicense(gameLicense);
    auto rom=readFile(romPath,4*1024*1024);
    if(rom.size()<64) throw std::runtime_error("The ROM must contain at least 64 bytes (maximum 4 MiB).");
    auto out=fs::absolute(destination);
    if(out.extension()!=L".exe" && out.extension()!=L".EXE") throw std::runtime_error("The output must be an .exe file.");
    if(fs::exists(out)) {
        if(fs::equivalent(out,romPath) || fs::equivalent(out,executablePath()) || (!icon.empty() && fs::equivalent(out,icon))) throw std::runtime_error("The output cannot replace a source file.");
        if(!overwrite) throw std::runtime_error("The output file already exists.");
    }
    if(!fs::is_directory(out.parent_path())) throw std::runtime_error("The output folder does not exist.");
    auto player=resource(PLAYER_RESOURCE);
    wchar_t temp[MAX_PATH]{};
    if(!GetTempFileNameW(out.parent_path().c_str(),L"ngp",0,temp)) throw std::runtime_error("Unable to create a temporary file in this folder.");
    HANDLE handle=nullptr;
    try {
        writeFile(temp,player.data(),player.size());
        handle=BeginUpdateResourceW(temp,FALSE); if(!handle) throw std::runtime_error("Unable to open the embedded player.");
        update(handle,RT_RCDATA,ROM_RESOURCE,rom.data(),rom.size()); update(handle,RT_RCDATA,CONFIG_RESOURCE,&cfg,sizeof(cfg));
        update(handle,RT_RCDATA,OPTIONS_RESOURCE,&options,sizeof(options));
        update(handle,RT_RCDATA,GAME_LICENSE_RESOURCE,licenseData.data(),licenseData.size());
        if(!icon.empty()) addIcon(handle,icon);
        HANDLE commit=handle; handle=nullptr;
        if(!EndUpdateResourceW(commit,FALSE)) throw std::runtime_error("Unable to finalize the executable.");
        if(!MoveFileExW(temp,out.c_str(),MOVEFILE_WRITE_THROUGH|(overwrite?MOVEFILE_REPLACE_EXISTING:0))) throw std::runtime_error("Unable to save the EXE. Make sure it is not already running.");
    } catch(...) { if(handle) EndUpdateResourceW(handle,TRUE); DeleteFileW(temp); throw; }
}
Config makeConfig(const std::wstring& title,unsigned scale,bool full) {
    if(title.empty() || title.size()>127) throw std::runtime_error("The game title must contain 1 to 127 characters.");
    Config c; c.scale=scale; c.fullscreen=full; std::copy(title.begin(),title.end(),c.title); return c;
}
HWND control(LPCWSTR cls,LPCWSTR title,DWORD style,int x,int y,int w,int h,int id=0) {
    auto c=CreateWindowExW(!wcscmp(cls,L"EDIT")?WS_EX_CLIENTEDGE:0,cls,title,WS_CHILD|WS_VISIBLE|style,x,y,w,h,mainWindow,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE); return c;
}
LRESULT CALLBACK procedure(HWND h,UINT msg,WPARAM w,LPARAM l) {
    if(msg==WM_COMMAND) {
        try {
            switch(LOWORD(w)) {
            case BrowseRom: {
                auto p=chooseFile(h,false,L"Neo Geo Pocket ROM\0*.ngp;*.ngc;*.npc;*.bin\0All files\0*.*\0",L"ngc");
                if(!p.empty()) { SetWindowTextW(romEdit,p.c_str()); if(textOf(nameEdit).empty()) SetWindowTextW(nameEdit,fs::path(p).stem().c_str()); } break;
            }
            case BrowseIcon: { auto p=chooseFile(h,false,L"Windows icon\0*.ico\0",L"ico"); if(!p.empty()) SetWindowTextW(iconEdit,p.c_str()); break; }
            case EditOptions: {
                gameOptions.scale=unsigned(SendMessageW(scaleBox,CB_GETCURSEL,0,0))+2;
                gameOptions.fullscreen=SendMessageW(fullCheck,BM_GETCHECK,0,0)==BST_CHECKED;
                if(editOptions(h,gameOptions,Options{})) {
                    SendMessageW(scaleBox,CB_SETCURSEL,gameOptions.scale-2,0);
                    SendMessageW(fullCheck,BM_SETCHECK,gameOptions.fullscreen?BST_CHECKED:BST_UNCHECKED,0);
                    SetWindowTextW(statusText,L"Controls and settings are ready for the next export.");
                }
                break;
            }
            case Build: {
                auto title=textOf(nameEdit),rom=textOf(romEdit);
                if(rom.empty()) throw std::runtime_error("Choose a ROM.");
                auto cfg=makeConfig(title,unsigned(SendMessageW(scaleBox,CB_GETCURSEL,0,0))+2,SendMessageW(fullCheck,BM_GETCHECK,0,0)==BST_CHECKED);
                gameOptions.scale=cfg.scale; gameOptions.fullscreen=cfg.fullscreen;
                std::wstring filename=title; for(auto& c:filename) if(c<32 || std::wstring(L"<>:\"/\\|?*").find(c)!=std::wstring::npos) c=L'_';
                auto output=chooseFile(h,true,L"Windows application\0*.exe\0",L"exe",filename+L".exe"); if(output.empty()) break;
                EnableWindow(buildButton,FALSE); SetWindowTextW(statusText,L"Creating executable..."); UpdateWindow(h);
                try { package(rom,output,cfg,gameOptions,textOf(iconEdit),true); } catch(...) { EnableWindow(buildButton,TRUE); throw; }
                EnableWindow(buildButton,TRUE); SetWindowTextW(statusText,L"EXE created: ready to play and share.");
                MessageBoxW(h,(L"Standalone game created:\n\n"+output+L"\n\nNo additional installation required for the player.\nPress Esc or F1 in the game to open settings.").c_str(),L"Export complete",MB_ICONINFORMATION); break;
            }
            case EditGameLicense:
                if(editGameLicense(h,gameLicense)) SetWindowTextW(statusText,L"Game credits and license will be included in the export (F2 in-game).");
                break;
            } return 0;
        } catch(const std::exception& e) { SetWindowTextW(statusText,L"Export failed. Fix the issue and try again."); MessageBoxW(h,wide(e.what()).c_str(),L"NgpCraft Packager",MB_ICONERROR); return 0; }
    }
    if(msg==WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h,msg,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    int argc=0; auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    std::vector<std::wstring> args; for(int i=1;i<argc;++i) args.emplace_back(argv[i]); LocalFree(argv);
    if(!args.empty()) {
        try {
            if(args.size()<3 || args[0]!=L"--pack") throw std::runtime_error("Usage: --pack ROM OUTPUT.exe [--title NAME] [--scale 2..6] [--fullscreen] [--icon FILE.ico] [--keys up,down,left,right,a,b,option] [--volume 0..100] [--fit] [--game-author NAME] [--game-copyright TEXT] [--game-license-name NAME] [--game-license FILE.txt] [--force]");
            std::wstring title=fs::path(args[1]).stem().wstring(); unsigned scale=4; bool full=false,force=false; fs::path icon;
            for(size_t i=3;i<args.size();++i) {
                if(args[i]==L"--fullscreen") full=true;
                else if(args[i]==L"--force") force=true;
                else if(args[i]==L"--title" && i+1<args.size()) title=args[++i];
                else if(args[i]==L"--icon" && i+1<args.size()) icon=args[++i];
                else if(args[i]==L"--game-author" && i+1<args.size()) gameLicense.author=args[++i];
                else if(args[i]==L"--game-copyright" && i+1<args.size()) gameLicense.copyright=args[++i];
                else if(args[i]==L"--game-license-name" && i+1<args.size()) gameLicense.name=args[++i];
                else if(args[i]==L"--game-license" && i+1<args.size()) gameLicense.terms=readLicenseFile(args[++i]);
                else if(args[i]==L"--scale" && i+1<args.size()) { auto value=args[++i]; if(value.size()!=1 || value[0]<L'2' || value[0]>L'6') throw std::runtime_error("Window scale must be between 2 and 6."); scale=value[0]-L'0'; }
                else if(args[i]==L"--volume" && i+1<args.size()) {
                    auto value=args[++i];
                    if(value.empty() || value.size()>3 || value.find_first_not_of(L"0123456789")!=std::wstring::npos) throw std::runtime_error("Volume must be between 0 and 100.");
                    gameOptions.volume=static_cast<uint32_t>(std::stoul(value));
                }
                else if(args[i]==L"--fit") gameOptions.integerScale=0;
                else if(args[i]==L"--keys" && i+1<args.size()) {
                    auto value=args[++i]; size_t start=0;
                    for(int k=0;k<7;++k) {
                        auto end=value.find(L',',start); auto part=value.substr(start,end==std::wstring::npos?end:end-start);
                        if(part.empty() || part.size()>3 || part.find_first_not_of(L"0123456789")!=std::wstring::npos || (k<6)==(end==std::wstring::npos)) throw std::runtime_error("--keys requires 7 decimal Windows virtual-key codes separated by commas.");
                        gameOptions.keys[k]=static_cast<uint32_t>(std::stoul(part)); start=end+1;
                    }
                }
                else throw std::runtime_error("Unknown or incomplete argument.");
            }
            gameOptions.scale=scale; gameOptions.fullscreen=full;
            package(args[1],args[2],makeConfig(title,scale,full),gameOptions,icon,force); return 0;
        } catch(const std::exception& e) { fprintf(stderr,"%s\n",e.what()); return 1; }
    }
    font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    headingFont=CreateFontW(-25,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    WNDCLASSW wc{}; wc.hInstance=instance; wc.lpfnWndProc=procedure; wc.lpszClassName=L"NgpCraftPackager"; wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1); wc.hCursor=LoadCursor(nullptr,IDC_ARROW); wc.hIcon=LoadIcon(nullptr,IDI_APPLICATION); RegisterClassW(&wc);
    RECT size{0,0,680,490}; DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX; AdjustWindowRect(&size,style,FALSE);
    mainWindow=CreateWindowW(wc.lpszClassName,L"NgpCraft Packager",style,CW_USEDEFAULT,CW_USEDEFAULT,size.right-size.left,size.bottom-size.top,nullptr,nullptr,instance,nullptr);
    auto heading=control(L"STATIC",L"NgpCraft Packager",0,24,20,600,36); SendMessageW(heading,WM_SETFONT,reinterpret_cast<WPARAM>(headingFont),TRUE);
    control(L"STATIC",L"Your Neo Geo Pocket game in a single Windows executable.",0,24,62,630,24);
    control(L"STATIC",L"Game ROM",0,24,108,110,24);
    romEdit=control(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,142,103,396,29,RomField);
    control(L"BUTTON",L"Browse...",WS_TABSTOP,548,102,106,30,BrowseRom);
    control(L"STATIC",L"Game title",0,24,152,115,24); nameEdit=control(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,142,147,512,29); SendMessageW(nameEdit,EM_SETLIMITTEXT,127,0);
    control(L"STATIC",L"Icon (optional)",0,24,196,118,24); iconEdit=control(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,142,191,396,29);
    control(L"BUTTON",L"Browse...",WS_TABSTOP,548,190,106,30,BrowseIcon);
    control(L"STATIC",L"Window size",0,24,240,110,24); scaleBox=control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,142,235,196,180);
    for(auto label:{L"2x  -  320 x 304",L"3x  -  480 x 456",L"4x  -  640 x 608",L"5x  -  800 x 760",L"6x  -  960 x 912"}) SendMessageW(scaleBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
    SendMessageW(scaleBox,CB_SETCURSEL,2,0); fullCheck=control(L"BUTTON",L"Start in fullscreen",WS_TABSTOP|BS_AUTOCHECKBOX,366,235,290,28);
    control(L"BUTTON",L"Controls and settings...",WS_TABSTOP,24,282,240,34,EditOptions);
    control(L"STATIC",L"Also available in-game with Esc or F1.",0,280,290,374,26);
    control(L"BUTTON",L"Game credits and license...",WS_TABSTOP,24,328,240,34,EditGameLicense);
    control(L"STATIC",L"Optional author, copyright and license text.",0,280,336,374,26);
    control(L"STATIC",L"Keyboard, XInput controller and automatic saves.",0,24,378,630,22);
    buildButton=control(L"BUTTON",L"Create EXE",WS_TABSTOP|BS_DEFPUSHBUTTON,440,398,214,42,Build);
    statusText=control(L"STATIC",L"Choose a ROM to get started.",0,24,456,632,24);
    ShowWindow(mainWindow,show); MSG msg; while(GetMessageW(&msg,nullptr,0,0)>0) if(!IsDialogMessageW(mainWindow,&msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    DeleteObject(font); DeleteObject(headingFont); return 0;
}
