// Item.SData loader, lifted from the original instruction listing.
//
// 0x00460ca0: thiscall(table, directory, file), ret 8, returns 1 on success and 0 on failure (full EAX).
//
// This unit is built with the MSVC exception ABI (_seh.cpp) because the original function has one
// SEH/C++ unwind state: state 0 covers only the vector construction of the item records of one
// item type, and its unwind action (0x006ad950) frees the raw block holding the count and the records.
//
// File layout of the decrypted body: a dword whose low byte is the number of item types, then per
// type a dword item count followed by that many items. One item is two length-prefixed strings
// (+0x00 and +0x04 of the 0x5c-byte record) and a packed block of 0x51 bytes copied field by field.
//
// Table (this) object: +0x04 per-type item array pointers, +0x08 per-type item counts (dword each),
// +0x0c type count (the dword store of the first body dword; only its low byte is read back).
#include <stdint.h>
#include <stddef.h>

#include "archive/saf.h"
#include "archive/saf_open.h"
#include "platform/support.hpp"
#include "runtime/allocations.h"
#include "runtime/array_lifetime.h"
#include "runtime/close_entry.h"
#include "runtime/stdio_format.h"
#include "startup/game_init.h"

extern "C" {
extern const char dat_006be1d4[]; // path format, "%s%s"-like: (directory, file name)
extern const char dat_006bc484[]; // open mode "rb"
extern const char dat_006bfe18[]; // 40 character signature plus NUL (41 bytes)
// Original archive and CRT helpers that have no C++ declaration yet (all cdecl).
int32_t FUN_0040cf80(int32_t descriptor, int32_t offset, int32_t origin); // seek
uint8_t FUN_005c1320(void* buffer, int32_t* length, const void* key);     // decrypt, result in AL
uint32_t FUN_005c1270(const void* buffer, uint32_t length);               // CRC (result unused here)
// Element constructor and destructor of one item record (still instruction listings).
void __attribute__((thiscall)) FUN_00460ae0(void*);
void __attribute__((thiscall)) FUN_00460b00(void*);
}

namespace {
// Size of one item record in memory and of the packed block that follows its two strings in the file.
constexpr uint32_t kItemRecordSize = 0x5c;
constexpr uint32_t kItemFileFieldsSize = 0x51;
// Offsets inside the item record, and inside the table (this) object.
constexpr uint32_t kItemName = 0x00;
constexpr uint32_t kItemDescription = 0x04;
constexpr uint32_t kItemDurability = 0x2c; // u16, doubled by the region feature 0x35 post-pass
constexpr uint32_t kTableArrays = 0x04;
constexpr uint32_t kTableCounts = 0x08;
constexpr uint32_t kTableTypeCount = 0x0c;
// Region feature queried through the feature object (slot 1) before the doubling post-pass.
constexpr uint32_t kRegionFeatureDoubling = 0x35;
// The post-pass only looks at type indices 0..0x40 (dat_00461424 has 0x41 entries).
constexpr uint32_t kLastDoublingTypeIndex = 0x40;

typedef uint16_t __attribute__((aligned(1), may_alias)) U16u;
typedef uint32_t __attribute__((aligned(1), may_alias)) U32u;

inline void copy8(const uint8_t* src, uint32_t from, uint8_t* item, uint32_t to) {
    item[to] = src[from];
}
inline void copy16(const uint8_t* src, uint32_t from, uint8_t* item, uint32_t to) {
    *reinterpret_cast<U16u*>(item + to) = *reinterpret_cast<const U16u*>(src + from);
}
inline void copy32(const uint8_t* src, uint32_t from, uint8_t* item, uint32_t to) {
    *reinterpret_cast<U32u*>(item + to) = *reinterpret_cast<const U32u*>(src + from);
}

// All 51 stores of one item, in the order of the original (store VA in the comments).
// copyN(src, source offset, item, destination offset); the source offsets cover 0..0x50 exactly once.
// The original writes destination +0x08 three times (sources 0x00, 0x0e and 0x0f); the last one wins.
inline void copy_item_fields(const uint8_t* src, uint8_t* item) {
    copy8(src, 0x00, item, 0x08);  // 461005
    copy8(src, 0x01, item, 0x09);  // 461012
    copy8(src, 0x02, item, 0x0a);  // 461020
    copy8(src, 0x03, item, 0x0b);  // 46102e
    copy16(src, 0x04, item, 0x0c);  // 46103d
    copy8(src, 0x06, item, 0x0e);  // 46104c
    copy8(src, 0x07, item, 0x0f);  // 46105c
    copy8(src, 0x08, item, 0x10);  // 46106a
    copy8(src, 0x09, item, 0x11);  // 461078
    copy8(src, 0x0a, item, 0x12);  // 461086
    copy8(src, 0x0b, item, 0x13);  // 461094
    copy8(src, 0x0c, item, 0x14);  // 4610a2
    copy8(src, 0x0d, item, 0x15);  // 4610b0
    copy8(src, 0x0e, item, 0x08);  // 4610be
    copy8(src, 0x0f, item, 0x08);  // 4610cd
    copy16(src, 0x10, item, 0x16);  // 4610dc
    copy16(src, 0x12, item, 0x18);  // 4610ee
    copy16(src, 0x14, item, 0x1a);  // 4610fd
    copy16(src, 0x16, item, 0x1c);  // 46110f
    copy16(src, 0x18, item, 0x1e);  // 461121
    copy16(src, 0x1a, item, 0x20);  // 461133
    copy16(src, 0x1c, item, 0x22);  // 461145
    copy8(src, 0x1e, item, 0x24);  // 461156
    copy8(src, 0x1f, item, 0x25);  // 461166
    copy8(src, 0x20, item, 0x26);  // 461174
    copy8(src, 0x21, item, 0x28);  // 461182
    copy8(src, 0x22, item, 0x29);  // 461190
    copy8(src, 0x23, item, 0x2a);  // 46119e
    copy8(src, 0x24, item, 0x2b);  // 4611ac
    copy16(src, 0x25, item, 0x2c);  // 4611bb
    copy16(src, 0x27, item, 0x2e);  // 4611ce
    copy16(src, 0x29, item, 0x30);  // 4611e0
    copy16(src, 0x2b, item, 0x32);  // 4611f2
    copy16(src, 0x2d, item, 0x34);  // 461201
    copy16(src, 0x2f, item, 0x36);  // 461213
    copy16(src, 0x31, item, 0x38);  // 461225
    copy16(src, 0x33, item, 0x3a);  // 461237
    copy16(src, 0x35, item, 0x3c);  // 461249
    copy16(src, 0x37, item, 0x3e);  // 46125b
    copy16(src, 0x39, item, 0x40);  // 46126d
    copy16(src, 0x3b, item, 0x42);  // 46127f
    copy16(src, 0x3d, item, 0x44);  // 461291
    copy16(src, 0x3f, item, 0x46);  // 4612a3
    copy8(src, 0x41, item, 0x48);  // 4612b4
    copy8(src, 0x42, item, 0x49);  // 4612c4
    copy32(src, 0x43, item, 0x4c);  // 4612d2
    copy32(src, 0x47, item, 0x50);  // 4612e3
    copy16(src, 0x4b, item, 0x54);  // 4612f4
    copy16(src, 0x4d, item, 0x56);  // 461306
    copy8(src, 0x4f, item, 0x58);  // 461317
    copy8(src, 0x50, item, 0x59);  // 461329
}

// Types whose items get the u16 at +0x2c doubled when region feature 0x35 is on: the byte table
// dat_00461424 (0x41 entries) selects entry 0 of the jump table dat_0046141c (the doubling code at
// 0x00461370) for type indices 0..14 and 44..64, and entry 1 (nothing to do, 0x004613af) for 15..43.
inline bool type_gets_doubled(uint32_t type_index) {
    return type_index <= 14 || (type_index >= 44 && type_index <= kLastDoublingTypeIndex);
}

// Unwind action 0x006ad950: release the raw block (count dword plus records) while the vector
// construction of one type's items is running.
struct ItemBlockGuard {
    void* block;
    bool active = true;
    ~ItemBlockGuard() noexcept(false) {
        if (active)
            fun_005cdc59(block);
    }
};

// The call stays an explicit protected operation so that unwinding reaches the guard.
__declspec(noinline) void construct_items(void* first, int32_t count) {
    fun_005ce6bb(first, kItemRecordSize, count, FUN_00460ae0, FUN_00460b00);
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
inline const uint8_t* read_string(const uint8_t* src, uint8_t* item, uint32_t slot) {
    const uint32_t length = *reinterpret_cast<const U32u*>(src);
    src += 4;
    if (length != 0) {
        uint8_t* text = static_cast<uint8_t*>(fun_005cde20(length));
        *reinterpret_cast<uint8_t**>(item + slot) = text;
        for (uint32_t i = 0; i < length; ++i)
            text[i] = src[i];
        src += length;
    }
    return src;
}

// The table fields are re-read at every use, as in the original.
inline uint32_t type_count(const uint8_t* self) {
    return *reinterpret_cast<const uint8_t*>(self + kTableTypeCount);
}
inline uint8_t**& item_arrays(uint8_t* self) {
    return *reinterpret_cast<uint8_t***>(self + kTableArrays);
}
inline int32_t*& item_counts(uint8_t* self) {
    return *reinterpret_cast<int32_t**>(self + kTableCounts);
}
} // namespace

// 0x00460ca0
extern "C" uint32_t __attribute__((thiscall))
FUN_00460ca0(void* table, const char* directory, const char* file_name) {
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
    // Unlike the Skill loader, the original computes the CRC over the first sizes[0] bytes of the
    // decrypted body and never compares it (EAX is overwritten at 0x00460e61); only the call remains.
    (void)FUN_005c1270(buffer, sizes[0]);
    (void)stored_crc;

    // Type count: a dword store into the table (only the low byte is read back afterwards).
    const uint32_t first_dword = *reinterpret_cast<const uint32_t*>(buffer);
    *reinterpret_cast<uint32_t*>(self + kTableTypeCount) = first_dword;
    const uint8_t* cursor = buffer + 4;
    item_arrays(self) = static_cast<uint8_t**>(fun_005cde20((first_dword & 0xffu) * 4));
    if (item_arrays(self) == nullptr) {
        FUN_005a26c0(file);
        fun_005cdc59(buffer);
        return 0;
    }
    item_counts(self) = static_cast<int32_t*>(fun_005cde20(type_count(self) * 4));
    if (item_counts(self) == nullptr) {
        FUN_005a26c0(file);
        fun_005cdc59(buffer);
        return 0;
    }

    if (type_count(self) != 0) {
        uint32_t type = 0;
        do {
            const uint32_t count_dword = *reinterpret_cast<const U32u*>(cursor);
            item_counts(self)[type] = static_cast<int32_t>(count_dword);
            cursor += 4;
            if (count_dword == 0) {
                // No items of this type: no array and no post-pass.
                item_arrays(self)[type] = nullptr;
            } else {
                void* block = fun_005cde20(count_dword * kItemRecordSize + 4);
                uint8_t* items = nullptr;
                if (block != nullptr) {
                    ItemBlockGuard guard{block};
                    *static_cast<uint32_t*>(block) = count_dword;
                    items = static_cast<uint8_t*>(block) + 4;
                    construct_items(items, static_cast<int32_t>(count_dword));
                    guard.active = false;
                }
                item_arrays(self)[type] = items;
                if (item_arrays(self)[type] == nullptr) {
                    FUN_005a26c0(file);
                    fun_005cdc59(buffer);
                    return 0;
                }
                if (item_counts(self)[type] > 0) {
                    int32_t index = 0;
                    uint32_t offset = 0;
                    do {
                        uint8_t* const item = item_arrays(self)[type] + offset;
                        cursor = read_string(cursor, item, kItemName);
                        cursor = read_string(cursor, item, kItemDescription);
                        copy_item_fields(cursor, item);
                        cursor += kItemFileFieldsSize;
                        ++index;
                        offset += kItemRecordSize;
                    } while (index < item_counts(self)[type]);
                }

                // Region feature 0x35 post-pass: double the u16 at +0x2c of every item of the types
                // listed in type_gets_doubled (the original does not test dat_007191d4 for null).
                if (uint8_t(dat_007191d4->callbacks->callback_004(dat_007191d4,
                                                                  kRegionFeatureDoubling)) != 0 &&
                    type <= kLastDoublingTypeIndex && type_gets_doubled(type)) {
                    if (item_counts(self)[type] > 0) {
                        int32_t index = 0;
                        uint32_t offset = 0;
                        do {
                            U16u* const durability = reinterpret_cast<U16u*>(
                                item_arrays(self)[type] + offset + kItemDurability);
                            *durability = static_cast<uint16_t>(*durability << 1);
                            ++index;
                            offset += kItemRecordSize;
                        } while (index < item_counts(self)[type]);
                    }
                }
            }
            ++type;
        } while (type < type_count(self));
    }

    fun_005cdc59(buffer);
    FUN_005a26c0(file);
    return 1;
}
