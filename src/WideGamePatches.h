// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "types.h"
#include <array>
#include <cstring>
#include <vector>

namespace melonDS::WideGamePatches
{
// Bundled payloads are from DS-Homebrew/TWiLightMenu resources/widescreen at
// b9e9861b573178931e605a53099e6a584b1631ad. Original authors include
// gamemasterplc and the widescreen cheat collection contributors:
// https://gbatemp.net/threads/widescreen-cheats-for-ds-games-on-3ds.543212/
// https://gist.github.com/vaguerant/62210d3a091b062e59950a73f1f44d8e
// Collection provenance is not local emulator validation. Experimental matching
// deliberately uses its published game-code/header-CRC convention, not a claim
// that a translated or modified ROM has a verified executable fingerprint.
struct CommunityProfile
{
    const char* ID;
    u16 HeaderCRC;
    u16 Offset, Words;
    bool CustomAspect;
};
}
#include "WidePatchData.h"

namespace melonDS::WideGamePatches
{
inline const CommunityProfile* FindCommunity(const char* gameCode, u16 crc)
{
    const CommunityProfile* wildcard = nullptr;
    for (const auto& profile : CommunityProfiles)
    {
        if (std::memcmp(gameCode, profile.ID, 4)) continue;
        if (profile.HeaderCRC == crc) return &profile;
        if (profile.HeaderCRC == 0xFFFF) wildcard = &profile;
    }
    return wildcard;
}

constexpr bool ValidAspect(int width, int height)
{
    return width >= 1 && width <= 32 && height >= 1 && height <= 32 &&
        width * 3 >= height * 4 && width * 9 <= height * 32;
}
constexpr u32 AspectQ12(int width, int height)
{
    return ValidAspect(width, height) ? (4096u * width + height / 2) / height : 0;
}
inline bool SupportsAspect(const CommunityProfile& profile, int width, int height)
{
    return profile.CustomAspect ? ValidAspect(width, height) :
        width > 0 && height > 0 && width <= 32 && height <= 32 && width * 10 == height * 16;
}

// Bound inline payload reads before passing a bundled stream to the existing
// interpreter. Do not interpret inline ARM words as Action Replay operations.
inline bool ValidProgram(const std::vector<u32>& words)
{
    if (words.empty() || words.size() % 2) return false;
    for (size_t i = 0; i < words.size(); )
    {
        const u32 opcode = words[i], value = words[i + 1];
        i += 2;
        const unsigned operation = opcode >> 24;
        if ((operation >= 0xC0 && operation <= 0xCF && operation != 0xC0 && operation != 0xC5 && operation != 0xC6) ||
            (operation >= 0xDD && operation <= 0xDF) ||
            (operation == 0xD4 && (opcode & 0xFF) > 8)) return false;
        if (opcode >> 28 == 0xE)
        {
            const u64 payloadWords = ((u64(value) + 7) / 8) * 2;
            if (payloadWords > words.size() - i) return false;
            i += payloadWords;
        }
    }
    return true;
}

inline std::vector<u32> BuildCommunityCode(const CommunityProfile& profile, int width, int height)
{
    if (!SupportsAspect(profile, width, height) ||
        size_t(profile.Offset) + profile.Words > std::size(CommunityWords)) return {};
    std::vector<u32> code(CommunityWords + profile.Offset, CommunityWords + profile.Offset + profile.Words);
    if (profile.CustomAspect)
    {
        if (code.size() % 6) return {};
        for (size_t i = 0; i < code.size(); i += 6)
        {
            if ((code[i] >> 28) != 9 || (code[i+2] >> 28) != 1 ||
                (code[i] & 0x0FFFFFFF) != (code[i+2] & 0x0FFFFFFF) ||
                code[i+1] != 0x1555 || code[i+3] != 0x199A ||
                code[i+4] != 0xD2000000 || code[i+5] != 0) return {};
            code[i+3] = AspectQ12(width, height);
        }
    }
    return ValidProgram(code) ? code : std::vector<u32>{};
}

struct WordPatch { u32 Address, Original, Replacement; };
// The first six writes are the locally tested renderer-compatible subset from
// gamemasterplc's Mario Kart USA code. These are broad bypasses, not parameterized
// frustum corrections.
inline constexpr WordPatch MarioKartCull[] = {
    {0x020D4D10,0x1A000005,0xE1A00000}, {0x020D4D1C,0x0A000002,0xE1A00000},
    {0x020D4C84,0x1A000013,0xE1A00000}, {0x020D4C90,0x0A000010,0xE1A00000},
    {0x020E1E6C,0x1A000004,0xE1A00000}, {0x020ED80C,0x1A000056,0xE1A00000},
    // Locally traced racer camera loop (2026-11-01): ignore its side-plane
    // rejection result. Retain the earlier behind-camera test and subsequent
    // distance/LOD checks, plus their normal model/shadow visibility updates.
    // The object-only six-write subset above does not cover this racer path.
    {0x02086C50,0xE59D0000,0xE3A00000}};
inline constexpr std::array<u8,20> MarioKartPayloadSHA1 = {
    0xB5,0x21,0xB5,0xD0,0x0D,0x6B,0xE9,0x1E,0x0E,0x50,
    0xEB,0xA2,0xFF,0xA8,0x91,0x74,0x4E,0x5B,0x7E,0x23};
inline constexpr u32 MarioKartUsedSize = 25562014;

// These return-visible entry replacements reproduce the isolated October 24
// interpreter probes. They bypass the entire selected predicate, including
// non-drawing callers; they are opt-in experiments, not selective FOV bounds.
// Original entry words were checked in the saved-RAM disassemblies, and each
// profile matches the used ROM payload after the secure area, as Mario Kart does.
inline constexpr WordPatch ZeldaCull[] = {
    {0x02088014,0xE92D41F8,0xE3A00001}, {0x02088018,0xE24DD00C,0xE12FFF1E}};
inline constexpr WordPatch JokerCull[] = {
    {0x02036608,0xE92D41F0,0xE3A00001}, {0x0203660C,0xE1A07000,0xE12FFF1E}};
inline constexpr WordPatch FFTA2Cull[] = {
    {0x0208B0BC,0xE92D4008,0xE3A00001}, {0x0208B0C0,0xE1D0E0F0,0xE12FFF1E}};

// Phantom Hourglass also computes a map-cell window by unprojecting the four
// native screen corners onto two ground planes (0208A404). Its scenery drawing
// loop at 02111BF0 only visits that window, independently of the sphere test.
// Expand those corners, retaining the game's camera transform, grid conversion,
// world clamps and one-cell drawing padding. Do not draw the entire map.
inline constexpr WordPatch ZeldaCameraSites[] = {
    {0x0208A488,0xE3A00000,0}, {0x0208A4AC,0xE1A01000,0},
    {0x0208A4B0,0xE1A02000,0}, {0x0208A4B4,0xE58D0014,0},
    {0x0208A4C8,0xE3A01000,0}, {0x0208A4EC,0xE1A02001,0},
    {0x0208A4F0,0xE58D1014,0}, {0x0208A4F4,0xE3A000FF,0},
    {0x0208A528,0xE3A00000,0}, {0x0208A52C,0xE58D0014,0},
    {0x0208A530,0xE3A010BF,0}, {0x0208A534,0xE1A02000,0},
    {0x0208A570,0xE3A000FF,0}, {0x0208A574,0xE3A010BF,0},
    {0x0208A608,0xE1A01000,0}, {0x0208A610,0xE58D0014,0},
    {0x0208A644,0xE3A01000,0}, {0x0208A64C,0xE58D1014,0},
    {0x0208A650,0xE3A000FF,0}, {0x0208A68C,0xE58D0014,0},
    {0x0208A690,0xE3A010BF,0}, {0x0208A6CC,0xE58D0014,0},
    {0x0208A6D0,0xE3A000FF,0}, {0x0208A6D4,0xE3A010BF,0}};

constexpr u32 RotateRight(u32 value, unsigned bits)
{
    return bits ? (value >> bits) | (value << (32-bits)) : value;
}
constexpr u32 CornerMove(int value)
{
    // ARMv5 MOV/MVN immediates are rotated bytes. Round the requested corner
    // outward when necessary, rather than accidentally shortening the view.
    u32 magnitude = value < 0 ? u32(-value-1) : u32(value);
    for (;; ++magnitude)
        for (unsigned rotation=0; rotation<16; ++rotation)
        {
            const u32 immediate = RotateRight(magnitude, (32-rotation*2)&31);
            if (immediate <= 255)
                return (value < 0 ? 0xE3E00000 : 0xE3A00000) | (rotation<<8) | immediate;
        }
}
inline auto ZeldaCameraWords(u32 left, u32 top, u32 right, u32 bottom)
{
    // Preserve the zero stack argument to 01FFB7EC. The second four calls
    // reuse that argument: the helper reads it but never writes it. R0/R1 may
    // now be negative, so they must not also supply the zero argument.
    return std::array<u32,24>{left,top|0x1000,0xE3A02000,0xE58D2014,
        top|0x1000,0xE3A02000,0xE58D2014,right,
        0xE3A02000,0xE58D2014,bottom|0x1000,left,
        right,bottom|0x1000,top|0x1000,left,
        top|0x1000,0xE1A00000,right,left,bottom|0x1000,
        right,0xE1A00000,bottom|0x1000};
}
inline auto BuildZeldaCameraWords(int width, int height)
{
    return ZeldaCameraWords(CornerMove(-(width-256)/2), CornerMove(-(height-192)/2),
        CornerMove(255+(width-256)/2), CornerMove(191+(height-192)/2));
}
inline bool ValidCornerMove(u32 word, int minimum, int maximum)
{
    const u32 opcode = word & 0xFFFFF000;
    if (opcode != 0xE3A00000 && opcode != 0xE3E00000) return false;
    const u32 immediate = RotateRight(word&255, ((word>>8)&15)*2);
    const s32 value = opcode == 0xE3E00000 ? s32(~immediate) : s32(immediate);
    return value >= minimum && value <= maximum;
}
template<typename Read, typename Write>
bool ApplyZeldaCameraFix(int width, int height, u32 arm9PC, Read read, Write write)
{
    if (width < 256 || width > 768 || height < 192 || height > 768 ||
        width%2 || height%2 || (width==256 && height==192)) return false;
    // Changing half of this calculation while it is executing would mix the
    // old and new corners. Retry outside the entire function, not just entry.
    if (arm9PC >= 0x0208A404 && arm9PC < 0x0208A7E8) return false;
    const auto desired = BuildZeldaCameraWords(width,height);
    std::array<u32,24> current;
    bool original = true;
    for (unsigned i=0; i<current.size(); ++i)
    {
        current[i] = read(ZeldaCameraSites[i].Address);
        original &= current[i] == ZeldaCameraSites[i].Original;
    }
    if (current == desired) return false;
    if (!original)
    {
        // Recognize a complete previous version of our generated patch. This
        // supports saved states made at another FOV without accepting a mixed
        // or conflicting patch, or depending on host-side ownership history.
        const u32 left=current[0], top=current[1]&~0x1000u;
        const u32 right=current[7], bottom=current[10]&~0x1000u;
        if (!ValidCornerMove(left,-260,0) || !ValidCornerMove(top,-292,0) ||
            !ValidCornerMove(right,255,512) || !ValidCornerMove(bottom,191,484) ||
            current != ZeldaCameraWords(left,top,right,bottom)) return false;
    }
    for (unsigned i=0; i<current.size(); ++i)
        if (current[i] != desired[i]) write(ZeldaCameraSites[i].Address,desired[i]);
    return true;
}

struct VisibilityProfile
{
    const char* Name;
    const char* GameCode;
    u16 HeaderCRC;
    u8 Revision;
    u32 UsedSize;
    std::array<u8,20> PayloadSHA1;
    const WordPatch* Patches;
    unsigned Count;
    bool PredicateEntry;
    bool SceneryWindow = false;
};
inline constexpr VisibilityProfile VisibilityProfiles[] = {
    {"Mario Kart DS", "AMCE", 0xDEF2, 0, MarioKartUsedSize, MarioKartPayloadSHA1,
        MarioKartCull, unsigned(std::size(MarioKartCull)), false},
    {"Zelda: Phantom Hourglass", "AZEE", 0x4D56, 0, 65336932,
        {0x40,0xB5,0x6C,0x74,0xF8,0x8D,0xEA,0xDB,0x42,0xD7,
         0xC0,0x05,0xEB,0x53,0x28,0x8E,0x18,0x30,0x46,0xFD},
        ZeldaCull, unsigned(std::size(ZeldaCull)), true, true},
    {"Dragon Quest Monsters: Joker", "AJRE", 0x3CCD, 0, 87602276,
        {0xAC,0xE1,0xC3,0x71,0x53,0xF2,0x62,0x74,0xC0,0x0D,
         0xE1,0x2E,0x1B,0xF3,0x74,0xC5,0x11,0xA5,0x44,0xDE},
        JokerCull, unsigned(std::size(JokerCull)), true},
    {"Final Fantasy Tactics A2", "A6FE", 0x7EDB, 0, 128954668,
        {0x29,0x66,0xB2,0xE2,0xD7,0x0D,0xE7,0x65,0x29,0x08,
         0x22,0xC7,0xF0,0x49,0xD9,0x8E,0x45,0x59,0x4A,0x61},
        FFTA2Cull, unsigned(std::size(FFTA2Cull)), true}};

inline const VisibilityProfile* FindVisibilityCandidate(const char* gameCode, u8 revision, u16 crc, u32 usedSize)
{
    for (const auto& profile : VisibilityProfiles)
        if (!std::memcmp(gameCode, profile.GameCode, 4) && revision == profile.Revision &&
            crc == profile.HeaderCRC && usedSize == profile.UsedSize) return &profile;
    return nullptr;
}

inline bool SupportsVisibilityView(int width, int height)
{
    // These bypasses do not contain aspect/FOV constants. All profiles are
    // available for expanded views; a native view does not need the bypass.
    return width > 256 || height > 192;
}

template<typename Read, typename Write>
u32 ApplyVisibilityFix(const VisibilityProfile& profile, u32 arm9PC, Read read, Write write)
{
    if (profile.PredicateEntry)
    {
        const u32 entry = profile.Patches[0].Address;
        // Do not replace an entry while its original prologue may be in flight.
        // R15 includes the ARM pipeline offset. Retry on the next regular IRQ.
        if (arm9PC >= entry && arm9PC < entry + 16) return 0;
        // A return stub is one unit: never leave a modified/partial prologue
        // paired with our BX LR, or repair over another cheat's instructions.
        for (unsigned i = 0; i < profile.Count; ++i)
            if (read(profile.Patches[i].Address) != profile.Patches[i].Original) return 0;
    }
    u32 mask = 0;
    for (unsigned i = 0; i < profile.Count; ++i)
    {
        const auto& p = profile.Patches[i];
        if (read(p.Address) != p.Original) continue;
        write(p.Address, p.Replacement);
        mask |= 1u << i;
    }
    return mask;
}

template<typename Read, typename Write>
u32 ApplyMarioKartCull(bool enabled, Read read, Write write)
{
    return enabled ? ApplyVisibilityFix(VisibilityProfiles[0], 0, read, write) : 0;
}
}
