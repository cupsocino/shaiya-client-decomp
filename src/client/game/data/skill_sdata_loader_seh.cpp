// Skill.SData loader, lifted from the original instruction listing.
//
// 0x00461860: thiscall(table, directory, file), ret 8, returns 1 on success and 0 on failure (full EAX).
//
// This unit is built with the MSVC exception ABI (_seh.cpp) because the original function has one
// SEH/C++ unwind state: state 0 covers only the vector construction of the nine level records of one
// skill, and its unwind action (0x006ad990) frees the 0x4cc block holding them.
//
// The decrypted Skill.SData body is read sequentially through a cursor (EBP in the original).
// After the 4-byte skill count come, per skill and per level, two length-prefixed strings (name and
// description) followed by a packed block of 0x74 bytes. The original copies each field of that
// block into the 0x88-byte level record at its own offset and width; the field list below keeps the
// original order, with the source offset worked out from the interleaved cursor increments.
#include <stdint.h>
#include <stddef.h>

#include "archive/saf.h"
#include "archive/saf_open.h"
#include "platform/support.hpp"
#include "runtime/allocations.h"
#include "runtime/array_lifetime.h"
#include "runtime/close_entry.h"
#include "runtime/stdio_format.h"

extern "C" {
extern const char dat_006be1d4[]; // path format, "%s%s"-like: (directory, file name)
extern const char dat_006bc484[]; // open mode "rb"
extern const char dat_006bfe18[]; // 40 character signature plus NUL (41 bytes)
// Original archive and CRT helpers that have no C++ declaration yet (all cdecl).
int32_t FUN_0040cf80(int32_t descriptor, int32_t offset, int32_t origin); // seek
uint8_t FUN_005c1320(void* buffer, int32_t* length, const void* key);     // decrypt, result in AL
uint32_t FUN_005c1270(const void* buffer, uint32_t length);               // CRC
// Element constructor and destructor of one skill level record (still instruction listings).
void __attribute__((thiscall)) FUN_00460b80(void*);
void __attribute__((thiscall)) FUN_00460ba0(void*);
}

namespace {
// Skill levels per skill and the size of one level record. Do not change them without also
// changing the data files: the Skill.SData layout and every FUN_004609e0 caller depend on them.
constexpr uint32_t kSkillLevelsPerSkill = 9;
constexpr uint32_t kSkillLevelSize = 0x88;
constexpr uint32_t kSkillBlockSize = 4 + kSkillLevelsPerSkill * kSkillLevelSize;
static_assert(9 * 0x88 == 0x4c8);
static_assert(kSkillLevelsPerSkill * kSkillLevelSize == 0x4c8);
static_assert(kSkillBlockSize == 0x4cc);
// Size of the packed per-level block that follows the two strings in the file.
constexpr uint32_t kSkillLevelFileFieldsSize = 0x74;
// Offsets inside the level record, and inside the table (this) object.
constexpr uint32_t kLevelName = 0x04;
constexpr uint32_t kLevelDescription = 0x84;
constexpr uint32_t kTableRows = 0x18;
constexpr uint32_t kTableCount = 0x1c;

typedef uint16_t __attribute__((aligned(1), may_alias)) U16u;
typedef uint32_t __attribute__((aligned(1), may_alias)) U32u;

inline void copy8(const uint8_t* src, uint32_t from, uint8_t* level, uint32_t to) {
    level[to] = src[from];
}
inline void copy16(const uint8_t* src, uint32_t from, uint8_t* level, uint32_t to) {
    *reinterpret_cast<U16u*>(level + to) = *reinterpret_cast<const U16u*>(src + from);
}
// 0x00461cd0..0x00461cf4: the word just stored is divided by 1000 (zero extended, signed idiv) and
// the remainder is written back to the same field. The quotient is discarded.
inline void reduce_modulo_1000(uint8_t* level, uint32_t at) {
    const uint32_t value = *reinterpret_cast<U16u*>(level + at);
    *reinterpret_cast<U16u*>(level + at) = static_cast<uint16_t>(value % 1000u);
}

// All 84 fixed fields of one level, in the order of the original (store VA in the comments).
// copyN(src, source offset, level, destination offset); the source offsets are contiguous 0..0x73.
inline void copy_level_fields(const uint8_t* src, uint8_t* level) {
    copy8(src, 0x00, level, 0x02);  // 461bb9
    copy16(src, 0x01, level, 0x08);  // 461bc7
    copy8(src, 0x03, level, 0x0a);  // 461bd6
    copy16(src, 0x04, level, 0x0c);  // 461be7
    copy16(src, 0x06, level, 0x0e);  // 461bf7
    copy16(src, 0x08, level, 0x10);  // 461c09
    copy8(src, 0x0a, level, 0x12);  // 461c1a
    copy8(src, 0x0b, level, 0x13);  // 461c2a
    copy8(src, 0x0c, level, 0x14);  // 461c38
    copy8(src, 0x0d, level, 0x15);  // 461c46
    copy8(src, 0x0e, level, 0x16);  // 461c54
    copy8(src, 0x0f, level, 0x17);  // 461c62
    copy8(src, 0x10, level, 0x18);  // 461c70
    copy8(src, 0x11, level, 0x19);  // 461c7e
    copy8(src, 0x12, level, 0x1a);  // 461c8d
    copy8(src, 0x13, level, 0x1b);  // 461c9b
    copy8(src, 0x14, level, 0x1c);  // 461ca9
    copy8(src, 0x15, level, 0x1d);  // 461cb6
    copy16(src, 0x16, level, 0x1e);  // 461cc5
    reduce_modulo_1000(level, 0x1e);  // 00461cd0..00461cf4
    copy8(src, 0x18, level, 0x20);  // 461d00
    copy8(src, 0x19, level, 0x21);  // 461d0d
    copy8(src, 0x1a, level, 0x22);  // 461d1a
    copy8(src, 0x1b, level, 0x23);  // 461d27
    copy8(src, 0x1c, level, 0x24);  // 461d34
    copy8(src, 0x1d, level, 0x25);  // 461d41
    copy8(src, 0x1e, level, 0x26);  // 461d4e
    copy8(src, 0x1f, level, 0x27);  // 461d5b
    copy8(src, 0x20, level, 0x28);  // 461d68
    copy8(src, 0x21, level, 0x29);  // 461d75
    copy8(src, 0x22, level, 0x2a);  // 461d82
    copy8(src, 0x23, level, 0x2b);  // 461d8f
    copy8(src, 0x24, level, 0x2c);  // 461d9c
    copy8(src, 0x25, level, 0x2d);  // 461daa
    copy8(src, 0x26, level, 0x2e);  // 461db8
    copy8(src, 0x27, level, 0x2f);  // 461dc6
    copy16(src, 0x28, level, 0x30);  // 461dd5
    copy16(src, 0x2a, level, 0x32);  // 461de5
    copy8(src, 0x2c, level, 0x34);  // 461df6
    copy16(src, 0x2d, level, 0x36);  // 461e07
    copy8(src, 0x2f, level, 0x38);  // 461e16
    copy8(src, 0x30, level, 0x39);  // 461e26
    copy8(src, 0x31, level, 0x3a);  // 461e34
    copy16(src, 0x32, level, 0x3c);  // 461e43
    copy16(src, 0x34, level, 0x3e);  // 461e53
    copy8(src, 0x36, level, 0x40);  // 461e64
    copy8(src, 0x37, level, 0x41);  // 461e75
    copy8(src, 0x38, level, 0x42);  // 461e83
    copy8(src, 0x39, level, 0x43);  // 461e90
    copy8(src, 0x3a, level, 0x44);  // 461e9e
    copy16(src, 0x3b, level, 0x46);  // 461ead
    copy8(src, 0x3d, level, 0x48);  // 461ebc
    copy8(src, 0x3e, level, 0x49);  // 461ecc
    copy8(src, 0x3f, level, 0x4a);  // 461eda
    copy8(src, 0x40, level, 0x4b);  // 461ee8
    copy16(src, 0x41, level, 0x4c);  // 461ef7
    copy8(src, 0x43, level, 0x4e);  // 461f06
    copy16(src, 0x44, level, 0x50);  // 461f17
    copy16(src, 0x46, level, 0x52);  // 461f27
    copy16(src, 0x48, level, 0x54);  // 461f39
    copy8(src, 0x4a, level, 0x56);  // 461f4a
    copy16(src, 0x4b, level, 0x58);  // 461f5c
    copy16(src, 0x4d, level, 0x5a);  // 461f6e
    copy16(src, 0x4f, level, 0x5c);  // 461f80
    copy16(src, 0x51, level, 0x5e);  // 461f92
    copy16(src, 0x53, level, 0x60);  // 461fa1
    copy16(src, 0x55, level, 0x62);  // 461fb3
    copy8(src, 0x57, level, 0x64);  // 461fc4
    copy16(src, 0x58, level, 0x66);  // 461fd5
    copy8(src, 0x5a, level, 0x68);  // 461fe4
    copy16(src, 0x5b, level, 0x6a);  // 461ff5
    copy8(src, 0x5d, level, 0x6c);  // 462004
    copy16(src, 0x5e, level, 0x6e);  // 462015
    copy16(src, 0x60, level, 0x70);  // 462025
    copy16(src, 0x62, level, 0x72);  // 462037
    copy16(src, 0x64, level, 0x74);  // 462049
    copy16(src, 0x66, level, 0x76);  // 46205b
    copy16(src, 0x68, level, 0x78);  // 46206d
    copy16(src, 0x6a, level, 0x7a);  // 462082
    copy8(src, 0x6c, level, 0x7c);  // 462093
    copy8(src, 0x6d, level, 0x7d);  // 4620a1
    copy8(src, 0x6e, level, 0x7e);  // 4620ae
    copy8(src, 0x6f, level, 0x7f);  // 4620bd
    copy16(src, 0x70, level, 0x80);  // 4620cc
    copy16(src, 0x72, level, 0x82);  // 4620e1
}

// Unwind action 0x006ad990: release the raw 0x4cc block while its vector construction is running.
struct SkillBlockGuard {
    void* block;
    bool active = true;
    ~SkillBlockGuard() noexcept(false) {
        if (active)
            fun_005cdc59(block);
    }
};

// The call stays an explicit protected operation so that unwinding reaches the guard.
__declspec(noinline) void construct_levels(void* first) {
    fun_005ce6bb(first, kSkillLevelSize, static_cast<int32_t>(kSkillLevelsPerSkill), FUN_00460b80,
                 FUN_00460ba0);
}

// The original inlines strcmp; only equal or different is used.
bool same_text(const uint8_t* a, const uint8_t* b) {
    for (;;) {
        const uint8_t byte = *a;
        if (byte != *b)
            return false;
        if (byte == 0)
            return true;
        ++a;
        ++b;
    }
}

// One length-prefixed string: u32 length, then that many bytes (no terminator is added).
// An empty string allocates nothing and leaves the slot untouched.
inline const uint8_t* read_string(const uint8_t* src, uint8_t* level, uint32_t slot) {
    const uint32_t length = *reinterpret_cast<const U32u*>(src);
    src += 4;
    if (length != 0) {
        uint8_t* text = static_cast<uint8_t*>(fun_005cde20(length));
        *reinterpret_cast<uint8_t**>(level + slot) = text;
        for (uint32_t i = 0; i < length; ++i)
            text[i] = src[i];
        src += length;
    }
    return src;
}

// The table fields are re-read at every use, as in the original.
inline uint32_t table_count(const uint8_t* self) {
    return *reinterpret_cast<const uint16_t*>(self + kTableCount);
}
inline uint8_t**& table_rows(uint8_t* self) {
    return *reinterpret_cast<uint8_t***>(self + kTableRows);
}
} // namespace

// 0x00461860
extern "C" uint32_t __attribute__((thiscall))
FUN_00461860(void* table, const char* directory, const char* file_name) {
    uint8_t* const self = static_cast<uint8_t*>(table);
    char path[0x104];
    fun_005cd489(path, dat_006be1d4, directory, file_name);
    const int32_t file = open_archive_or_loose_file(path, dat_006bc484);
    if (file == 0)
        return 0;

    // Header: 40-byte signature, CRC, size pair and 12 more bytes (read, never used).
    uint8_t expected[0x40] = {};
    uint8_t found[0x40] = {};
    uint32_t stored_crc = 0;
    uint32_t sizes[2] = {0, 0};
    uint8_t unused_header[0xc] = {};
    for (uint32_t i = 0; i < 41; ++i)
        expected[i] = static_cast<uint8_t>(dat_006bfe18[i]);
    FUN_0040d020(found, 1, 0x28, file);
    FUN_0040d020(&stored_crc, 4, 1, file);
    FUN_0040d020(sizes, 8, 1, file);
    FUN_0040d020(unused_header, 1, 0xc, file);
    if (!same_text(found, expected)) {
        // Rewind and give up; the descriptor stays open (original behaviour).
        FUN_0040cf80(file, 0, 0);
        return 0;
    }

    if (dat_00719e80 == nullptr)
        return 0;
    const uint32_t body_length = dat_00719e80->byte_length - 0x40;
    int32_t rounded_length = static_cast<int32_t>(((body_length + 0xf) >> 4) << 4);
    uint8_t* const buffer =
        static_cast<uint8_t*>(fun_005cde20(static_cast<uint32_t>(rounded_length)));
    {
        const uint32_t rounded = static_cast<uint32_t>(rounded_length);
        uint32_t* words = reinterpret_cast<uint32_t*>(buffer);
        for (uint32_t i = 0; i < (rounded >> 2); ++i)
            words[i] = 0;
        uint8_t* tail = reinterpret_cast<uint8_t*>(words + (rounded >> 2));
        for (uint32_t i = 0; i < (rounded & 3u); ++i)
            tail[i] = 0;
    }
    FUN_0040d020(buffer, 1, body_length, file);
    if (FUN_005c1320(buffer, &rounded_length, nullptr) == 0) {
        FUN_005a26c0(file);
        if (buffer != nullptr)
            fun_005cdc59(buffer);
        return 0;
    }
    // The CRC covers the first sizes[0] bytes of the decrypted body. A mismatch leaves the file
    // open and the buffer allocated (original behaviour).
    if (FUN_005c1270(buffer, sizes[0]) != stored_crc)
        return 0;

    // Skill count: a dword store into the table (only the low word is read back afterwards).
    *reinterpret_cast<uint32_t*>(self + kTableCount) = *reinterpret_cast<const uint32_t*>(buffer);
    const uint8_t* cursor = buffer + 4;
    table_rows(self) = static_cast<uint8_t**>(fun_005cde20(table_count(self) * 4 + 4));
    if (table_rows(self) == nullptr) {
        FUN_005a26c0(file);
        fun_005cdc59(buffer);
        return 0;
    }

    if (table_count(self) != 0) {
        int32_t skill = 0;
        do {
            void* block = fun_005cde20(kSkillBlockSize);
            uint8_t* row = nullptr;
            if (block != nullptr) {
                SkillBlockGuard guard{block};
                *static_cast<uint32_t*>(block) = kSkillLevelsPerSkill;
                row = static_cast<uint8_t*>(block) + 4;
                construct_levels(row);
                guard.active = false;
            }
            table_rows(self)[skill] = row;
            if (table_rows(self)[skill] == nullptr) {
                FUN_005a26c0(file);
                fun_005cdc59(buffer);
                return 0;
            }
            for (int32_t offset = 0; offset < static_cast<int32_t>(kSkillLevelsPerSkill * kSkillLevelSize);
                 offset += kSkillLevelSize) {
                uint8_t* const level = table_rows(self)[skill] + offset;
                cursor = read_string(cursor, level, kLevelName);
                cursor = read_string(cursor, level, kLevelDescription);
                copy_level_fields(cursor, level);
                cursor += kSkillLevelFileFieldsSize;
            }
            ++skill;
        } while (skill < static_cast<int32_t>(table_count(self)));
    }

    fun_005cdc59(buffer);
    FUN_005a26c0(file);
    return 1;
}
