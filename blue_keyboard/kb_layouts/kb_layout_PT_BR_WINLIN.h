#pragma once
#include "kb_layout_common.h"

////////////////////////////////////////////////////////////////////
//  Portuguese (Brazil) layout (pt-BR) for Windows / Linux
//  Assumes ABNT2 (the common Brazilian layout with Ç and the extra ISO key).
//  Host OS must be set to Portuguese (Brazil).
//
//  This table maps characters that differ from US and/or are commonly accessed
//  via AltGr on pt-BR for programming and typical typing.
//  Dead-key composition for accented vowels is not implemented here (single-chord).
////////////////////////////////////////////////////////////////////

#ifndef MOD_SHIFT
  #define MOD_SHIFT 0x02
#endif
#ifndef MOD_ALTGR
  #define MOD_ALTGR 0x40
#endif

// ABNT2 has the ISO key left of Z (same usage you already alias as ISO_BSLASH_KEY).
#ifndef BR_KEY_LTGT
  #define BR_KEY_LTGT ISO_BSLASH_KEY
#endif

// On many PT-BR layouts, Ç is on the key where US has ';' (usage 0x33).
#ifndef BR_KEY_C_CEDILLA
  #define BR_KEY_C_CEDILLA 0x33
#endif

// Common HID usages for digits row:
// '1'..'0' = 0x1E..0x27
// '-'      = 0x2D
// '='      = 0x2E

static const KbMapEntry KBMAP_PT_BR_WINLIN[] =
{
  // --------------------------------------------------------------------------
  // Core AltGr symbols used for programming (pt-BR)
  // --------------------------------------------------------------------------
  { U'@',  MOD_ALTGR, 0x1F }, // AltGr+2
  { U'#',  MOD_ALTGR, 0x20 }, // AltGr+3
  { U'€',  MOD_ALTGR, 0x22 }, // AltGr+5 (common on many BR layouts)

  // Brackets / braces (common BR mapping)
  { U'[',  MOD_ALTGR, 0x2F }, // AltGr+[ key position
  { U']',  MOD_ALTGR, 0x30 }, // AltGr+] key position
  { U'{',  MOD_ALTGR | MOD_SHIFT, 0x2F },
  { U'}',  MOD_ALTGR | MOD_SHIFT, 0x30 },

  // Backslash / pipe (often available via AltGr on BR)
  { U'\\', MOD_ALTGR, 0x31 }, // AltGr+\ key position
  { U'|',  MOD_ALTGR | MOD_SHIFT, 0x31 },

  // --------------------------------------------------------------------------
  // Brazilian-specific letters
  // --------------------------------------------------------------------------
  { U'ç',  0x00,      BR_KEY_C_CEDILLA },
  { U'Ç',  MOD_SHIFT, BR_KEY_C_CEDILLA },

  // --------------------------------------------------------------------------
  // ISO extra key (left of Z): < >
  // --------------------------------------------------------------------------
  { U'<',  0x00,      BR_KEY_LTGT },
  { U'>',  MOD_SHIFT, BR_KEY_LTGT },

  // --------------------------------------------------------------------------
  // Common punctuation differences people expect on BR keyboards
  // (These are conservative; remove any that collide with your real layout tests.)
  // --------------------------------------------------------------------------
  { U'?',  MOD_SHIFT, 0x38 }, // Shift + '/' key
  { U'/',  0x00,      0x38 }, // '/' key

  // --------------------------------------------------------------------------
  // Optional: add degree / section / etc if your BR layout provides them
  // --------------------------------------------------------------------------
};


// Accessor used by your dispatcher
static inline const KbMapEntry* kb_get_PT_BR_WINLIN_map(uint16_t& count)
{
  count = (uint16_t)(sizeof(KBMAP_PT_BR_WINLIN) / sizeof(KBMAP_PT_BR_WINLIN[0]));
  return KBMAP_PT_BR_WINLIN;
}
