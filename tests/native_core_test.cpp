#include "native_core.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>

static void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
static uint32_t hash(const std::vector<uint8_t>& v) {
    uint32_t h=2166136261u; for(size_t i=0;i<v.size()-4;++i) h=(h^v[i])*16777619u; return h;
}
int wmain() {
    try {
        // Synthetic cartridge data only; this test does not execute copyrighted ROMs.
        std::vector<uint8_t> rom(0x400000,0xff);
        NativeCore source; source.load(rom);
        check(ngpc_abi_version()==18,"unexpected core ABI");
        ngpc_set_flash_size(source.handle(),0,0x200000);
        ngpc_set_flash_size(source.handle(),1,0x200000);
        uint8_t a=0x42,b=0x73;
        ngpc_flash_restore(source.handle(),0x201234,&a,1);
        ngpc_flash_restore(source.handle(),0x801234,&b,1);
        auto saved=source.save();
        check(saved.size()==NativeCore::maxSaveSize,"full dual-chip save size");
        NativeCore target; target.load(rom);
        check(target.restore(saved),"valid save rejected");
        check(target.save()==saved,"flash or RTC round-trip mismatch");
        // Validation failures must leave both chips and RTC untouched.
        auto bad=saved; bad.back()^=1;
        check(!target.restore(bad),"corruption accepted");
        bad=saved; bad.pop_back(); check(!target.restore(bad),"truncation accepted");
        bad=saved; bad[14]=0x21; auto sum=hash(bad); std::memcpy(bad.data()+bad.size()-4,&sum,4);
        check(!target.restore(bad),"invalid chip capacity accepted");
        check(target.save()==saved,"invalid save changed the machine");
        auto otherRom=rom; otherRom[0x100]^=1;
        NativeCore other; other.load(otherRom); check(!other.restore(saved),"wrong ROM accepted");
        // Real flash command: program in a save block and snapshot during its busy window.
        auto m=source.handle();
        ngpc_bus_write(m,0x205555,0xaa); ngpc_bus_write(m,0x202aaa,0x55);
        ngpc_bus_write(m,0x205555,0xa0); ngpc_bus_write(m,0x3fa010,0x5a);
        uint8_t bus=0; ngpc_read_mem(m,0x3fa010,&bus,1);
        check(bus!=0x5a,"test did not enter flash busy state");
        auto busy=source.save();
        check(busy[36+0x1fa010]==0x5a,"saved bus status instead of flash contents");
        check(target.restore(busy),"busy-time save rejected");
        ngpc_read_mem(target.handle(),0x3fa010,&bus,1);
        check(bus==0x5a,"programmed flash byte not restored");
        // No-flash homebrew can still persist its RTC.
        NativeCore small; small.load(std::vector<uint8_t>(64,0));
        auto tiny=small.save(); check(small.restore(tiny),"small ROM save rejected");
        NativeCore unpadded; unpadded.load(std::vector<uint8_t>(79490,0));
        auto partial=unpadded.save(); check(unpadded.restore(partial),"unpadded ROM save rejected");
        std::puts("PASS: desktop ABI, both flash chips, RTC, corrupt/wrong-ROM saves, busy-flash snapshot, small ROM");
        return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
