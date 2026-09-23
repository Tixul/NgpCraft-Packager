#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstdint>
#include <cstring>
namespace fs = std::filesystem;
constexpr int ROM_RESOURCE=101, CONFIG_RESOURCE=102, PLAYER_RESOURCE=201;
struct Config {
    uint32_t version=1, scale=4, fullscreen=0, reserved=0;
    wchar_t title[128]{};
};
static_assert(sizeof(Config)==272, "Windows config layout");
inline std::vector<uint8_t> resource(int id) {
    HRSRC r=FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if(!r) throw std::runtime_error("Missing embedded resource.");
    auto p=static_cast<const uint8_t*>(LockResource(LoadResource(nullptr,r)));
    DWORD n=SizeofResource(nullptr,r);
    if(!p || !n) throw std::runtime_error("Empty embedded resource.");
    return {p,p+n};
}
inline std::vector<uint8_t> readFile(const fs::path& path, size_t limit=16*1024*1024) {
    std::ifstream f(path,std::ios::binary|std::ios::ate);
    if(!f) throw std::runtime_error("Unable to read the file.");
    auto n=f.tellg();
    if(n<0 || static_cast<uint64_t>(n)>limit) throw std::runtime_error("File is too large.");
    std::vector<uint8_t> data(static_cast<size_t>(n)); f.seekg(0);
    if(!data.empty() && !f.read(reinterpret_cast<char*>(data.data()),n)) throw std::runtime_error("Incomplete file read.");
    return data;
}
inline void writeFile(const fs::path& path,const void* data,size_t size) {
    std::ofstream f(path,std::ios::binary|std::ios::trunc);
    if(!f || !f.write(static_cast<const char*>(data),size)) throw std::runtime_error("Unable to write the file.");
    f.close(); if(!f) throw std::runtime_error("Incomplete file write.");
}
inline fs::path executablePath() {
    std::vector<wchar_t> p(32768); DWORD n=GetModuleFileNameW(nullptr,p.data(),static_cast<DWORD>(p.size()));
    if(!n || n>=p.size()) throw std::runtime_error("Unable to locate the executable.");
    return fs::path(std::wstring(p.data(),n));
}
inline std::wstring wide(const std::string& s) {
    int n=MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,nullptr,0);
    std::wstring out(n,L'\0'); if(n) MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,out.data(),n);
    if(!out.empty()) out.pop_back();
    return out;
}
