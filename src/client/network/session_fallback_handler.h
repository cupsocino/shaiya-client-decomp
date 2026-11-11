#pragma once
#include <stdint.h>

struct PacketReader;

extern "C" {
// 0x005667a0, session slot 0x630: the handler for opcodes the dispatcher (network/dispatcher.cpp,
// unknown_packet) has no case for. thiscall with ECX = the session object, two stack arguments
// (the opcode dword, whose low word is the opcode, and the packet reader), callee pops 8, no result.
// It reads the packet itself. Opcodes handled (low word of `opcode`):
//   0x1cxx -> FUN_00571930(self, opcode, reader)      0x1dxx -> FUN_00570680(self, opcode, reader)
//   0x0114 / 0x0115 server time offset, 0x030b / 0x030c / 0x030d cooldown record list at
//   player state +26854948, 0x0809 / 0x080a call FUN_00495540 on the window at [[dat_0071cfb0]]+0x2d0.
void __attribute__((thiscall)) FUN_005667a0(void* self, uint32_t opcode, PacketReader* reader);
}
