#pragma once
#include "kb_layout_common.h"

////////////////////////////////////////////////////////////////////
//  IE layout (English - Ireland) for Windows / Linux (ISO)
//  Host OS must be set to English (Ireland).
//
//  Ireland layout is largely UK ISO, plus Irish fada support via AltGr dead keys.
//  We map:
//   - UK/IE symbol differences (@, ", #, ~, \, |)
//   - Currency: £ (AltGr+3), € (AltGr+4)
//   - ¬ and ¦ on the top-left key (common)
//   - Irish fadas: á é í ó ú (and uppercase) using dead-key sequence
////////////////////////////////////////////////////////////////////

// top-left key under ESC (grave/tilde physical key on US)
#ifndef IE_KEY_TLDE
  #define IE_KEY_TLDE 0x35
#endif

// Dead-key for acute accent on Irish layout:
// commonly AltGr + apostrophe key, then the vowel. :contentReference[oaicite:2]{index=2}
#ifndef IE_DEAD_ACUTE_KEY
  #define IE_DEAD_ACUTE_KEY QUOTE_KEY  // physical apostrophe key (usage 0x34)
#endif

static const KbMapEntry KBMAP_IE_WINLIN[] =
{
  // --------------------------------------------------------------------------
  // UK/IE swapped punctuation and ISO keys
  // --------------------------------------------------------------------------
  { '@',    MOD_SHIFT, QUOTE_KEY,     0, 0 },        // @ = Shift + '
  { '"',    MOD_SHIFT, 0x1F,          0, 0 },        // " = Shift + 2

  { '#',    MOD_SHIFT, 0x20,          0, 0 },        // # = Shift + 3  (Irish)
  { '~',    MOD_SHIFT, ISO_HASH_KEY,  0, 0 },        // ~ = Shift + ISO 0x32

  { '\\',   0,         ISO_BSLASH_KEY,0, 0 },        // \ = ISO 0x64
  { '|',    MOD_SHIFT, ISO_BSLASH_KEY,0, 0 },        // | = Shift + ISO 0x64

  // --------------------------------------------------------------------------
  // Currency
  // --------------------------------------------------------------------------
  { 0x00A3, MOD_ALTGR, 0x20,          0, 0 },        // £ = AltGr + 3
  { 0x20AC, MOD_ALTGR, 0x21,          0, 0 },        // € = AltGr + 4

  // --------------------------------------------------------------------------
  // Top-left key variants (common on UK/IE)
  // --------------------------------------------------------------------------
  { '`',    0,         IE_KEY_TLDE,   0, 0 },        // `
  { 0x00AC, MOD_SHIFT, IE_KEY_TLDE,   0, 0 },        // ¬ = Shift + `
  { 0x00A6, MOD_ALTGR, IE_KEY_TLDE,   0, 0 },        // ¦ = AltGr + `

  // --------------------------------------------------------------------------
  // Irish fadas (acute accent): dead-key sequence
  //   Step1: AltGr + apostrophe (dead acute)
  //   Step2: vowel key
  // We use the two-chord fields (mods2/key2).
  // --------------------------------------------------------------------------
  { 0x00E1, MOD_ALTGR, IE_DEAD_ACUTE_KEY,  0,      0x04 }, // á : (AltGr+dead) then 'a'
  { 0x00C1, MOD_ALTGR, IE_DEAD_ACUTE_KEY,  MOD_SHIFT, 0x04 }, // Á

  { 0x00E9, MOD_ALTGR, IE_DEAD_ACUTE_KEY,  0,      0x08 }, // é : (AltGr+dead) then 'e'
  { 0x00C9, MOD_ALTGR, IE_DEAD_ACUTE_KEY,  MOD_SHIFT, 0x08 }, // É

  { 0x00ED, MOD_ALTGR, IE_DEAD_ACUTE_KEY,  0,      0x0C }, // í : then 'i'
  { 0x00CD, MOD_ALTGR, IE_DEAD_ACUTE_KEY,  MOD_SHIFT, 0x0C }, // Í

  { 0x00F3, MOD_ALTGR, IE_DEAD_ACUTE_KEY,  0,      0x12 }, // ó : then 'o'
  { 0x00D3, MOD_ALTGR, IE_DEAD_ACUTE_KEY,  MOD_SHIFT, 0x12 }, // Ó

  { 0x00FA, MOD_ALTGR, IE_DEAD_ACUTE_KEY,  0,      0x18 }, // ú : then 'u'
  { 0x00DA, MOD_ALTGR, IE_DEAD_ACUTE_KEY,  MOD_SHIFT, 0x18 }, // Ú
};

static inline const KbMapEntry* kb_get_IE_WINLIN_map(uint16_t& count)
{
  count = (uint16_t)(sizeof(KBMAP_IE_WINLIN) / sizeof(KBMAP_IE_WINLIN[0]));
  return KBMAP_IE_WINLIN;
}
