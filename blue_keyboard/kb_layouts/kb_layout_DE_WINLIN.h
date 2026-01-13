#pragma once
#include "kb_layout_common.h"

////////////////////////////////////////////////////////////////////
//  German layout (DE) for Windows / Linux (ISO / QWERTZ)
//  Host OS must be set to German layout.
////////////////////////////////////////////////////////////////////
//
// Notes (physical key positions by HID usage):
// - ö is on US ';' key            => usage 0x33
// - ä is on US quote key          => QUOTE_KEY (0x34)
// - ü is on US '[' key            => usage 0x2F
// - + * ~ is on US ']' key        => usage 0x30   (AltGr gives ~)
// - ß ? is on US '-' key          => usage 0x2D
// - ´ ` (dead keys) on US '=' key => usage 0x2E   (often dead; commit with SPACE)
// - ^ ° (dead key) on key left of '1' => usage 0x35 (often dead; commit with SPACE)
// - # ' is ISO key near Enter     => ISO_HASH_KEY (0x32)
// - < > | is ISO key left of Z    => ISO_BSLASH_KEY (0x64)

#ifndef MOD_SHIFT
  #define MOD_SHIFT 0x02
#endif

// MOD_ALTGR is provided by kb_layout_common.h in your setup (0x40).
// If it isn't, uncomment this:
// #ifndef MOD_ALTGR
//   #define MOD_ALTGR 0x40
// #endif

// Handy
#define SPACE_KEY 0x2C

static const KbMapEntry KBMAP_DE_WINLIN[] = {

  // ----- QWERTZ letter swap -----
  { 'y', 0,         0x1D, 0, 0 },         // y -> press physical Z key
  { 'Y', MOD_SHIFT, 0x1D, 0, 0 },
  { 'z', 0,         0x1C, 0, 0 },         // z -> press physical Y key
  { 'Z', MOD_SHIFT, 0x1C, 0, 0 },

  // ----- Umlauts + ß -----
  { 0x00F6 /*ö*/, 0,         0x33,     0, 0 },
  { 0x00D6 /*Ö*/, MOD_SHIFT, 0x33,     0, 0 },
  { 0x00E4 /*ä*/, 0,         QUOTE_KEY,0, 0 },
  { 0x00C4 /*Ä*/, MOD_SHIFT, QUOTE_KEY,0, 0 },
  { 0x00FC /*ü*/, 0,         0x2F,     0, 0 },   // US '[' key position
  { 0x00DC /*Ü*/, MOD_SHIFT, 0x2F,     0, 0 },
  { 0x00DF /*ß*/, 0,         0x2D,     0, 0 },   // US '-' key position
  { '?',          MOD_SHIFT, 0x2D,     0, 0 },   // Shift+ß

  // ----- ISO keys -----
  { '<', 0,         ISO_BSLASH_KEY, 0, 0 },
  { '>', MOD_SHIFT, ISO_BSLASH_KEY, 0, 0 },
  { '|', MOD_ALTGR, ISO_BSLASH_KEY, 0, 0 },      // AltGr+<>

  { '#', 0,         ISO_HASH_KEY,   0, 0 },      // # near Enter
  { 0x27 /*'*/, MOD_SHIFT, ISO_HASH_KEY, 0, 0 }, // ' = Shift + #

  // ----- Digits row (DE differs from US for some Shift symbols) -----
  // (You usually don’t need these if the host layout is DE and you’re typing ASCII via kb.write,
  //  but we include the “US-different” ones so your mapping layer can be self-contained.)
  { '!', MOD_SHIFT, 0x1E, 0, 0 }, // Shift+1
  { '"', MOD_SHIFT, 0x1F, 0, 0 }, // Shift+2
  { 0x00A7 /*§*/, MOD_SHIFT, 0x20, 0, 0 }, // Shift+3
  { '$', MOD_SHIFT, 0x21, 0, 0 }, // Shift+4
  { '%', MOD_SHIFT, 0x22, 0, 0 }, // Shift+5
  { '&', MOD_SHIFT, 0x23, 0, 0 }, // Shift+6
  { '/', MOD_SHIFT, 0x24, 0, 0 }, // Shift+7
  { '(', MOD_SHIFT, 0x25, 0, 0 }, // Shift+8
  { ')', MOD_SHIFT, 0x26, 0, 0 }, // Shift+9
  { '=', MOD_SHIFT, 0x27, 0, 0 }, // Shift+0

  // ----- + * ~ key (physical US ']' key, usage 0x30 on HID) -----
  { '+', 0,         0x30, 0, 0 },
  { '*', MOD_SHIFT, 0x30, 0, 0 },
  { '~', MOD_ALTGR, 0x30, 0, 0 }, // AltGr + '+' key

  // ----- Dead keys: ^ ° and ´ ` -----
  // ^ is usually a dead key on DE Win/Linux. To output a literal '^', press it then SPACE.
  { '^', 0,         0x35, SPACE_KEY, 0 },         // '^' then Space
  { 0x00B0 /*°*/, MOD_SHIFT, 0x35, 0, 0 },        // Shift + ^ key

  // ´ and ` are usually dead keys on the key right of ß (usage 0x2E).
  { 0x00B4 /*´*/, 0,         0x2E, SPACE_KEY, 0 }, // ´ then Space
  { '`',          MOD_SHIFT, 0x2E, SPACE_KEY, 0 }, // ` then Space

  // ----- AltGr programming symbols -----
  { '@',  MOD_ALTGR, 0x14, 0, 0 }, // AltGr+Q
  { 0x20AC /*€*/, MOD_ALTGR, 0x08, 0, 0 }, // AltGr+E

  { '{',  MOD_ALTGR, 0x24, 0, 0 }, // AltGr+7
  { '[',  MOD_ALTGR, 0x25, 0, 0 }, // AltGr+8
  { ']',  MOD_ALTGR, 0x26, 0, 0 }, // AltGr+9
  { '}',  MOD_ALTGR, 0x27, 0, 0 }, // AltGr+0

  { '\\', MOD_ALTGR, 0x2D, 0, 0 }, // AltGr+ß

  // ----- Common punctuation that is different on DE physical positions -----
  // Hyphen/minus and underscore are on the key right of '.' on DE (physical US '/' key = usage 0x38)
  { '-',  0,         0x38, 0, 0 }, // '-' (DE key right of '.')
  { '_',  MOD_SHIFT, 0x38, 0, 0 }, // '_'

  // Semicolon/colon are on comma/dot with Shift on DE
  { ';',  MOD_SHIFT, 0x36, 0, 0 }, // Shift+comma
  { ':',  MOD_SHIFT, 0x37, 0, 0 }, // Shift+dot

  // (Optional) micro sign is often AltGr+M on DE layouts. If you want it, keep it:
  { 0x00B5 /*µ*/, MOD_ALTGR, 0x10 /*M*/, 0, 0 },
};

static inline const KbMapEntry* kb_get_DE_WINLIN_map(uint16_t& count)
{
  count = (uint16_t)(sizeof(KBMAP_DE_WINLIN) / sizeof(KBMAP_DE_WINLIN[0]));
  return KBMAP_DE_WINLIN;
}
