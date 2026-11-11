#include "session_stat_callbacks.h"
#include "game/player/player_state_owner.h"
#include "game/world/world_state_owner.h"
#include "runtime/allocations.h"

extern "C" {
// 0x00447a40: entity lookup by id in the world object (thiscall, one stack argument); 0 when absent.
void* __attribute__((thiscall)) FUN_00447a40(void* world, uint32_t id);
// 0x004cc090: `*value = *value >= amount ? *value - amount : 0`, with only the low word of the
// second stack argument used (movzx). It is a stdcall leaf (stack arguments, ret 8) that ignores
// ECX; the listing calls it with ECX = the player state, which is dead. Returns the new value in EAX.
// The asm label keeps the undecorated listing symbol (stdcall would otherwise become _name@8).
uint32_t __attribute__((stdcall)) session_saturating_subtract(uint32_t* value,
                                                              uint32_t amount) __asm__("_FUN_004cc090");
}

namespace {
template <typename T>
inline T& field(void* base, uint32_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

inline void* player_state() { return &original_player_state_storage; }
inline void* world() { return &original_world_state_storage; }
// dat_0085cfbc: pointer to the own character's entity (0 before world entry).
inline char* world_own_entity() {
    return reinterpret_cast<char*>(original_world_state_storage.field_0085cfbc);
}

// original_player_state_storage offsets
constexpr uint32_t kStateMaxHp = 20, kStateMaxMp = 24, kStateMaxSp = 28;
constexpr uint32_t kStateOwnId = 4336;
constexpr uint32_t kStateFlag0 = 26837740, kStateFlag1 = 26837741;

// entity offsets
constexpr uint32_t kEntityHp = 0x13c, kEntityMaxHp = 0x140;
constexpr uint32_t kEntityMp = 0x144, kEntityMaxMp = 0x148;
constexpr uint32_t kEntitySp = 0x14c, kEntityMaxSp = 0x150;
}  // namespace

// 0x00560680 (packet 0x0301): own character: player max HP and the two flag bytes; entity: max HP,
// current HP, the flag bytes and the dword at +0x29c = 1. The listing leaves EAX = the entity or 0.
extern "C" void* __attribute__((thiscall))
FUN_00560680(void*, uint32_t id, uint32_t max_hp, uint32_t hp, uint8_t flag0, uint8_t flag1) {
    if (field<uint32_t>(player_state(), kStateOwnId) == id) {
        field<uint32_t>(player_state(), kStateMaxHp) = max_hp;
        field<uint8_t>(player_state(), kStateFlag0) = flag0;
        field<uint8_t>(player_state(), kStateFlag1) = flag1;
    }
    void* entity = FUN_00447a40(world(), id);
    if (entity != nullptr) {
        field<uint32_t>(entity, kEntityMaxHp) = max_hp;
        field<uint32_t>(entity, kEntityHp) = hp;
        field<uint8_t>(entity, 0x280) = flag0;
        field<uint8_t>(entity, 0x281) = flag1;
        field<uint32_t>(entity, 0x29c) = 1;
    }
    return entity;
}

// 0x00560710 (packet 0x0302): own character: player max HP; entity: max HP and current HP.
extern "C" void* __attribute__((thiscall))
FUN_00560710(void*, uint32_t id, uint32_t max_hp, uint32_t hp) {
    if (field<uint32_t>(player_state(), kStateOwnId) == id)
        field<uint32_t>(player_state(), kStateMaxHp) = max_hp;
    void* entity = FUN_00447a40(world(), id);
    if (entity != nullptr) {
        field<uint32_t>(entity, kEntityMaxHp) = max_hp;
        field<uint32_t>(entity, kEntityHp) = hp;
    }
    return entity;
}

// 0x00560d50 (packet 0x0505): add signed deltas (wrapping dword adds) to current HP, MP and SP.
extern "C" void* __attribute__((thiscall))
FUN_00560d50(void*, uint32_t id, uint32_t delta_hp, uint32_t delta_mp, uint32_t delta_sp) {
    void* entity = FUN_00447a40(world(), id);
    if (entity != nullptr) {
        field<uint32_t>(entity, kEntityHp) += delta_hp;
        field<uint32_t>(entity, kEntityMp) += delta_mp;
        field<uint32_t>(entity, kEntitySp) += delta_sp;
    }
    return entity;
}

// 0x00560da0 (packet 0x051F): own character: player max HP/MP/SP; entity: max HP/MP/SP.
extern "C" void* __attribute__((thiscall))
FUN_00560da0(void*, uint32_t id, uint32_t max_hp, uint32_t max_mp, uint32_t max_sp) {
    if (field<uint32_t>(player_state(), kStateOwnId) == id) {
        field<uint32_t>(player_state(), kStateMaxHp) = max_hp;
        field<uint32_t>(player_state(), kStateMaxMp) = max_mp;
        field<uint32_t>(player_state(), kStateMaxSp) = max_sp;
    }
    void* entity = FUN_00447a40(world(), id);
    if (entity != nullptr) {
        field<uint32_t>(entity, kEntityMaxHp) = max_hp;
        field<uint32_t>(entity, kEntityMaxMp) = max_mp;
        field<uint32_t>(entity, kEntityMaxSp) = max_sp;
    }
    return entity;
}

// 0x005619b0 (packet 0x050B): set one maximum on the entity: kind 0 = HP, 1 = SP, 2 = MP; any
// other kind changes nothing. The kind is the low byte of its stack slot (movzx).
extern "C" void* __attribute__((thiscall))
FUN_005619b0(void*, uint32_t id, uint8_t kind, uint32_t value) {
    void* entity = FUN_00447a40(world(), id);
    if (entity != nullptr) {
        switch (kind) {
        case 0:
            field<uint32_t>(entity, kEntityMaxHp) = value;
            break;
        case 1:
            field<uint32_t>(entity, kEntityMaxSp) = value;
            break;
        case 2:
            field<uint32_t>(entity, kEntityMaxMp) = value;
            break;
        default:
            break;
        }
    }
    return entity;
}

// 0x00561a00 (packet 0x050C): subtract (saturating at 0) sp_cost from the own entity's current SP,
// then mp_cost from its current MP. The own entity pointer (dat_0085cfbc) is read again for the
// second call, as in the listing, which reaches the second FUN_004cc090 with a tail jump.
extern "C" uint32_t __attribute__((thiscall))
FUN_00561a00(void*, uint32_t sp_cost, uint32_t mp_cost) {
    if (world_own_entity() == nullptr)
        return 0;
    session_saturating_subtract(reinterpret_cast<uint32_t*>(world_own_entity() + kEntitySp), sp_cost);
    return session_saturating_subtract(reinterpret_cast<uint32_t*>(world_own_entity() + kEntityMp),
                                       mp_cost);
}

namespace {
// The listing counts the bytes up to the NUL (the loop result is a signed dword) and copies the
// string including its NUL into a heap block of length + 1 bytes.
int32_t text_length(const char* text) {
    int32_t length = 0;
    while (text[length] != 0)
        ++length;
    return length;
}

void copy_text(char* destination, const char* source) {
    for (;;) {
        const char c = *source++;
        *destination++ = c;
        if (c == 0)
            break;
    }
}

// Replace the heap copy of the text held in `slot`: free the old block (if any) and clear the slot
// first, then allocate length + 1 bytes, store the new block in the slot and copy the text.
void replace_text(char** slot, const char* text, int32_t length) {
    if (*slot != nullptr) {
        fun_005cdc59(*slot);
        *slot = nullptr;
    }
    char* copy = static_cast<char*>(fun_005cde20(static_cast<uint32_t>(length) + 1));
    *slot = copy;
    copy_text(copy, text);
}

constexpr uint32_t kStateText = 26841216;
constexpr uint32_t kEntityText = 0x2d0;
}  // namespace

// 0x0055ff60 (packet 0x0105): the main character stat block. Stores the 15 stack arguments into the
// player state (a1, a2 and a6 are words, read with `mov ax, [esp+N]`), sets session +0x1ec = 1,
// keeps a heap copy of the text a15 in the player state, then, if the own entity exists, copies the
// maximum values to it, clamps its current HP/MP/SP to them (unsigned) and keeps a heap copy of
// the text on the entity as well. The listing's EAX at `ret` is a leftover (low byte cleared
// pointer, the entity or 0), nothing reads it, so this returns void.
extern "C" void __attribute__((thiscall))
FUN_0055ff60(void* session, uint16_t a1, uint16_t a2, uint32_t max_hp, uint32_t max_mp,
             uint32_t max_sp, uint16_t a6, uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10,
             uint32_t a11, uint32_t a12, uint32_t a13, const uint32_t* a14, const char* a15) {
    void* state = player_state();
    field<uint32_t>(state, kStateMaxHp) = max_hp;
    field<uint32_t>(state, kStateMaxMp) = max_mp;
    field<uint32_t>(state, kStateMaxSp) = max_sp;
    field<uint32_t>(state, 36) = a7;
    field<uint32_t>(state, 40) = a8;
    field<uint32_t>(state, 32) = a9;
    field<uint16_t>(state, 168) = a1;
    field<uint16_t>(state, 170) = a2;
    field<uint16_t>(state, 4332) = a6;
    field<uint32_t>(state, 4320) = a11;
    field<uint32_t>(state, 4324) = a12;
    field<uint32_t>(state, 4328) = a13;
    field<uint32_t>(state, 26839316) = a14[0];
    field<uint32_t>(state, 26839320) = a14[1];
    field<uint32_t>(state, 26839324) = a14[2];
    field<uint32_t>(state, 26839328) = a14[3];
    field<uint32_t>(state, 8) = a10;
    field<uint32_t>(session, 0x1ec) = 1;

    const int32_t length = text_length(a15);
    if (length > 0)
        replace_text(&field<char*>(state, kStateText), a15, length);

    void* entity = FUN_00447a40(world(), field<uint32_t>(state, kStateOwnId));
    if (entity == nullptr)
        return;
    const uint32_t hp_limit = field<uint32_t>(state, kStateMaxHp);
    field<uint32_t>(entity, kEntityMaxHp) = hp_limit;
    field<uint32_t>(entity, kEntityMaxMp) = field<uint32_t>(state, kStateMaxMp);
    field<uint32_t>(entity, kEntityMaxSp) = field<uint32_t>(state, kStateMaxSp);
    if (hp_limit < field<uint32_t>(entity, kEntityHp))
        field<uint32_t>(entity, kEntityHp) = hp_limit;
    if (field<uint32_t>(entity, kEntityMaxMp) < field<uint32_t>(entity, kEntityMp))
        field<uint32_t>(entity, kEntityMp) = field<uint32_t>(entity, kEntityMaxMp);
    if (field<uint32_t>(entity, kEntityMaxSp) < field<uint32_t>(entity, kEntitySp))
        field<uint32_t>(entity, kEntitySp) = field<uint32_t>(entity, kEntityMaxSp);
    if (length > 0)
        replace_text(&field<char*>(entity, kEntityText), a15, length);
}
