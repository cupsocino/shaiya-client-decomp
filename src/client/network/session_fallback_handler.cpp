#include "session_fallback_handler.h"
#include "core/x87_runtime.h"
#include "game/player/player_state_owner.h"
#include "network/packets.h"
#include "startup/time_seed.h"
#include "startup/window.h"

extern "C" {
// 0x00571930 / 0x00570680: sub-dispatchers for opcodes 0x1cxx / 0x1dxx (thiscall on the session, which
// both copy from ECX on entry; ret 8). Their layouts are not recovered.
void __attribute__((thiscall)) FUN_00571930(void* self, uint32_t opcode, PacketReader* reader);
void __attribute__((thiscall)) FUN_00570680(void* self, uint32_t opcode, PacketReader* reader);
// 0x005666f0: push_back of a 12 byte cooldown record into the vector object at ECX (thiscall, ret 4).
void __attribute__((thiscall)) FUN_005666f0(void* vector, const void* record);
// 0x005651a0: erase of the record at `it` from the vector object at ECX, iterator result stored to
// `out` (thiscall, ret 8).
void* __attribute__((thiscall)) FUN_005651a0(void* vector, void* out, void* it);
// 0x00454050: unpack the server's packed date and time word into a 16 byte SYSTEMTIME-like block
// (cdecl, plain ret): year = 2000 + (v >> 26 & 0x3f), month = v >> 22 & 0xf, day = v >> 17 & 0x1f,
// hour = v >> 12 & 0x1f, minute = v >> 6 & 0x3f, second = v & 0x3f.
void __attribute__((cdecl)) FUN_00454050(uint32_t packed, void* block);
// 0x004540b0: mktime() of that block (local time, year >= 1900 else 0); cdecl, plain ret.
uint32_t __attribute__((cdecl)) FUN_004540b0(void* block);
// 0x004609e0: skill level record for (id, level); thiscall, ret 8 (the id is read as a word and
// the level as a byte of their stack slots). See session_skill_callbacks_mid.cpp.
void* __attribute__((thiscall)) FUN_004609e0(void* table, uint16_t id, uint8_t level);
// 0x00495540: update of the window object at ECX with (flag, first dword of the 13 byte wire block,
// the double at +1 converted to an integer, the dword at +9); thiscall, ret 0x10.
void __attribute__((thiscall)) FUN_00495540(void* window, uint32_t flag, uint32_t block_dword,
                                            uint32_t converted, uint32_t tail_dword);
// 0x00555f30: vector cleanup on the object at ECX (already C++: core/filter_owner.cpp).
uint32_t __attribute__((thiscall)) FUN_00555f30(void* object);
uint32_t __attribute__((stdcall)) timeGetTime();
}

namespace {
// 12 byte record of the vector at player state +26854948 (begin at +26854952, end at +26854956):
// {u16 skill, u8 level, one byte the original never writes (stack residue; zero here),
//  u32 duration ms, u32 tick (timeGetTime)}. Packed: the vector holds heap bytes at a 12 byte stride.
struct __attribute__((packed)) CooldownRecord {
    uint16_t skill;
    uint8_t level;
    uint8_t unused;
    uint32_t duration_ms;
    uint32_t start_tick;
};
static_assert(sizeof(CooldownRecord) == 12, "record stride used by the listing (add eax, 0xc)");

constexpr uint32_t kTimeOffsetBlock = 26854286;  // 16 bytes, not 4-aligned
constexpr uint32_t kTimeOffsetA = 26854304;
constexpr uint32_t kTimeOffsetB = 26854308;
constexpr uint32_t kCooldownKeyDword = 26854940;
constexpr uint32_t kCooldownKeyByte = 26854944;
constexpr uint32_t kCooldownVector = 26854948;
constexpr uint32_t kCooldownBegin = 26854952;
constexpr uint32_t kCooldownEnd = 26854956;
constexpr uint32_t kSkillTable = 34920;
constexpr uint32_t kSkillTable100 = 34968;

inline char* state_at(uint32_t offset) {
    return reinterpret_cast<char*>(&original_player_state_storage) + offset;
}
template <typename T>
inline T load_state(uint32_t offset) {
    T value;
    __builtin_memcpy(&value, state_at(offset), sizeof value);
    return value;
}
template <typename T>
inline void store_state(uint32_t offset, T value) {
    __builtin_memcpy(state_at(offset), &value, sizeof value);
}
inline void read_bytes(PacketReader* reader, void* destination, uint32_t size) {
    read_packet_bytes_with_diagnostic(reader, destination, size);
}

// The level byte 0x64 selects the second table, always looked up with level 1. The listing pushes the
// whole stack dword holding the level (upper bytes are stack residue); the lookup reads one byte.
inline void* lookup_skill(uint16_t skill, uint8_t level) {
    return level == 0x64 ? FUN_004609e0(state_at(kSkillTable100), skill, 1)
                         : FUN_004609e0(state_at(kSkillTable), skill, level);
}

// First record (stride 12, compared with `!=` against the end pointer like the listing) whose skill
// and level match.
inline CooldownRecord* find_record(uint16_t skill, uint8_t level) {
    const uint32_t begin = load_state<uint32_t>(kCooldownBegin);
    const uint32_t end = load_state<uint32_t>(kCooldownEnd);
    for (uint32_t it = begin; it != end; it += 12) {
        auto* record = reinterpret_cast<CooldownRecord*>(it);
        if (record->skill == skill && record->level == level)
            return record;
    }
    return nullptr;
}

// 0x0114 (full = true) and 0x0115 (full = false): one u32 packed server date and time. Result is
// (mktime of that date read as local time) - (local unix time at arrival).
void time_offset(PacketReader* reader, bool full) {
    uint32_t now = 0;
    read_unix_time32(&now);
    uint32_t packed = 0;
    read_bytes(reader, &packed, 4);
    if (full) {
        // Four unaligned dword stores in this order (the block is 16 bytes at a 2 mod 4 address).
        store_state<uint32_t>(kTimeOffsetBlock, 0);
        store_state<uint32_t>(kTimeOffsetBlock + 4, 0);
        store_state<uint32_t>(kTimeOffsetBlock + 8, 0);
        store_state<uint32_t>(kTimeOffsetBlock + 12, 0);
        FUN_00454050(packed, state_at(kTimeOffsetBlock));
        const uint32_t result = FUN_004540b0(state_at(kTimeOffsetBlock));
        store_state<uint32_t>(kTimeOffsetA, result - now);
        store_state<uint32_t>(kTimeOffsetB, result - now);
    } else {
        uint32_t block[4] = {0, 0, 0, 0};
        FUN_00454050(packed, block);
        const uint32_t result = FUN_004540b0(block);
        store_state<uint32_t>(kTimeOffsetB, result - now);
    }
}

// 0x0809 (flag 1) and 0x080a (flag 0): 13 wire bytes {u8, double (unaligned) at +1, u32 at +9}.
void window_update(PacketReader* reader, uint32_t flag) {
    uint8_t block[13];
    read_bytes(reader, block, 13);
    void* holder = *reinterpret_cast<void**>(dat_0071cfb0);  // dat_0071cfb0 itself is not tested
    if (holder == nullptr)
        return;
    char* window = *reinterpret_cast<char**>(static_cast<char*>(holder) + 0x2d0);
    if (window == nullptr)
        return;
    if (*reinterpret_cast<uint32_t*>(window + 0x20) == 0)
        return;
    uint32_t block_dword, tail_dword;
    double value;
    __builtin_memcpy(&tail_dword, block + 9, 4);
    __builtin_memcpy(&value, block + 1, 8);
    const uint32_t converted = static_cast<uint32_t>(x87_to_integer_005cd414(value));
    __builtin_memcpy(&block_dword, block, 4);
    FUN_00495540(window, flag, block_dword, converted, tail_dword);
}
}  // namespace

// 0x005667a0 (slot 0x630). See the header.
extern "C" void __attribute__((thiscall))
FUN_005667a0(void* self, uint32_t opcode, PacketReader* reader) {
    const uint32_t op = opcode & 0xffffu;
    if ((op & 0xff00u) == 0x1c00u) {
        FUN_00571930(self, opcode, reader);
        return;
    }
    if ((op & 0xff00u) == 0x1d00u) {
        FUN_00570680(self, opcode, reader);
        return;
    }
    switch (op) {
    case 0x0114:
        time_offset(reader, true);
        return;
    case 0x0115:
        time_offset(reader, false);
        return;
    case 0x030b: {
        // List reset and refill: key byte, key dword, u8 count, then count x {u16 skill, u8 level,
        // u32 seconds}. The record is reused for every entry; tick = timeGetTime() read once.
        FUN_00555f30(state_at(kCooldownVector));
        read_bytes(reader, state_at(kCooldownKeyByte), 1);
        read_bytes(reader, state_at(kCooldownKeyDword), 4);
        uint8_t count = 0;
        read_bytes(reader, &count, 1);
        const uint32_t tick = timeGetTime();
        CooldownRecord record = {};
        for (uint8_t index = 0; index < count; ++index) {
            read_bytes(reader, &record.skill, 2);
            read_bytes(reader, &record.level, 1);
            read_bytes(reader, &record.duration_ms, 4);
            record.duration_ms = record.duration_ms * 1000;
            record.start_tick = tick;
            FUN_005666f0(state_at(kCooldownVector), &record);
        }
        return;
    }
    case 0x030c: {
        // {u8 key, u32 key, u16 skill, u8 level}: start or restart one cooldown.
        uint8_t key_byte = 0;
        uint32_t key_dword = 0;
        read_bytes(reader, &key_byte, 1);
        read_bytes(reader, &key_dword, 4);
        if (load_state<uint8_t>(kCooldownKeyByte) != key_byte)
            return;
        if (load_state<uint32_t>(kCooldownKeyDword) != key_dword)
            return;
        uint16_t skill = 0;
        uint8_t level = 0;
        read_bytes(reader, &skill, 2);
        read_bytes(reader, &level, 1);
        char* skill_record = static_cast<char*>(lookup_skill(skill, level));
        if (skill_record == nullptr)
            return;
        CooldownRecord record = {};
        record.skill = skill;
        record.level = level;
        record.duration_ms = static_cast<uint32_t>(*reinterpret_cast<uint16_t*>(skill_record + 0x46)) * 1000;
        record.start_tick = timeGetTime();
        CooldownRecord* found = find_record(skill, level);
        if (found == nullptr) {
            FUN_005666f0(state_at(kCooldownVector), &record);
            return;
        }
        found->duration_ms = static_cast<uint32_t>(*reinterpret_cast<uint16_t*>(skill_record + 0x46)) * 1000;
        return;
    }
    case 0x030d: {
        // {u8 key, u32 key, u16 skill, u8 level}: all four are read before the key tests; erase.
        uint8_t key_byte = 0;
        uint32_t key_dword = 0;
        uint16_t skill = 0;
        uint8_t level = 0;
        read_bytes(reader, &key_byte, 1);
        read_bytes(reader, &key_dword, 4);
        read_bytes(reader, &skill, 2);
        read_bytes(reader, &level, 1);
        if (load_state<uint8_t>(kCooldownKeyByte) != key_byte)
            return;
        if (load_state<uint32_t>(kCooldownKeyDword) != key_dword)
            return;
        CooldownRecord* found = find_record(skill, level);
        if (found == nullptr)
            return;
        uint32_t erased = 0;
        FUN_005651a0(state_at(kCooldownVector), &erased, found);
        return;
    }
    case 0x0809:
        window_update(reader, 1);
        return;
    case 0x080a:
        window_update(reader, 0);
        return;
    default:
        return;
    }
}
