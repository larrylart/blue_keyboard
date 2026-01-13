#pragma once
#include "kb_layout_common.h"

////////////////////////////////////////////////////////////////////
//  Portuguese (Portugal) layout (pt-PT) for macOS
//  Host OS must be set to Portuguese input source.
//
//  This table covers the "non-US / programmer" characters that typically
//  require Option (Alt) on macOS Portuguese layouts.
//  Accented vowels are normally produced via dead keys on macOS; single-chord
//  mapping cannot reliably generate composed characters without sequence support.
////////////////////////////////////////////////////////////////////

#ifndef MOD_SHIFT
  #define MOD_SHIFT 0x02
#endif

// On your project, MOD_ALT should represent macOS Option (Left Alt).
// If kb_layout_common.h already defines MOD_ALT, this does nothing.
#ifndef MOD_ALT
  #define MOD_ALT 0x04
#endif

// Some helpers for readability (HID usages):
//  '1'..'0' = 0x1E..0x27
//  '2' = 0x1F, '3' = 0x20, '7' = 0x24, '8' = 0x25, '9' = 0x26
//  '[' = 0x2F, ']' = 0x30 on US physical positions (used as physical keys only)

static const KbMapEntry KBMAP_PT_PT_MAC[] =
{
  // --------------------------------------------------------------------------
  // Core programming symbols on Portuguese mac layout (Option combinations)
  // --------------------------------------------------------------------------
  { U'@',  MOD_ALT,              0x1F }, // Option+2
  { U'€',  MOD_ALT,              0x20 }, // Option+3

  { U'[',  MOD_ALT,              0x25 }, // Option+8
  { U']',  MOD_ALT,              0x26 }, // Option+9
  { U'{',  MOD_ALT | MOD_SHIFT,  0x25 }, // Option+Shift+8
  { U'}',  MOD_ALT | MOD_SHIFT,  0x26 }, // Option+Shift+9

  // Many PT mac layouts also provide backslash/pipe with Option on a key near the right side.
  // Without the exact physical map constants available here, bind to a common, practical pair:
  { U'\\', MOD_ALT | MOD_SHIFT,  ANSI_BSLASH_KEY }, // best-effort
  { U'|',  MOD_ALT,              ANSI_BSLASH_KEY }, // best-effort

  // --------------------------------------------------------------------------
  // Common Portuguese letters that are NOT ASCII (single-key on PT keyboards)
  // Note: These are best-effort physical bindings; if your PT mac input source
  // differs, you may want to adjust these key usages to the correct physical key.
  // --------------------------------------------------------------------------
  // ç / Ç is typically on the key where US has ';'
  { U'ç',  0x00,                 0x33 }, // ';' physical key
  { U'Ç',  MOD_SHIFT,            0x33 },

  // Ordinals often exist on PT keyboards (ª º). Commonly tied to the key near ç.
  // Map them to QUOTE_KEY as a reasonable default.
  { 0x00AA /*ª*/, 0x00,          QUOTE_KEY },
  { 0x00BA /*º*/, MOD_SHIFT,     QUOTE_KEY },

  // --------------------------------------------------------------------------
  // Useful punctuation that differs on many PT layouts
  // If you find any of these are wrong for your specific PT mac input source,
  // delete or adjust them (they’re not required for the core “programmer” set).
  // --------------------------------------------------------------------------
  { U'«',  MOD_ALT,              0x2D }, // best-effort (often on the key after 0)
  { U'»',  MOD_ALT | MOD_SHIFT,  0x2D }, // best-effort
};


// Accessor used by your dispatcher
static inline const KbMapEntry* kb_get_PT_PT_MAC_map(uint16_t& count)
{
  count = (uint16_t)(sizeof(KBMAP_PT_PT_MAC) / sizeof(KBMAP_PT_PT_MAC[0]));
  return KBMAP_PT_PT_MAC;
}
