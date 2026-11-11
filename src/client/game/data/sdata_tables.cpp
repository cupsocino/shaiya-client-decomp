#include "sdata_tables.h"

namespace {
template <typename T>
inline T load(const void* base, uint32_t offset) {
    return *reinterpret_cast<const T*>(static_cast<const char*>(base) + offset);
}

inline void* zero_record(void* record, uint32_t dwords) {
    uint32_t* cursor = static_cast<uint32_t*>(record);
    for (uint32_t i = 0; i < dwords; ++i)
        cursor[i] = 0;
    return record;
}
}  // namespace

// 0x004609e0: skill level record lookup. The fail paths are `count < id` (unsigned, 16 bit),
// `id == 0`, a null row, `level == 0` and `level - 1 >= 9` (signed, level is zero extended).
// The reads happen in the original order: count, rows pointer, row, level.
extern "C" void* __attribute__((thiscall)) FUN_004609e0(void* table, uint16_t id, uint8_t level) {
    if (load<uint16_t>(table, 0x1c) < id)
        return nullptr;
    if (id == 0)
        return nullptr;
    const uint32_t* rows = load<const uint32_t*>(table, 0x18);
    char* row = reinterpret_cast<char*>(rows[id - 1]);
    if (row == nullptr)
        return nullptr;
    const int32_t wide_level = level;
    if (wide_level - 1 >= kSkillLevelsPerSkill)
        return nullptr;
    if (level == 0)
        return nullptr;
    return row + (static_cast<uint32_t>(wide_level) - 1) * kSkillLevelRecordSize;
}

// 0x00460a30: record by index in the 0x94 table.
extern "C" void* __attribute__((thiscall)) FUN_00460a30(void* table, uint16_t index) {
    if (index >= load<uint16_t>(table, 0x24))
        return nullptr;
    return load<char*>(table, 0x20) + static_cast<uint32_t>(index) * kSdata94RecordSize;
}

// 0x00460a60: record search by name (inlined strcmp equality). The count is read once, the name
// pointer of every record is read inside the loop, a null name pointer faults as in the original.
extern "C" void* __attribute__((thiscall)) FUN_00460a60(void* table, const char* name) {
    const int32_t count = load<uint16_t>(table, 0x24);
    if (count <= 0)
        return nullptr;
    char* const base = load<char*>(table, 0x20);
    for (int32_t i = 0; i < count; ++i) {
        char* const record = base + static_cast<uint32_t>(i) * kSdata94RecordSize;
        const char* a = load<const char*>(record, 4);
        const char* b = name;
        for (;;) {
            const uint8_t ca = static_cast<uint8_t>(*a);
            if (ca != static_cast<uint8_t>(*b))
                break;  // different
            if (ca == 0)
                return record;  // both ended together
            ++a;
            ++b;
        }
    }
    return nullptr;
}

// 0x00460ae0: Item.SData record, 0x17 dwords.
extern "C" void* __attribute__((thiscall)) FUN_00460ae0(void* record) {
    return zero_record(record, 0x17);
}

// 0x00460b40: Monster.SData record, 0xb dwords.
extern "C" void* __attribute__((thiscall)) FUN_00460b40(void* record) {
    return zero_record(record, 0xb);
}

// 0x00460b80: one skill level record, 0x22 dwords (0x88 bytes).
extern "C" void* __attribute__((thiscall)) FUN_00460b80(void* record) {
    return zero_record(record, 0x22);
}

// 0x00460be0: record of the 0x94 table, 0x25 dwords.
extern "C" void* __attribute__((thiscall)) FUN_00460be0(void* record) {
    return zero_record(record, 0x25);
}
