#pragma once
#include <stdint.h>

// SData table lookups and record initialisers. Names retain the original procedure addresses.
// All of them are thiscall leaves: the table or record pointer arrives in ECX, stack arguments are
// popped by the callee.
//
// The tables are loaded from Skill.SData (skill levels) and Cash.SData (0x94-byte records,
// loaded by 0x00462140).
// Only the offsets used by these routines are named:
//   skill table (this):   +0x18 u32* rows (rows[id - 1] -> first level record of the skill, or 0)
//                         +0x1c u16 row count
//   0x94 table (this):    +0x20 u8*  records (stride 0x94)
//                         +0x24 u16  record count
//   0x94 record:          +0x04 char* name (NUL terminated)
constexpr int32_t kSkillLevelsPerSkill = 9;   // level records per skill in Skill.SData
constexpr uint32_t kSkillLevelRecordSize = 0x88;
constexpr uint32_t kSdata94RecordSize = 0x94;

extern "C" {
// 0x004609e0: skill level record for (id, level), or 0 when id or level is out of range.
void* __attribute__((thiscall)) FUN_004609e0(void* table, uint16_t id, uint8_t level);
// 0x00460a30: record number `index` of the 0x94 table, or 0 when index >= count.
void* __attribute__((thiscall)) FUN_00460a30(void* table, uint16_t index);
// 0x00460a60: first record of the 0x94 table whose name equals `name`, or 0.
void* __attribute__((thiscall)) FUN_00460a60(void* table, const char* name);
// 0x00460ae0 / 0x00460b40 / 0x00460b80 / 0x00460be0: zero a record (0x5c, 0x2c, 0x88, 0x94 bytes)
// and return it. Used as element constructors by the SData loaders.
void* __attribute__((thiscall)) FUN_00460ae0(void* record);
void* __attribute__((thiscall)) FUN_00460b40(void* record);
void* __attribute__((thiscall)) FUN_00460b80(void* record);
void* __attribute__((thiscall)) FUN_00460be0(void* record);
}
