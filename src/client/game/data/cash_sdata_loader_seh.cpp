// Cash.SData loader (the 0x94-byte record table), lifted from the original instruction listing.
//
// 0x00462140: thiscall(table, directory, file), ret 8, returns 1 on success and 0 on failure (full EAX).
// The caller (0x004246c0) passes directory "data" and file "Cash.SData", so this is the item-mall
// (cash shop) product table.
//
// This unit is built with the MSVC exception ABI (_seh.cpp) because the original function has one
// SEH/C++ unwind state: state 0 covers only the vector construction of the records, and its unwind
// action (0x006ad9b0) frees the raw block holding the count and the records.
//
// File layout of the decrypted body: a dword whose low word is the record count, then per record a
// packed numeric part of 0x88 bytes followed by three length-prefixed strings. The original copies
// each field of the numeric part into the 0x94-byte record at its own offset and width, then the
// strings: +0x00 a fixed 0x33-byte zero-filled buffer (display name), +0x04 and +0x90 exact-length
// buffers without terminator.
//
// Table (this) object, the shared SData container at dat_0086de50: +0x20 pointer to the records,
// +0x24 the count (a dword store of the first body dword; only its low word is read back). The
// array base and the count are also copied to the globals dat_00864df8 / dat_00864dfc (the
// network code walks the table through these). Unlike the other SData loaders there is no null
// check after the array allocation and none after the per-record allocations (original behaviour).
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
extern uint32_t dat_00864df8;     // copy of the record array base (table +0x20)
extern uint16_t dat_00864dfc;     // copy of the record count (table +0x24, low word)
// Original archive and CRT helpers that have no C++ declaration yet (all cdecl).
int32_t FUN_0040cf80(int32_t descriptor, int32_t offset, int32_t origin); // seek
uint8_t FUN_005c1320(void* buffer, int32_t* length, const void* key);     // decrypt, result in AL
uint32_t FUN_005c1270(const void* buffer, uint32_t length);               // CRC
// Element constructor and destructor of one 0x94-byte record (instruction listings or the
// player_state_storage_cleanup.cpp unit).
void __attribute__((thiscall)) FUN_00460be0(void*);
void __attribute__((thiscall)) FUN_00460c00(void*);
}

namespace {
constexpr uint32_t kCashRecordSize = 0x94;
// Size of the packed numeric part that precedes the strings in the file.
constexpr uint32_t kCashFileHeadSize = 16;
constexpr uint32_t kCashGroups = 4;
constexpr uint32_t kCashPairsPerGroup = 6;
// One (u32, u8) pair is five bytes in the file; a group is six pairs.
constexpr uint32_t kCashPairFileSize = 5;
static_assert(kCashFileHeadSize + kCashGroups * kCashPairsPerGroup * kCashPairFileSize == 0x88);
static_assert(0x10 + kCashGroups * kCashPairsPerGroup * 4 == 0x70);
static_assert(0x70 + kCashGroups * kCashPairsPerGroup == 0x88);
// Offsets inside the record, and inside the table (this) object.
constexpr uint32_t kRecordName = 0x00;   // 0x33-byte buffer
constexpr uint32_t kRecordCode = 0x04;   // string pointer
constexpr uint32_t kRecordText = 0x90;   // string pointer
constexpr uint32_t kRecordNameSize = 0x33;
constexpr uint32_t kTableRecords = 0x20;
constexpr uint32_t kTableCount = 0x24;

typedef uint32_t __attribute__((aligned(1), may_alias)) U32u;

inline void copy32(const uint8_t* src, uint32_t from, uint8_t* record, uint32_t to) {
    *reinterpret_cast<U32u*>(record + to) = *reinterpret_cast<const U32u*>(src + from);
}

// Unwind action 0x006ad9b0: release the raw block while its vector construction is running.
struct CashBlockGuard {
    void* block;
    bool active = true;
    ~CashBlockGuard() noexcept(false) {
        if (active)
            fun_005cdc59(block);
    }
};

// The call stays an explicit protected operation so that unwinding reaches the guard.
__declspec(noinline) void construct_records(void* first, int32_t count) {
    fun_005ce6bb(first, kCashRecordSize, count, FUN_00460be0, FUN_00460c00);
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

// The table fields are re-read at every use, as in the original.
inline uint32_t table_count(const uint8_t* self) {
    return *reinterpret_cast<const uint16_t*>(self + kTableCount);
}
inline uint8_t*& table_records(uint8_t* self) {
    return *reinterpret_cast<uint8_t**>(self + kTableRecords);
}

// 0x004624c0..0x00462519: first string. Length prefix, then (if non-zero) a fixed 0x33-byte buffer
// is allocated and zeroed, `length` bytes are copied into it (no bound check, as in the original)
// and the last byte is cleared again.
inline const uint8_t* read_name(const uint8_t* src, uint8_t* record) {
    const uint32_t length = *reinterpret_cast<const U32u*>(src);
    src += 4;
    if (length != 0) {
        uint8_t* text = static_cast<uint8_t*>(fun_005cde20(kRecordNameSize));
        *reinterpret_cast<uint8_t**>(record + kRecordName) = text;
        for (uint32_t i = 0; i < kRecordNameSize; ++i)
            text[i] = 0;
        for (uint32_t i = 0; i < length; ++i)
            text[i] = src[i];
        src += length;
        text[0x32] = 0;
    }
    return src;
}

// 0x00462519..0x00462599: the two exact-length strings (no terminator is added).
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
} // namespace

// 0x00462140
extern "C" uint32_t __attribute__((thiscall))
FUN_00462140(void* table, const char* directory, const char* file_name) {
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

    // No null check on dat_00719e80 in this loader.
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

    // Record count: a dword store into the table (only the low word is read back afterwards).
    const uint32_t first_dword = *reinterpret_cast<const uint32_t*>(buffer);
    const uint32_t record_count = static_cast<uint32_t>(static_cast<uint16_t>(first_dword));
    *reinterpret_cast<uint32_t*>(self + kTableCount) = first_dword;
    const uint8_t* cursor = buffer + 4;
    {
        void* block = fun_005cde20(record_count * kCashRecordSize + 4);
        uint8_t* first = nullptr;
        if (block != nullptr) {
            CashBlockGuard guard{block};
            *static_cast<uint32_t*>(block) = record_count;
            first = static_cast<uint8_t*>(block) + 4;
            construct_records(first, static_cast<int32_t>(record_count));
            guard.active = false;
        }
        // Stored even when null; the loader never checks it (a non-empty table then crashes below).
        table_records(self) = first;
        dat_00864df8 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(first));
        dat_00864dfc = static_cast<uint16_t>(table_count(self));
    }

    if (table_count(self) != 0) {
        int32_t index = 0;
        uint32_t offset = 0;
        do {
            uint8_t* const record = table_records(self) + offset;
            // Numeric head: four dwords.
            copy32(cursor, 0x00, record, 0x08);
            copy32(cursor, 0x04, record, 0x88);
            copy32(cursor, 0x08, record, 0x8c);
            copy32(cursor, 0x0c, record, 0x0c);
            cursor += kCashFileHeadSize;
            // Four groups of six (dword, byte) pairs: the dwords fill +0x10..+0x6f, the bytes
            // +0x70..+0x87 (group g, pair k: dword at +0x10 + 0x18*g + 4*k, byte at +0x70 + 6*g + k).
            for (uint32_t group = 0; group < kCashGroups; ++group) {
                for (uint32_t pair = 0; pair < kCashPairsPerGroup; ++pair) {
                    copy32(cursor, 0, record, 0x10 + 0x18 * group + 4 * pair);
                    record[0x70 + 6 * group + pair] = cursor[4];
                    cursor += kCashPairFileSize;
                }
            }
            cursor = read_name(cursor, record);
            cursor = read_string(cursor, record, kRecordCode);
            cursor = read_string(cursor, record, kRecordText);
            ++index;
            offset += kCashRecordSize;
        } while (index < static_cast<int32_t>(table_count(self)));
    }

    fun_005cdc59(buffer);
    FUN_005a26c0(file);
    return 1;
}
