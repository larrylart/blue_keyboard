#pragma once
#include "kb_layout_common.h"

////////////////////////////////////////////////////////////////////
//  US layout (US) for macOS (ANSI)
//  Host OS must be set to U.S.
//
//  For ASCII, macOS US matches the library defaults, so overrides are mostly for
//  common Unicode symbols users expect on Mac via Option.
////////////////////////////////////////////////////////////////////

static const KbMapEntry KBMAP_US_MAC[] =
{
  // Euro on macOS US: Option+Shift+2
  { 0x20AC, MOD_ALT | MOD_SHIFT, 0x1F /* '2' */, 0, 0 }, // €

  // Pound on macOS US: Option+3
  { 0x00A3, MOD_ALT, 0x20 /* '3' */, 0, 0 }, // £

  // Section sign: Option+6
  { 0x00A7, MOD_ALT, 0x23 /* '6' */, 0, 0 }, // §

  // Degree: Option+Shift+8
  { 0x00B0, MOD_ALT | MOD_SHIFT, 0x25 /* '8' */, 0, 0 }, // °

  // Trademark: Option+2
  { 0x2122, MOD_ALT, 0x1F /* '2' */, 0, 0 }, // ™

  // Registered: Option+R
  { 0x00AE, MOD_ALT, 0x15 /* 'R' */, 0, 0 }, // ®

  // Copyright: Option+G
  { 0x00A9, MOD_ALT, 0x0A /* 'G' */, 0, 0 }, // ©

  // Ellipsis: Option+;
  { 0x2026, MOD_ALT, 0x33 /* ';' */, 0, 0 }, // …

  // Em dash: Option+Shift+-
  { 0x2014, MOD_ALT | MOD_SHIFT, 0x2D /* '-' */, 0, 0 }, // —
  // En dash: Option+-
  { 0x2013, MOD_ALT, 0x2D /* '-' */, 0, 0 }, // –

  // Curly quotes (common in Mac typography)
  // Left double quote: Option+[
  { 0x201C, MOD_ALT, 0x2F /* '[' */, 0, 0 }, // “
  // Right double quote: Option+Shift+[
  { 0x201D, MOD_ALT | MOD_SHIFT, 0x2F /* '[' */, 0, 0 }, // ”
  // Left single quote: Option+]
  { 0x2018, MOD_ALT, 0x30 /* ']' */, 0, 0 }, // ‘
  // Right single quote: Option+Shift+]
  { 0x2019, MOD_ALT | MOD_SHIFT, 0x30 /* ']' */, 0, 0 }, // ’
};

static inline const KbMapEntry* kb_get_US_MAC_map(uint16_t& count)
{
  count = (uint16_t)(sizeof(KBMAP_US_MAC) / sizeof(KBMAP_US_MAC[0]));
  return KBMAP_US_MAC;
}
