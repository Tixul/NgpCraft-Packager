#pragma once
#include "ngpc_core.h"
#include <array>
#include <vector>

// One statically linked desktop core, driven once per display frame.
class NativeCore {
public:
    static constexpr double fps = 6144000.0 / (515.0 * 199.0);
    static constexpr unsigned sampleRate = 44100;
    static constexpr size_t maxSaveSize = 0x400000 + 40;
    NativeCore() = default;
    ~NativeCore() { close(); }
    NativeCore(const NativeCore&) = delete;
    NativeCore& operator=(const NativeCore&) = delete;
    void load(const std::vector<uint8_t>& rom);
    void close();
    void run(uint8_t keys);
    std::vector<uint8_t> save() const;
    bool restore(const std::vector<uint8_t>& bytes);
    ngpc_t* handle() const { return machine; }
    const std::array<uint32_t,160*152>& video() const { return pixels; }
    const int16_t* audio() const { return samples.data(); }
    unsigned audioFrames() const { return sampleFrames; }
private:
    ngpc_t* machine = nullptr;
    uint32_t romHash = 0;
    size_t romSize = 0;
    std::array<uint16_t,160*152> nativePixels{};
    std::array<uint32_t,160*152> pixels{};
    std::array<int16_t,8192> samples{};
    unsigned sampleFrames = 0;
};
