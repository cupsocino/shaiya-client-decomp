// Monster.SData loader, lifted from the original instruction listing.
//
// 0x00461470: thiscall(table, directory, file), ret 8, returns 1 on success and 0 on failure (full EAX).
//
// This unit is built with the MSVC exception ABI (_seh.cpp) because the original function has one
// SEH/C++ unwind state: state 0 covers only the vector construction of the monster records, and its
// unwind action (0x006ad970) frees the raw block holding the count and the records.
//
// File layout of the decrypted body: a dword whose low word is the monster count, then per monster
// one length-prefixed string (the name, record +0x00) and a packed block of 0x1f bytes that the
// original copies field by field into the 0x2c-byte record.
//
// Table (this) object, the shared SData container at dat_0086de50: +0x10 pointer to the records,
// +0x14 the count (a dword store of the first body dword; only its low word is read back).
// The record array holds count + 1 constructed records (the vector count is the low word plus one);
// only the first `count` are filled from the file.
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
// Element constructor and destructor of one monster record (the constructor is still an
// instruction listing; the destructor lives in player_state_storage_cleanup.cpp).
void __attribute__((thiscall)) FUN_00460b40(void*);
void __attribute__((thiscall)) FUN_00460b60(void*);
}

namespace {
constexpr uint32_t kMonsterRecordSize = 0x2c;
// Size of the packed block that follows the name in the file.
constexpr uint32_t kMonsterFileFieldsSize = 0x1f;
// Offsets inside the monster record, and inside the table (this) object.
constexpr uint32_t kRecordName = 0x00;
constexpr uint32_t kTableRecords = 0x10;
constexpr uint32_t kTableCount = 0x14;
static_assert(kMonsterRecordSize == 0x2c);

typedef uint16_t __attribute__((aligned(1), may_alias)) U16u;
typedef uint32_t __attribute__((aligned(1), may_alias)) U32u;

inline void copy8(const uint8_t* src, uint32_t from, uint8_t* record, uint32_t to) {
    record[to] = src[from];
}
inline void copy16(const uint8_t* src, uint32_t from, uint8_t* record, uint32_t to) {
    *reinterpret_cast<U16u*>(record + to) = *reinterpret_cast<const U16u*>(src + from);
}
inline void copy32(const uint8_t* src, uint32_t from, uint8_t* record, uint32_t to) {
    *reinterpret_cast<U32u*>(record + to) = *reinterpret_cast<const U32u*>(src + from);
}

// All 19 fixed fields of one monster, in the order of the original (store VA in the comments).
// copyN(src, source offset, record, destination offset); the source offsets are contiguous 0..0x1e.
inline void copy_monster_fields(const uint8_t* src, uint8_t* record) {
    copy16(src, 0x00, record, 0x04); // 46172c
    copy16(src, 0x02, record, 0x06); // 461738
    copy8(src, 0x04, record, 0x08);  // 461746
    copy32(src, 0x05, record, 0x0c); // 461753
    copy8(src, 0x09, record, 0x10);  // 46175e
    copy8(src, 0x0a, record, 0x11);  // 46176b
    copy8(src, 0x0b, record, 0x12);  // 461776
    copy32(src, 0x0c, record, 0x14); // 461781
    copy8(src, 0x10, record, 0x18);  // 46178c
    copy32(src, 0x11, record, 0x1c); // 461799
    copy8(src, 0x15, record, 0x20);  // 4617a4
    copy8(src, 0x16, record, 0x21);  // 4617b1
    copy8(src, 0x17, record, 0x22);  // 4617bc
    copy8(src, 0x18, record, 0x23);  // 4617c7
    copy8(src, 0x19, record, 0x24);  // 4617d2
    copy8(src, 0x1a, record, 0x25);  // 4617dd
    copy8(src, 0x1b, record, 0x26);  // 4617e9
    copy8(src, 0x1c, record, 0x27);  // 4617f4
    copy16(src, 0x1d, record, 0x28); // 461800
}

// Unwind action 0x006ad970: release the raw block while its vector construction is running.
struct MonsterBlockGuard {
    void* block;
    bool active = true;
    ~MonsterBlockGuard() noexcept(false) {
        if (active)
            fun_005cdc59(block);
    }
};

// The call stays an explicit protected operation so that unwinding reaches the guard.
__declspec(noinline) void construct_records(void* first, int32_t count) {
    fun_005ce6bb(first, kMonsterRecordSize, count, FUN_00460b40, FUN_00460b60);
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
inline const uint8_t* read_string(const uint8_t* src, uint8_t* record, uint32_t slot) {
    const uint32_t length = *reinterpret_cast<const U32u*>(src);
    src += 4;
    if (length != 0) {
        uint8_t* text = static_cast<uint8_t*>(fun_005cde20(length));
        *reinterpret_cast<uint8_t**>(record + slot) = text;
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
inline uint8_t*& table_records(uint8_t* self) {
    return *reinterpret_cast<uint8_t**>(self + kTableRecords);
}
} // namespace

// 0x00461470
extern "C" uint32_t __attribute__((thiscall))
FUN_00461470(void* table, const char* directory, const char* file_name) {
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

    // Monster count: a dword store into the table (only the low word is read back afterwards).
    // The record array has one more record than the count.
    const uint32_t first_dword = *reinterpret_cast<const uint32_t*>(buffer);
    *reinterpret_cast<uint32_t*>(self + kTableCount) = first_dword;
    const uint32_t record_count = static_cast<uint32_t>(static_cast<uint16_t>(first_dword)) + 1;
    const uint8_t* cursor = buffer + 4;
    {
        void* block = fun_005cde20(record_count * kMonsterRecordSize + 4);
        uint8_t* first = nullptr;
        if (block != nullptr) {
            MonsterBlockGuard guard{block};
            *static_cast<uint32_t*>(block) = record_count;
            first = static_cast<uint8_t*>(block) + 4;
            construct_records(first, static_cast<int32_t>(record_count));
            guard.active = false;
        }
        table_records(self) = first;
    }
    if (table_records(self) == nullptr) {
        FUN_005a26c0(file);
        fun_005cdc59(buffer);
        return 0;
    }

    if (table_count(self) != 0) {
        int32_t index = 0;
        uint32_t offset = 0;
        do {
            uint8_t* const record = table_records(self) + offset;
            cursor = read_string(cursor, record, kRecordName);
            copy_monster_fields(cursor, record);
            cursor += kMonsterFileFieldsSize;
            ++index;
            offset += kMonsterRecordSize;
        } while (index < static_cast<int32_t>(table_count(self)));
    }

    fun_005cdc59(buffer);
    FUN_005a26c0(file);
    return 1;
}
