#pragma once
#include <stdint.h>

// Session vtable callbacks for the HP/MP/SP packets (original session vtable slots).
// Names retain the original procedure addresses. All are thiscall with ECX = the session object
// (only 0x0055ff60 uses it); stack arguments are popped by the callee. They are never called
// directly: the packet readers (network/packet_*.cpp) reach them through the session vtable.
//
// Layouts, as far as the code shows (offsets in bytes):
//   entity (found with FUN_00447a40(&world, id)):
//     +0x13c current HP   +0x140 maximum HP   (the 0x0055ff60 clamp: current = min(current, maximum))
//     +0x144 current MP   +0x148 maximum MP
//     +0x14c current SP   +0x150 maximum SP
//     +0x280, +0x281 bytes (0x0301), +0x29c dword = 1 (0x0301), +0x2d0 char* copy of a name string
//   player state (original_player_state_storage): +20 / +24 / +28 maximum HP / MP / SP of the own
//     character, +4336 the own character id.
extern "C" {
// 0x0055ff60, packet 0x0105 (slot 0x28): main character stat block, 15 stack arguments.
// Stores (state offset): a3/a4/a5 -> +20/+24/+28 (max HP/MP/SP), a9 -> +32, a7/a8 -> +36/+40,
// a10 -> +8, a1/a2 (words) -> +168/+170, a6 (word) -> +4332, a11/a12/a13 -> +4320/+4324/+4328,
// *a14 (4 dwords) -> +26839316.., text a15 -> heap copy at +26841216 (and entity +0x2d0).
// Sets session +0x1ec = 1.
void __attribute__((thiscall))
FUN_0055ff60(void* session, uint16_t a1, uint16_t a2, uint32_t max_hp, uint32_t max_mp,
             uint32_t max_sp, uint16_t a6, uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10,
             uint32_t a11, uint32_t a12, uint32_t a13, const uint32_t* a14, const char* a15);
// 0x00560680, packet 0x0301 (slot 0xb8): (id, max, current, byte, byte). Returns the entity or 0.
void* __attribute__((thiscall))
FUN_00560680(void* session, uint32_t id, uint32_t max_hp, uint32_t hp, uint8_t flag0,
             uint8_t flag1);
// 0x00560710, packet 0x0302 (slot 0xbc): (id, max, current). Returns the entity or 0.
void* __attribute__((thiscall)) FUN_00560710(void* session, uint32_t id, uint32_t max_hp,
                                             uint32_t hp);
// 0x00560d50, packet 0x0505 (slot 0xf0): (id, delta HP, delta MP, delta SP). Returns the entity or 0.
void* __attribute__((thiscall)) FUN_00560d50(void* session, uint32_t id, uint32_t delta_hp,
                                             uint32_t delta_mp, uint32_t delta_sp);
// 0x00560da0, packet 0x051F (slot 0xf4): (id, max HP, max MP, max SP). Returns the entity or 0.
void* __attribute__((thiscall)) FUN_00560da0(void* session, uint32_t id, uint32_t max_hp,
                                             uint32_t max_mp, uint32_t max_sp);
// 0x005619b0, packet 0x050B (slot 0x12c): (id, kind, value). Returns the entity or 0.
void* __attribute__((thiscall)) FUN_005619b0(void* session, uint32_t id, uint8_t kind,
                                             uint32_t value);
// 0x00561a00, packet 0x050C (slot 0x130): (sp_cost, mp_cost) taken from the own entity.
// Returns what the last FUN_004cc090 call returned (0 when there is no own entity).
uint32_t __attribute__((thiscall)) FUN_00561a00(void* session, uint32_t sp_cost,
                                                uint32_t mp_cost);
}
