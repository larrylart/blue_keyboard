#pragma once
#include "kb_layout_common.h"

////////////////////////////////////////////////////////////////////
//  UK layout (UK) for Windows / Linux (ISO)
//  Host OS must be set to English (United Kingdom).
//
//  UK differs from US mainly on:
//   - @ and " swapped positions (Shift+' and Shift+2)
//   - ISO key near Enter: # and ~
//   - ISO key near left shift: \ and |
//   - £ on Shift+3
//   - € on AltGr+4
//   - top-left key (`) produces ¬ with Shift, and ¦ with AltGr (common UK/IE)
////////////////////////////////////////////////////////////////////

#ifndef UK_KEY_TLDE
  #define UK_KEY_TLDE 0x35  // physical key under ESC (grave/tilde on US)
#endif

static const KbMapEntry KBMAP_UK_WINLIN[] = {

  // Currency
  { 0x00A3, MOD_SHIFT, 0x20, 0, 0 }, // £ = Shift+3
  { 0x20AC, MOD_ALTGR, 0x21, 0, 0 }, // € = AltGr+4

  // UK swapped punctuation
  { '@',    MOD_SHIFT, QUOTE_KEY,   0, 0 }, // @ = Shift+'
  { '"',    MOD_SHIFT, 0x1F,        0, 0 }, // " = Shift+2

  // ISO key near Enter: # and ~
  { '#',    0,         ISO_HASH_KEY,    0, 0 }, // # (ISO 0x32)
  { '~',    MOD_SHIFT, ISO_HASH_KEY,    0, 0 }, // ~ = Shift+ISO 0x32

  // ISO key near left shift: \ and |
  { '\\',   0,         ISO_BSLASH_KEY,  0, 0 }, // \ (ISO 0x64)
  { '|',    MOD_SHIFT, ISO_BSLASH_KEY,  0, 0 }, // | = Shift+ISO 0x64

  // Top-left key (under Esc): `, ¬, ¦ (common UK/IE behavior)
  { '`',    0,         UK_KEY_TLDE,  0, 0 }, // `
  { 0x00AC, MOD_SHIFT, UK_KEY_TLDE,  0, 0 }, // ¬ = Shift+`
  { 0x00A6, MOD_ALTGR, UK_KEY_TLDE,  0, 0 }, // ¦ = AltGr+`
};

static inline const KbMapEntry* kb_get_UK_WINLIN_map(uint16_t& count)
{
  count = (uint16_t)(sizeof(KBMAP_UK_WINLIN) / sizeof(KBMAP_UK_WINLIN[0]));
  return KBMAP_UK_WINLIN;
}
