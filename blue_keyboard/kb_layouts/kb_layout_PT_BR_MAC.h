#pragma once
#include "kb_layout_common.h"

////////////////////////////////////////////////////////////////////
//  Portuguese (Brazil) layout (pt-BR) for macOS
//  Host OS must be set to a Brazilian Portuguese input source.
//
//  This mapping focuses on the characters that are commonly NOT reachable via
//  plain ASCII on many layouts (Option combos) and Brazilian-specific letters.
//  Dead-key composition for ã/á/â/é/ê/ó/ô/õ/ú/ü etc. is not implemented here
//  because a single-chord table cannot reliably emit composed characters.
////////////////////////////////////////////////////////////////////

#ifndef MOD_SHIFT
  #define MOD_SHIFT 0x02
#endif

// On your project, MOD_ALT represents macOS Option (Left Alt).
#ifndef MOD_ALT
  #define MOD_ALT 0x04
#endif

// ISO key left of Z
#ifndef BR_KEY_LTGT
  #define BR_KEY_LTGT ISO_BSLASH_KEY
#endif

// Ç key is commonly on the physical ';' position on many Latin layouts.
// If this is wrong for your specific input source, change this constant.
#ifndef BR_KEY_C_CEDILLA
  #define BR_KEY_C_CEDILLA 0x33
#endif

static const KbMapEntry KBMAP_PT_BR_MAC[] =
{
  // --------------------------------------------------------------------------
  // Core coding symbols (Option combos)
  // --------------------------------------------------------------------------
  { U'@',  MOD_ALT,              0x1F }, // Option+2
  { U'€',  MOD_ALT | MOD_SHIFT,  0x1F }, // Option+Shift+2 (common on mac)

  { U'[',  MOD_ALT,              0x25 }, // Option+8
  { U']',  MOD_ALT,              0x26 }, // Option+9
  { U'{',  MOD_ALT | MOD_SHIFT,  0x25 }, // Option+Shift+8
  { U'}',  MOD_ALT | MOD_SHIFT,  0x26 }, // Option+Shift+9

  // Backslash / pipe (best-effort using ANSI backslash position)
  { U'\\', 0x00,                 ANSI_BSLASH_KEY },       // \
  { U'|',  MOD_SHIFT,            ANSI_BSLASH_KEY },       // |
  { U'\\', MOD_ALT | MOD_SHIFT,  ANSI_BSLASH_KEY },       // Option+Shift+\ (fallback)
  { U'|',  MOD_ALT,              ANSI_BSLASH_KEY },       // Option+\ (fallback)

  // --------------------------------------------------------------------------
  // Brazilian-specific letter
  // --------------------------------------------------------------------------
  { U'ç',  0x00,                 BR_KEY_C_CEDILLA },
  { U'Ç',  MOD_SHIFT,            BR_KEY_C_CEDILLA },

  // --------------------------------------------------------------------------
  // ISO key left of Z: < >
  // --------------------------------------------------------------------------
  { U'<',  0x00,                 BR_KEY_LTGT },
  { U'>',  MOD_SHIFT,            BR_KEY_LTGT },

  // --------------------------------------------------------------------------
  // Useful Mac typography symbols (optional but nice)
  // --------------------------------------------------------------------------
  { 0x00A7 /*§*/, MOD_ALT,              0x23 }, // Option+6
  { 0x00B0 /*°*/, MOD_ALT | MOD_SHIFT,  0x25 }, // Option+Shift+8
  { 0x00A9 /*©*/, MOD_ALT,              0x0A }, // Option+G
  { 0x00AE /*®*/, MOD_ALT,              0x15 }, // Option+R
  { 0x2122 /*™*/, MOD_ALT,              0x1F }, // Option+2
  { 0x2026 /*…*/, MOD_ALT,              0x33 }, // Option+;
  { 0x2013 /*–*/, MOD_ALT,              0x2D }, // Option+-
  { 0x2014 /*—*/, MOD_ALT | MOD_SHIFT,  0x2D }, // Option+Shift+-
};


// Accessor used by your dispatcher
static inline const KbMapEntry* kb_get_PT_BR_MAC_map(uint16_t& count)
{
  count = (uint16_t)(sizeof(KBMAP_PT_BR_MAC) / sizeof(KBMAP_PT_BR_MAC[0]));
  return KBMAP_PT_BR_MAC;
}
