#pragma once
#include "kb_layout_common.h"

////////////////////////////////////////////////////////////////////
//  Spanish (Spain) layout (ES) for Windows / Linux (ISO)
//  Host OS must be set to Spanish (Spain).
//
//  This table maps characters that differ from US and/or are accessed via AltGr,
//  plus the Spanish-specific letters/punctuation.
//  Dead-key composition for accented vowels is not implemented here (single-chord).
////////////////////////////////////////////////////////////////////

#ifndef MOD_SHIFT
  #define MOD_SHIFT 0x02
#endif
#ifndef MOD_ALTGR
  #define MOD_ALTGR 0x40
#endif

// ISO key left of Z (provides < > on ES ISO)
#ifndef ES_KEY_LTGT
  #define ES_KEY_LTGT ISO_BSLASH_KEY
#endif

// Spanish Ñ is typically on the physical key where US has ';' (usage 0x33)
#ifndef ES_KEY_N_TILDE
  #define ES_KEY_N_TILDE 0x33
#endif

// Spanish Ç is not standard on ES (it is on Catalan/Spanish variants). Not included by default.

// Useful HID usages:
//  '1'..'0' = 0x1E..0x27
//  '-'      = 0x2D
//  '='      = 0x2E
//  '['      = 0x2F
//  ']'      = 0x30
//  '\'      = 0x31
//  ';'      = 0x33
//  QUOTE    = 0x34 (QUOTE_KEY from common header)
//  ','      = 0x36
//  '.'      = 0x37
//  '/'      = 0x38

static const KbMapEntry KBMAP_ES_WINLIN[] =
{
  // --------------------------------------------------------------------------
  // Spanish-specific letters / punctuation
  // --------------------------------------------------------------------------
  { U'ñ',  0x00,      ES_KEY_N_TILDE },
  { U'Ñ',  MOD_SHIFT, ES_KEY_N_TILDE },

  // Inverted punctuation (commonly present on ES)
  // Often on AltGr combos; provide conservative bindings:
  { 0x00A1 /*¡*/, MOD_ALTGR, 0x1E }, // AltGr+1 (best-effort)
  { 0x00BF /*¿*/, MOD_ALTGR, 0x1F }, // AltGr+2 (best-effort)

  // --------------------------------------------------------------------------
  // Core programmer symbols on ES (AltGr)
  // --------------------------------------------------------------------------
  { U'@',  MOD_ALTGR, 0x1F }, // AltGr+2
  { U'#',  MOD_ALTGR, 0x20 }, // AltGr+3
  { U'€',  MOD_ALTGR, 0x08 }, // AltGr+E (common on ES)

  // Brackets / braces
  { U'[',  MOD_ALTGR, 0x2F }, // AltGr+[ key position
  { U']',  MOD_ALTGR, 0x30 }, // AltGr+] key position
  { U'{',  MOD_ALTGR | MOD_SHIFT, 0x2F },
  { U'}',  MOD_ALTGR | MOD_SHIFT, 0x30 },

  // Backslash / pipe (commonly available via AltGr)
  { U'\\', MOD_ALTGR, 0x31 },
  { U'|',  MOD_ALTGR | MOD_SHIFT, 0x31 },

  // Tilde (often AltGr+4 on ES)
  { U'~',  MOD_ALTGR, 0x21 }, // AltGr+4 (best-effort)

  // --------------------------------------------------------------------------
  // ISO key left of Z: < >
  // --------------------------------------------------------------------------
  { U'<',  0x00,      ES_KEY_LTGT },
  { U'>',  MOD_SHIFT, ES_KEY_LTGT },
};

// Accessor used by your dispatcher
static inline const KbMapEntry* kb_get_ES_WINLIN_map(uint16_t& count)
{
  count = (uint16_t)(sizeof(KBMAP_ES_WINLIN) / sizeof(KBMAP_ES_WINLIN[0]));
  return KBMAP_ES_WINLIN;
}

