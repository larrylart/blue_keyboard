#pragma once
#include "kb_layout_common.h"

////////////////////////////////////////////////////////////////////
//  IE layout for macOS
//  Host OS must be set to an Irish/British-family input source.
//
//  Includes:
//   - Common UK/IE-style symbols (best-effort; differs by chosen mac input source)
//   - Currency: £ and €
//   - Irish fadas using macOS dead-key: Option+E then vowel
////////////////////////////////////////////////////////////////////

// Top-left key (under ESC) on most keyboards = usage 0x35
#ifndef IE_KEY_TLDE
  #define IE_KEY_TLDE 0x35
#endif

// macOS acute dead key is typically Option+E, then vowel. 
#ifndef IE_DEAD_ACUTE_KEY
  #define IE_DEAD_ACUTE_KEY 0x08  // 'E' usage
#endif

static const KbMapEntry KBMAP_IE_MAC[] =
{
  // --------------------------------------------------------------------------
  // Currency (British/Irish family on macOS)
  // --------------------------------------------------------------------------
  { 0x00A3, MOD_SHIFT, 0x20, 0, 0 }, // £ = Shift+3  
  { 0x20AC, MOD_ALT,   0x1F, 0, 0 }, // € = Option+2 

  // --------------------------------------------------------------------------
  // Common UK/IE-style symbols (best-effort; depends on selected mac input source)
  // --------------------------------------------------------------------------
  { '#',    MOD_ALT,   0x20, 0, 0 },        // # = Option+3 (common) 
  { '@',    MOD_SHIFT, 0x1F, 0, 0 },        // @ = Shift+2 (British-family)
  { '"',    MOD_SHIFT, QUOTE_KEY, 0, 0 },   // " = Shift+' (British-family)

  // Backslash / pipe: often on ANSI backslash position on mac; keep as best-effort.
  { '\\',   0,         ANSI_BSLASH_KEY, 0, 0 }, // \
  { '|',    MOD_SHIFT, ANSI_BSLASH_KEY, 0, 0 }, // |

  // Tilde/backtick: physical key under ESC.
  { '`',    0,         IE_KEY_TLDE, 0, 0 }, // `
  { '~',    MOD_SHIFT, IE_KEY_TLDE, 0, 0 }, // ~

  // --------------------------------------------------------------------------
  // Irish fadas (acute): Option+E (dead key), then vowel
  // --------------------------------------------------------------------------
  // á Á
  { 0x00E1, MOD_ALT, IE_DEAD_ACUTE_KEY, 0,       0x04 }, // á : Opt+E, a
  { 0x00C1, MOD_ALT, IE_DEAD_ACUTE_KEY, MOD_SHIFT,0x04 }, // Á : Opt+E, Shift+a

  // é É
  { 0x00E9, MOD_ALT, IE_DEAD_ACUTE_KEY, 0,       0x08 }, // é : Opt+E, e
  { 0x00C9, MOD_ALT, IE_DEAD_ACUTE_KEY, MOD_SHIFT,0x08 }, // É

  // í Í
  { 0x00ED, MOD_ALT, IE_DEAD_ACUTE_KEY, 0,       0x0C }, // í : Opt+E, i
  { 0x00CD, MOD_ALT, IE_DEAD_ACUTE_KEY, MOD_SHIFT,0x0C }, // Í

  // ó Ó
  { 0x00F3, MOD_ALT, IE_DEAD_ACUTE_KEY, 0,       0x12 }, // ó : Opt+E, o
  { 0x00D3, MOD_ALT, IE_DEAD_ACUTE_KEY, MOD_SHIFT,0x12 }, // Ó

  // ú Ú
  { 0x00FA, MOD_ALT, IE_DEAD_ACUTE_KEY, 0,       0x18 }, // ú : Opt+E, u
  { 0x00DA, MOD_ALT, IE_DEAD_ACUTE_KEY, MOD_SHIFT,0x18 }, // Ú
};

static inline const KbMapEntry* kb_get_IE_MAC_map(uint16_t& count)
{
  count = (uint16_t)(sizeof(KBMAP_IE_MAC) / sizeof(KBMAP_IE_MAC[0]));
  return KBMAP_IE_MAC;
}
