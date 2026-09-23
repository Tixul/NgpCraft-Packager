#include "native_core.hpp"
#include "bios_hle_data.hpp"
#include "machine.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace {
constexpr uint32_t magic = 0x5350474e; // NGPS, little-endian.
struct SaveHeader {
    uint32_t magic, version, romHash, capacity[2];
    ngpc_rtc_t rtc;
};
static_assert(sizeof(SaveHeader) == 36 && sizeof(ngpc_rtc_t) == 16);
uint32_t checksum(const uint8_t* p, size_t n) {
    uint32_t h=2166136261u;
    while(n--) h=(h^*p++)*16777619u;
    return h;
}
uint32_t chipBase(unsigned chip) { return chip ? 0x800000u : 0x200000u; }
// Until a game identifies the chip, the desktop core may report the exact,
// unpadded ROM length rather than a hardware-sized multiple of 64 KiB.
bool validCapacity(uint32_t n) { return n<=0x200000; }
}

void NativeCore::close() {
    if(machine) ngpc_destroy(machine);
    machine=nullptr; sampleFrames=0;
}

void NativeCore::load(const std::vector<uint8_t>& rom) {
    if(rom.size()<64 || rom.size()>0x400000) throw std::runtime_error("Invalid ROM size.");
    close(); machine=ngpc_create();
    if(!machine) throw std::runtime_error("Unable to create the emulator.");
    if(ngpc_load_rom(machine,rom.data(),rom.size()) ||
       ngpc_load_bios(machine,ngpcraft_firmware::bios_hle,ngpcraft_firmware::bios_hle_size)) {
        close(); throw std::runtime_error("Unable to load the ROM or built-in firmware.");
    }
    romHash=checksum(rom.data(),rom.size()); romSize=rom.size();
    ngpc_set_timing_silicon(machine,10,8);
    ngpc_set_language(machine,1);
    ngpc_set_k1ge_console(machine,0);
    ngpc_reset(machine,NGPC_RESET_HANDOFF);
    // Desktop core/bios_fingerprint.py: the expected bytes come from the user's ROM.
    static constexpr char title[16]="METALSLUG2ND";
    if(rom.size()>=0x08dcc4+64 && !std::memcmp(rom.data()+0x24,title,16)) {
        std::array<uint8_t,0x2000> chars{};
        ngpc_read_mem(machine,0xa000,chars.data(),chars.size());
        auto first=rom.data()+0x08dcc4;
        if(std::search(chars.begin(),chars.end(),first,first+64)==chars.end())
            ngpc_write_mem(machine,0xa1c0,first,64);
    }
    pixels.fill(0);
}

void NativeCore::run(uint8_t keys) {
    if(!machine) throw std::runtime_error("No game loaded.");
    keys&=0x7f; ngpc_write_mem(machine,0xb0,&keys,1);
    ngpc_summary_t summary{};
    ngpc_run_frames(machine,1,2000000,&summary);
    if(summary.stop_status!=NGPC_OK && summary.stop_status!=NGPC_HALTED && summary.stop_status!=NGPC_COUNT_REACHED)
        throw std::runtime_error("Emulation stopped (code "+std::to_string(summary.stop_status)+").");
    ngpc_get_framebuffer(machine,nativePixels.data(),nativePixels.size());
    for(size_t i=0;i<pixels.size();++i) {
        auto p=nativePixels[i];
        pixels[i]=uint32_t((p&15)*17)<<16 | uint32_t(((p>>4)&15)*17)<<8 | ((p>>8)&15)*17;
    }
    sampleFrames=ngpc_get_audio(machine,samples.data(),samples.size()/2);
}

std::vector<uint8_t> NativeCore::save() const {
    if(!machine) throw std::runtime_error("No game loaded.");
    SaveHeader h{}; h.magic=magic; h.version=1; h.romHash=romHash;
    ngpc_get_rtc(machine,&h.rtc);
    size_t size=sizeof(h)+sizeof(uint32_t);
    for(unsigned c=0;c<2;++c) {
        h.capacity[c]=ngpc_flash_capacity(machine,c);
        if(!validCapacity(h.capacity[c])) throw std::runtime_error("Invalid cartridge flash capacity.");
        size+=h.capacity[c];
    }
    std::vector<uint8_t> result(size);
    std::memcpy(result.data(),&h,sizeof(h));
    size_t offset=sizeof(h);
    // This one internal access is tied to the vendored desktop source revision.
    // ngpc_read_mem performs bus reads: while flash is busy or in ID mode those
    // return status/ID bytes, not persistent data. Never put those in a save.
    const auto& memory=reinterpret_cast<const ngpc::Machine*>(machine)->mem;
    for(unsigned c=0;c<2;++c) {
        std::memcpy(result.data()+offset,memory.data()+chipBase(c),h.capacity[c]);
        offset+=h.capacity[c];
    }
    auto sum=checksum(result.data(),offset);
    std::memcpy(result.data()+offset,&sum,sizeof(sum));
    return result;
}

bool NativeCore::restore(const std::vector<uint8_t>& bytes) {
    if(!machine || bytes.size()<sizeof(SaveHeader)+4 || bytes.size()>maxSaveSize) return false;
    SaveHeader h{}; std::memcpy(&h,bytes.data(),sizeof(h));
    if(h.magic!=magic || h.version!=1 || h.romHash!=romHash) return false;
    size_t size=sizeof(h)+4;
    for(unsigned c=0;c<2;++c) {
        if(!validCapacity(h.capacity[c]) || (h.capacity[c] && romSize<=c*0x200000u)) return false;
        size+=h.capacity[c];
    }
    if(size!=bytes.size()) return false;
    uint32_t sum; std::memcpy(&sum,bytes.data()+size-4,4);
    if(sum!=checksum(bytes.data(),size-4)) return false;
    // All validation precedes mutation. Restore before the first emulated frame.
    size_t offset=sizeof(h);
    for(unsigned c=0;c<2;++c) {
        ngpc_set_flash_size(machine,c,h.capacity[c]);
        if(h.capacity[c]) ngpc_flash_restore(machine,chipBase(c),bytes.data()+offset,h.capacity[c]);
        offset+=h.capacity[c];
    }
    ngpc_set_rtc(machine,&h.rtc); ngpc_flash_clear_dirty(machine);
    return true;
}
