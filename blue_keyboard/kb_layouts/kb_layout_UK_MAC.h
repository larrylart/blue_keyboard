#pragma once
#include "kb_layout_common.h"

////////////////////////////////////////////////////////////////////
//  UK layout (British) for macOS
//  Host OS must be set to: British (not "British - PC").
////////////////////////////////////////////////////////////////////

static const KbMapEntry KBMAP_UK_MAC[] =
{
  // Core currency + common programming symbols
  { 0x00A3, MOD_SHIFT, 0x20, 0, 0 }, // £  = Shift + 3
  { 0x0023, MOD_ALT,   0x20, 0, 0 }, // #  = Option + 3
  { 0x20AC, MOD_ALT,   0x1F, 0, 0 }, // €  = Option + 2

  // UK macOS: @ lives on Shift+2 (if you see " instead, wrong input source)
  { 0x0040, MOD_SHIFT, 0x1F, 0, 0 }, // @  = Shift + 2

  // Quotes: on British macOS the double-quote is typically Shift + apostrophe key
  { 0x0022, MOD_SHIFT, QUOTE_KEY, 0, 0 }, // " = Shift + '

  // Tilde/backtick tend to live on the ISO key next to Z on UK ISO boards.
  // (This is the key that differs between ISO/ANSI hardware.)
  { 0x0060, 0x00,      ISO_BSLASH_KEY, 0, 0 }, // ` (best-effort)
  { 0x007E, MOD_SHIFT, ISO_BSLASH_KEY, 0, 0 }, // ~ (best-effort)

  // Backslash/pipe are usually on the ANSI backslash key position on macOS British.
  { 0x005C, 0x00,      ANSI_BSLASH_KEY, 0, 0 }, // \ (best-effort)
  { 0x007C, MOD_SHIFT, ANSI_BSLASH_KEY, 0, 0 }, // | (best-effort)
};

static inline const KbMapEntry* kb_get_UK_MAC_map(uint16_t& count)
{
  count = (uint16_t)(sizeof(KBMAP_UK_MAC) / sizeof(KBMAP_UK_MAC[0]));
  return KBMAP_UK_MAC;
}
