#pragma once
#include "common.hpp"
#include <array>
#include <algorithm>
#include <iterator>

constexpr int OPTIONS_RESOURCE=105, OPTIONS_DIALOG=300;
constexpr int KEY_FIRST=310, OPTIONS_SCALE=320, OPTIONS_FULL=321, OPTIONS_VOLUME=322;
constexpr int OPTIONS_INTEGER=323, OPTIONS_RESET=324, OPTIONS_STATUS=325;
inline constexpr std::array<const wchar_t*,7> actionNames={L"Up",L"Down",L"Left",L"Right",L"NGPC A",L"NGPC B",L"NGPC Option"};
// Stable, versioned resource / preferences format. The legacy Config stays unchanged.
struct Options {
    uint32_t version=1;
    uint32_t keys[7]={VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT,'Z','X',VK_RETURN};
    uint32_t scale=4, fullscreen=0, volume=100, integerScale=1;
};
static_assert(sizeof(Options)==48,"Options layout");

inline bool assignableKey(uint32_t key) {
    if(key<8 || key>254 || (key>=VK_F1 && key<=VK_F24)) return false;
    switch(key) {
    case VK_TAB: case VK_ESCAPE: case VK_SHIFT: case VK_CONTROL: case VK_MENU:
    case VK_LSHIFT: case VK_RSHIFT: case VK_LCONTROL: case VK_RCONTROL:
    case VK_LMENU: case VK_RMENU: case VK_LWIN: case VK_RWIN: case VK_APPS:
    case VK_PAUSE: case VK_SNAPSHOT: case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL:
        return false;
    default: return MapVirtualKeyW(key,MAPVK_VK_TO_VSC)!=0;
    }
}
inline bool validOptions(const Options& o) {
    if(o.version!=1 || o.scale<2 || o.scale>6 || o.fullscreen>1 || o.volume>100 || o.integerScale>1) return false;
    for(size_t i=0;i<std::size(o.keys);++i) {
        if(!assignableKey(o.keys[i])) return false;
        for(size_t j=0;j<i;++j) if(o.keys[j]==o.keys[i]) return false;
    }
    return true;
}
inline Options defaultOptions(const Config& config) {
    Options o; o.scale=std::clamp(config.scale,2u,6u); o.fullscreen=config.fullscreen?1:0; return o;
}
inline std::wstring keyName(uint32_t key) {
    if((key>='A' && key<='Z') || (key>='0' && key<='9')) return std::wstring(1,static_cast<wchar_t>(key));
    if(key>=VK_NUMPAD0 && key<=VK_NUMPAD9) return L"Num "+std::to_wstring(key-VK_NUMPAD0);
    switch(key) {
    case VK_UP: return L"Up";
    case VK_DOWN: return L"Down";
    case VK_LEFT: return L"Left";
    case VK_RIGHT: return L"Right";
    case VK_RETURN: return L"Enter";
    case VK_SPACE: return L"Space";
    case VK_BACK: return L"Backspace";
    case VK_DELETE: return L"Delete";
    case VK_INSERT: return L"Insert";
    case VK_HOME: return L"Home";
    case VK_END: return L"End";
    case VK_PRIOR: return L"Page Up";
    case VK_NEXT: return L"Page Down";
    case VK_ADD: return L"Num +";
    case VK_SUBTRACT: return L"Num -";
    case VK_MULTIPLY: return L"Num *";
    case VK_DIVIDE: return L"Num /";
    case VK_DECIMAL: return L"Num .";
    case VK_SEPARATOR: return L"Num separator";
    case VK_CLEAR: return L"Clear";
    }
    // Keep punctuation appropriate to the user's layout, without localized key names.
    UINT character=MapVirtualKeyW(key,MAPVK_VK_TO_CHAR)&0x7fffffff;
    if(character>=33 && character<=0xffff) return std::wstring(1,static_cast<wchar_t>(character));
    return L"Key "+std::to_wstring(key);
}
inline Options loadPreferences(const fs::path& path,const Options& fallback) {
    // A missing, corrupt or newer preferences file must never prevent playing.
    try {
        auto data=readFile(path,sizeof(Options)); Options o;
        if(data.size()==sizeof(o)) { memcpy(&o,data.data(),sizeof(o)); if(validOptions(o)) return o; }
    } catch(const std::exception&) {}
    return fallback;
}
inline void storePreferences(const fs::path& path,const Options& o) {
    if(!validOptions(o)) throw std::runtime_error("Invalid settings.");
    auto temp=path; temp+=L".tmp";
    try {
        writeFile(temp,&o,sizeof(o));
        if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Unable to save preferences.");
    } catch(...) { DeleteFileW(temp.c_str()); throw; }
}

namespace options_dialog {
struct State { Options draft,defaults; std::array<WNDPROC,7> oldProcs{}; };
inline State* state(HWND dialog) { return reinterpret_cast<State*>(GetWindowLongPtrW(dialog,DWLP_USER)); }
inline void fill(HWND dialog) {
    auto& o=state(dialog)->draft;
    for(int i=0;i<7;++i) SetDlgItemTextW(dialog,KEY_FIRST+i,keyName(o.keys[i]).c_str());
    SendDlgItemMessageW(dialog,OPTIONS_SCALE,CB_SETCURSEL,o.scale-2,0);
    CheckDlgButton(dialog,OPTIONS_FULL,o.fullscreen?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(dialog,OPTIONS_INTEGER,o.integerScale?BST_CHECKED:BST_UNCHECKED);
    SetDlgItemInt(dialog,OPTIONS_VOLUME,o.volume,FALSE);
}
inline LRESULT CALLBACK keyProcedure(HWND field,UINT msg,WPARAM w,LPARAM l) {
    HWND dialog=GetParent(field); auto* s=state(dialog); int index=GetDlgCtrlID(field)-KEY_FIRST;
    if(msg==WM_GETDLGCODE) {
        // Keep Tab for navigation and Escape for Cancel, but capture Enter as a binding.
        auto* message=reinterpret_cast<MSG*>(l);
        if(message && message->message==WM_KEYDOWN && message->wParam!=VK_TAB && message->wParam!=VK_ESCAPE) return DLGC_WANTALLKEYS;
    }
    if(msg==WM_KEYDOWN && w!=VK_TAB && w!=VK_ESCAPE) {
        if(l&(1LL<<30)) return 0;
        if(!assignableKey(static_cast<uint32_t>(w))) {
            SetDlgItemTextW(dialog,OPTIONS_STATUS,L"Reserved key. Choose a letter, arrow, number, Space..."); return 0;
        }
        for(int i=0;i<7;++i) if(i!=index && s->draft.keys[i]==w) {
            std::wstring text=L"This key is already assigned to: "; text+=actionNames[i];
            SetDlgItemTextW(dialog,OPTIONS_STATUS,text.c_str()); return 0;
        }
        s->draft.keys[index]=static_cast<uint32_t>(w); SetWindowTextW(field,keyName(static_cast<uint32_t>(w)).c_str());
        SendMessageW(field,EM_SETSEL,0,-1); SetDlgItemTextW(dialog,OPTIONS_STATUS,L"Key changed. Click Apply to save your changes."); return 0;
    }
    return CallWindowProcW(s->oldProcs[index],field,msg,w,l);
}
inline INT_PTR CALLBACK procedure(HWND dialog,UINT msg,WPARAM w,LPARAM l) {
    if(msg==WM_INITDIALOG) {
        auto* s=reinterpret_cast<State*>(l); SetWindowLongPtrW(dialog,DWLP_USER,l);
        for(int i=0;i<7;++i) {
            HWND field=GetDlgItem(dialog,KEY_FIRST+i);
            s->oldProcs[i]=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(field,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(keyProcedure)));
        }
        for(auto label:{L"2x - 320 x 304",L"3x - 480 x 456",L"4x - 640 x 608",L"5x - 800 x 760",L"6x - 960 x 912"})
            SendDlgItemMessageW(dialog,OPTIONS_SCALE,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        SendDlgItemMessageW(dialog,OPTIONS_VOLUME,EM_SETLIMITTEXT,3,0); fill(dialog);
        // Center on the owner's monitor, including when the game is in fullscreen.
        RECT owner{},rect{}; GetWindowRect(GetParent(dialog),&owner); GetWindowRect(dialog,&rect);
        MONITORINFO mi{sizeof(mi),{},{},0}; GetMonitorInfoW(MonitorFromWindow(GetParent(dialog),MONITOR_DEFAULTTONEAREST),&mi);
        int width=rect.right-rect.left,height=rect.bottom-rect.top;
        int x=std::max(mi.rcWork.left,std::min(owner.left+(owner.right-owner.left-width)/2,mi.rcWork.right-width));
        int y=std::max(mi.rcWork.top,std::min(owner.top+(owner.bottom-owner.top-height)/2,mi.rcWork.bottom-height));
        SetWindowPos(dialog,nullptr,x,y,0,0,SWP_NOSIZE|SWP_NOZORDER);
        return TRUE;
    }
    if(msg==WM_COMMAND) {
        switch(LOWORD(w)) {
        case OPTIONS_RESET: state(dialog)->draft=state(dialog)->defaults; fill(dialog); SetDlgItemTextW(dialog,OPTIONS_STATUS,L"Original settings restored. Click Apply to use them."); return TRUE;
        case IDOK: {
            auto& o=state(dialog)->draft; BOOL valid=FALSE;
            o.volume=GetDlgItemInt(dialog,OPTIONS_VOLUME,&valid,FALSE);
            o.scale=static_cast<uint32_t>(SendDlgItemMessageW(dialog,OPTIONS_SCALE,CB_GETCURSEL,0,0))+2;
            o.fullscreen=IsDlgButtonChecked(dialog,OPTIONS_FULL)==BST_CHECKED;
            o.integerScale=IsDlgButtonChecked(dialog,OPTIONS_INTEGER)==BST_CHECKED;
            if(!valid || !validOptions(o)) { SetDlgItemTextW(dialog,OPTIONS_STATUS,L"Volume must be a number between 0 and 100."); return TRUE; }
            EndDialog(dialog,IDOK); return TRUE;
        }
        case IDCANCEL: EndDialog(dialog,IDCANCEL); return TRUE;
        }
    }
    if(msg==WM_CLOSE) { EndDialog(dialog,IDCANCEL); return TRUE; }
    return FALSE;
}
}
inline bool editOptions(HWND owner,Options& options,const Options& defaults) {
    options_dialog::State state{options,defaults,{}};
    INT_PTR result=DialogBoxParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(OPTIONS_DIALOG),owner,options_dialog::procedure,reinterpret_cast<LPARAM>(&state));
    if(result==-1) throw std::runtime_error("Unable to open settings.");
    if(result!=IDOK) return false;
    options=state.draft; return true;
}
