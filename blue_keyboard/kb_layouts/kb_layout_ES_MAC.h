#pragma once
#include "kb_layout_common.h"

////////////////////////////////////////////////////////////////////
//  Spanish (Spain) layout (ES) for macOS (ISO)
//  Host OS must be set to Spanish input source.
//
//  Uses KbMapEntry 2-step support to emit dead-key compositions for:
//    á é í ó ú, Á É Í Ó Ú
//    ü Ü
//    ñ Ñ   (also mapped to physical Ñ key as single step)
//
//  Also provides common programming symbols typically reached via Option.
//  Note: Some Option-symbol locations can vary by exact Spanish input source.
////////////////////////////////////////////////////////////////////
//
// KbMapEntry fields:
//   { cp, mods1, key1, mods2, key2 }
// If mods2/key2 are 0, it is a single chord/tap.
// If mods2/key2 are non-zero, it emits step1 then step2.
//
// Modifier bits come from kb_layout_common.h:
//   MOD_SHIFT, MOD_ALT (Option), MOD_ALTGR, etc.
//
// HID usage reminders (Keyboard/Keypad page):
//   A=0x04 E=0x08 I=0x0C N=0x11 O=0x12 U=0x18
//   1..0 = 0x1E..0x27
//   Space=0x2C
//   '/'=0x38
//   ISO_HASH_KEY = 0x32, ISO_BSLASH_KEY = 0x64, QUOTE_KEY = 0x34
////////////////////////////////////////////////////////////////////

// Physical Ñ key on many ES layouts matches the US ';' position (usage 0x33).
#ifndef ES_KEY_N_TILDE
  #define ES_KEY_N_TILDE 0x33
#endif

static const KbMapEntry KBMAP_ES_MAC[] =
{
  // --------------------------------------------------------------------------
  // Spanish letters / punctuation (physical keys)
  // --------------------------------------------------------------------------
  { U'ñ',  0x00,      ES_KEY_N_TILDE, 0, 0 },
  { U'Ñ',  MOD_SHIFT, ES_KEY_N_TILDE, 0, 0 },

  // --------------------------------------------------------------------------
  // macOS dead-key compositions (best-effort, widely used across Mac layouts)
  // --------------------------------------------------------------------------
  // Acute accent: Option+E then vowel
  { U'á',  MOD_ALT, 0x08 /*E*/, 0x00, 0x04 /*A*/ },
  { U'é',  MOD_ALT, 0x08 /*E*/, 0x00, 0x08 /*E*/ },
  { U'í',  MOD_ALT, 0x08 /*E*/, 0x00, 0x0C /*I*/ },
  { U'ó',  MOD_ALT, 0x08 /*E*/, 0x00, 0x12 /*O*/ },
  { U'ú',  MOD_ALT, 0x08 /*E*/, 0x00, 0x18 /*U*/ },

  { U'Á',  MOD_ALT, 0x08 /*E*/, MOD_SHIFT, 0x04 /*A*/ },
  { U'É',  MOD_ALT, 0x08 /*E*/, MOD_SHIFT, 0x08 /*E*/ },
  { U'Í',  MOD_ALT, 0x08 /*E*/, MOD_SHIFT, 0x0C /*I*/ },
  { U'Ó',  MOD_ALT, 0x08 /*E*/, MOD_SHIFT, 0x12 /*O*/ },
  { U'Ú',  MOD_ALT, 0x08 /*E*/, MOD_SHIFT, 0x18 /*U*/ },

  // Tilde: Option+N then letter (ñ/Ñ can also be produced this way)
  { U'ñ',  MOD_ALT, 0x11 /*N*/, 0x00, 0x11 /*N*/ },
  { U'Ñ',  MOD_ALT, 0x11 /*N*/, MOD_SHIFT, 0x11 /*N*/ },

  // Umlaut: Option+U then vowel
  { U'ü',  MOD_ALT, 0x18 /*U*/, 0x00, 0x18 /*U*/ },
  { U'Ü',  MOD_ALT, 0x18 /*U*/, MOD_SHIFT, 0x18 /*U*/ },

  // Tilde itself as a character: Option+N then Space
  { U'~',  MOD_ALT, 0x11 /*N*/, 0x00, 0x2C /*Space*/ },

  // Acute accent as a character: Option+E then Space
  { 0x00B4 /*´*/, MOD_ALT, 0x08 /*E*/, 0x00, 0x2C /*Space*/ },

  // Grave accent as a character: Option+` then Space (usage 0x35 is grave/tilde key on US)
  { U'`',  MOD_ALT, 0x35,        0x00, 0x2C /*Space*/ },

  // --------------------------------------------------------------------------
  // Core programmer symbols (mac Option combos — commonly consistent)
  // --------------------------------------------------------------------------
  { U'@',  MOD_ALT,              0x1F /*2*/, 0, 0 },                 // Option+2
  { U'€',  MOD_ALT | MOD_SHIFT,  0x1F /*2*/, 0, 0 },                 // Option+Shift+2

  { U'[',  MOD_ALT,              0x25 /*8*/, 0, 0 },                 // Option+8
  { U']',  MOD_ALT,              0x26 /*9*/, 0, 0 },                 // Option+9
  { U'{',  MOD_ALT | MOD_SHIFT,  0x25 /*8*/, 0, 0 },                 // Option+Shift+8
  { U'}',  MOD_ALT | MOD_SHIFT,  0x26 /*9*/, 0, 0 },                 // Option+Shift+9

  // Backslash and pipe vary by input source; these are common workable fallbacks
  { U'\\', MOD_ALT | MOD_SHIFT,  0x24 /*7*/, 0, 0 },                 // Option+Shift+7
  { U'|',  MOD_ALT,              0x24 /*7*/, 0, 0 },                 // Option+7

  // Less/greater (ISO key left of Z)
  { U'<',  0x00,                 ISO_BSLASH_KEY, 0, 0 },
  { U'>',  MOD_SHIFT,            ISO_BSLASH_KEY, 0, 0 },

  // Slash/question are physical and usually stable
  { U'/',  0x00,                 0x38 /*/*/, 0, 0 },
  { U'?',  MOD_SHIFT,            0x38 /*/*/, 0, 0 },

  // Inverted punctuation: layout-dependent; provide common mac-style fallbacks
  { 0x00A1 /*¡*/, MOD_ALT,        0x1E /*1*/, 0, 0 },                // Option+1 (fallback)
  { 0x00BF /*¿*/, MOD_ALT,        0x1F /*2*/, 0, 0 },                // Option+2 (fallback)
};

static inline const KbMapEntry* kb_get_ES_MAC_map(uint16_t& count)
{
  count = (uint16_t)(sizeof(KBMAP_ES_MAC) / sizeof(KBMAP_ES_MAC[0]));
  return KBMAP_ES_MAC;
}
