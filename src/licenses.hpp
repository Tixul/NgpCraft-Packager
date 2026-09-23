#pragma once
#include "common.hpp"
#include <commdlg.h>
#include <array>

constexpr int GAME_LICENSE_RESOURCE=106;
struct GameLicense { std::wstring author,copyright,name,terms; };
inline std::wstring licenseText(HWND h) {
    int length=GetWindowTextLengthW(h); std::wstring text(length+1,0);
    GetWindowTextW(h,text.data(),length+1); text.resize(length); return text;
}
inline std::wstring decodeLicenseUtf8(const std::string& bytes) {
    size_t start=bytes.compare(0,3,"\xef\xbb\xbf")==0?3:0;
    if(bytes.find('\0')!=std::string::npos) throw std::runtime_error("License text must not contain null characters.");
    if(start==bytes.size()) return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data()+start,static_cast<int>(bytes.size()-start),nullptr,0);
    if(!n) throw std::runtime_error("License text must be a UTF-8 text file.");
    std::wstring result(n,0);
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data()+start,static_cast<int>(bytes.size()-start),result.data(),n);
    return result;
}
inline std::string encodeLicenseUtf8(const std::wstring& text) {
    if(text.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    if(!n) throw std::runtime_error("Invalid characters in game credits or license.");
    std::string bytes(n,0); WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),bytes.data(),n,nullptr,nullptr);
    return bytes;
}
inline void validateGameLicense(const GameLicense& info) {
    if(info.author.size()>256 || info.copyright.size()>512 || info.name.size()>128 || info.terms.size()>32768)
        throw std::runtime_error("Game credits or license are too long (license text: 32768 characters maximum).");
    for(auto* field:{&info.author,&info.copyright,&info.name,&info.terms})
        if(field->find(L'\0')!=std::wstring::npos) throw std::runtime_error("Game credits or license contain null characters.");
    for(auto* field:{&info.author,&info.copyright,&info.name})
        if(field->find_first_of(L"\r\n\t")!=std::wstring::npos) throw std::runtime_error("Author, copyright and license name must each fit on one line.");
}
inline std::vector<uint8_t> serializeGameLicense(const GameLicense& info) {
    validateGameLicense(info);
    std::array<std::string,4> fields={encodeLicenseUtf8(info.author),encodeLicenseUtf8(info.copyright),encodeLicenseUtf8(info.name),encodeLicenseUtf8(info.terms)};
    uint32_t header[5]={1,0,0,0,0};
    for(size_t i=0;i<fields.size();++i) header[i+1]=static_cast<uint32_t>(fields[i].size());
    std::vector<uint8_t> bytes(sizeof(header)); memcpy(bytes.data(),header,sizeof(header));
    for(const auto& field:fields) bytes.insert(bytes.end(),field.begin(),field.end());
    return bytes;
}
inline GameLicense readGameLicense() {
    GameLicense info;
    if(!FindResourceW(nullptr,MAKEINTRESOURCEW(GAME_LICENSE_RESOURCE),RT_RCDATA)) return info;
    auto bytes=resource(GAME_LICENSE_RESOURCE); uint32_t header[5]{};
    if(bytes.size()<sizeof(header) || bytes.size()>140000) throw std::runtime_error("Invalid embedded game license.");
    memcpy(header,bytes.data(),sizeof(header));
    if(header[0]!=1) throw std::runtime_error("Unsupported game license format.");
    size_t offset=sizeof(header); std::wstring* fields[]={&info.author,&info.copyright,&info.name,&info.terms};
    for(int i=0;i<4;++i) {
        size_t size=header[i+1]; if(size>bytes.size()-offset) throw std::runtime_error("Truncated game license.");
        *fields[i]=decodeLicenseUtf8(std::string(reinterpret_cast<const char*>(bytes.data()+offset),size)); offset+=size;
    }
    if(offset!=bytes.size()) throw std::runtime_error("Invalid embedded game license length.");
    validateGameLicense(info); return info;
}
inline std::wstring readLicenseFile(const fs::path& path) {
    auto bytes=readFile(path,131072);
    auto text=decodeLicenseUtf8(std::string(bytes.begin(),bytes.end()));
    GameLicense check; check.terms=text; validateGameLicense(check); return text;
}
inline std::wstring licenseNewlines(const std::wstring& input) {
    std::wstring output;
    for(size_t i=0;i<input.size();++i) {
        if(input[i]==L'\r') { if(i+1<input.size() && input[i+1]==L'\n') ++i; output+=L"\r\n"; }
        else if(input[i]==L'\n') output+=L"\r\n";
        else output+=input[i];
    }
    return output;
}
namespace license_dialog {
inline INT_PTR CALLBACK edit(HWND h,UINT msg,WPARAM w,LPARAM l) {
    if(msg==WM_INITDIALOG) {
        SetWindowLongPtrW(h,DWLP_USER,l); auto* info=reinterpret_cast<GameLicense*>(l);
        const std::wstring* fields[]={&info->author,&info->copyright,&info->name,&info->terms};
        int limits[]={256,512,128,65536};
        for(int i=0;i<4;++i) { SendDlgItemMessageW(h,501+i,EM_SETLIMITTEXT,limits[i],0); SetDlgItemTextW(h,501+i,licenseNewlines(*fields[i]).c_str()); }
        return TRUE;
    }
    if(msg==WM_COMMAND) {
        if(LOWORD(w)==IDCANCEL) { EndDialog(h,IDCANCEL); return TRUE; }
        try {
            if(LOWORD(w)==505) {
                wchar_t path[32768]{}; OPENFILENAMEW d{}; d.lStructSize=sizeof(d); d.hwndOwner=h;
                d.lpstrFilter=L"UTF-8 text\0*.txt;*.md\0All files\0*.*\0"; d.lpstrFile=path; d.nMaxFile=32768;
                d.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
                if(GetOpenFileNameW(&d)) SetDlgItemTextW(h,504,licenseNewlines(readLicenseFile(path)).c_str());
                return TRUE;
            }
            if(LOWORD(w)==IDOK) {
                GameLicense result{licenseText(GetDlgItem(h,501)),licenseText(GetDlgItem(h,502)),licenseText(GetDlgItem(h,503)),licenseText(GetDlgItem(h,504))};
                // Store line endings consistently; display expansion does not consume the text limit.
                std::wstring terms; for(size_t i=0;i<result.terms.size();++i) {
                    if(result.terms[i]==L'\r') { if(i+1<result.terms.size() && result.terms[i+1]==L'\n') ++i; terms+=L'\n'; }
                    else terms+=result.terms[i];
                }
                result.terms=std::move(terms); validateGameLicense(result);
                *reinterpret_cast<GameLicense*>(GetWindowLongPtrW(h,DWLP_USER))=std::move(result);
                EndDialog(h,IDOK); return TRUE;
            }
        } catch(const std::exception& e) { SetDlgItemTextW(h,506,wide(e.what()).c_str()); return TRUE; }
    }
    if(msg==WM_CLOSE) { EndDialog(h,IDCANCEL); return TRUE; }
    return FALSE;
}
struct View { std::array<std::wstring,2> content; };
inline void select(HWND h) {
    auto* view=reinterpret_cast<View*>(GetWindowLongPtrW(h,DWLP_USER));
    int selected=static_cast<int>(SendDlgItemMessageW(h,521,CB_GETCURSEL,0,0));
    if(selected<0 || selected>1) return;
    const wchar_t* scopes[]={L"Applies to the game ROM and its content.",L"Applies to the NgpCraft player, emulator core and HLE BIOS."};
    SetDlgItemTextW(h,522,scopes[selected]); SetDlgItemTextW(h,523,licenseNewlines(view->content[selected]).c_str());
    SendDlgItemMessageW(h,523,EM_SETSEL,0,0); SendDlgItemMessageW(h,523,EM_SCROLLCARET,0,0);
}
inline INT_PTR CALLBACK view(HWND h,UINT msg,WPARAM w,LPARAM l) {
    if(msg==WM_INITDIALOG) {
        SetWindowLongPtrW(h,DWLP_USER,l);
        SendDlgItemMessageW(h,523,EM_SETLIMITTEXT,131072,0);
        for(auto label:{L"Game - author and license",L"NgpCraft player and emulator - MIT"}) SendDlgItemMessageW(h,521,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        SendDlgItemMessageW(h,521,CB_SETCURSEL,0,0); select(h); return TRUE;
    }
    if(msg==WM_COMMAND) {
        if(LOWORD(w)==521 && HIWORD(w)==CBN_SELCHANGE) { select(h); return TRUE; }
        if(LOWORD(w)==IDOK || LOWORD(w)==IDCANCEL) { EndDialog(h,IDOK); return TRUE; }
    }
    if(msg==WM_CLOSE) { EndDialog(h,IDOK); return TRUE; }
    return FALSE;
}
}
inline bool editGameLicense(HWND owner,GameLicense& info) {
    GameLicense draft=info;
    auto result=DialogBoxParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(500),owner,license_dialog::edit,reinterpret_cast<LPARAM>(&draft));
    if(result==-1) throw std::runtime_error("Unable to open game credits and license.");
    if(result!=IDOK) return false;
    info=std::move(draft); return true;
}
inline void showLicenses(HWND owner,const std::wstring& title) {
    auto info=readGameLicense(); std::wstring game=title+L"\n\n";
    game+=L"Author / studio: "+(info.author.empty()?L"Not supplied":info.author)+L"\n";
    if(!info.copyright.empty()) game+=info.copyright+L"\n";
    game+=L"License: "+(info.name.empty()?L"Not specified":info.name)+L"\n\n";
    game+=info.terms.empty()?L"No game license terms were supplied with this package.":info.terms;
    auto player=resource(103);
    license_dialog::View model{{game,wide(std::string(player.begin(),player.end()))}};
    if(DialogBoxParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(520),owner,license_dialog::view,reinterpret_cast<LPARAM>(&model))==-1)
        throw std::runtime_error("Unable to open licenses.");
}
