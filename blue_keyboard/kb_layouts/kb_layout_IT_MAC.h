#pragma once
#include "kb_layout_common.h"

////////////////////////////////////////////////////////////////////
// Italian layout for macOS (IT_MAC)
//
// This file uses your table format:
//   { codepoint, mods1, key1, mods2, key2 }
// where (mods2,key2) is used for 2-step sequences (dead-key then base key).
//
// References:
// - Apple Italian: @ is Option + ò. 
// - Backtick on Italian mac: ` is Option + \\. 
// - « » are Option+\ and Option+Shift+\ (typography shortcuts). 
// - Accents by dead keys (generic macOS):
//     Grave: Option+` then vowel; Acute: Option+E then vowel. 
// - Euro key for mac layouts: Italian = Option+I, Italian-Pro = Option+E. 
// - Italian physical letter positions (è/ò/à/ù)
////////////////////////////////////////////////////////////////////

// ---- HID usages used below (USB HID Keyboard/Keypad page) ----
#ifndef HID_KEY_A
  #define HID_KEY_A 0x04
  #define HID_KEY_E 0x08
  #define HID_KEY_I 0x0C
  #define HID_KEY_O 0x12
  #define HID_KEY_U 0x18
  #define HID_KEY_N 0x11
#endif

// Physical “backslash” key (US usage) is 0x31.
// On many ISO boards/mac mappings, this is the physical key used with Option to get ` and «». 
#ifndef IT_MAC_KEY_BACKSLASH
  #define IT_MAC_KEY_BACKSLASH 0x31
#endif

// Physical grave/backtick key (US usage) is 0x35.
// Used for the generic macOS dead-key “Option + `”. 
#ifndef IT_MAC_KEY_GRAVE
  #define IT_MAC_KEY_GRAVE 0x35
#endif

// Italian dedicated-letter keys (physical positions):
//  - è is right of P  -> use the US '[' key position (0x2F) as in your Win/Linux IT mapping.
//  - ì is right of 0  -> use the US '=' key position (0x2E).
//  - ò is right of L  -> use QUOTE_KEY (0x34).
//  - à is right of ò  -> often maps to the US '\' key position (0x31) or an ISO-specific key.
//  - ù is right of à  -> ISO_HASH_KEY (0x32).
//
// The “à” key position can vary depending on ISO/ANSI handling. If you see à coming out wrong,
// adjust IT_MAC_KEY_A_GRAVE first.
#ifndef IT_MAC_KEY_E_GRAVE
  #define IT_MAC_KEY_E_GRAVE 0x2F
#endif
#ifndef IT_MAC_KEY_I_GRAVE
  #define IT_MAC_KEY_I_GRAVE 0x2E
#endif
#ifndef IT_MAC_KEY_O_GRAVE
  #define IT_MAC_KEY_O_GRAVE QUOTE_KEY
#endif
#ifndef IT_MAC_KEY_A_GRAVE
  // Most common “good first guess” for Italian: same physical as US backslash key.
  #define IT_MAC_KEY_A_GRAVE 0x31
#endif
#ifndef IT_MAC_KEY_U_GRAVE
  #define IT_MAC_KEY_U_GRAVE ISO_HASH_KEY
#endif

////////////////////////////////////////////////////////////////////
// Mapping table
////////////////////////////////////////////////////////////////////
static const KbMapEntry KBMAP_IT_MAC[] =
{
  // --------------------------------------------------------------------------
  // Dedicated Italian letters (type directly via the Italian input source)
  // --------------------------------------------------------------------------
  { 0x00E8 /*è*/, 0,        IT_MAC_KEY_E_GRAVE, 0, 0 },
  { 0x00C8 /*È*/, MOD_SHIFT,IT_MAC_KEY_E_GRAVE, 0, 0 },

  { 0x00EC /*ì*/, 0,        IT_MAC_KEY_I_GRAVE, 0, 0 },
  { 0x00CC /*Ì*/, MOD_SHIFT,IT_MAC_KEY_I_GRAVE, 0, 0 },

  { 0x00F2 /*ò*/, 0,        IT_MAC_KEY_O_GRAVE, 0, 0 },
  { 0x00D2 /*Ò*/, MOD_SHIFT,IT_MAC_KEY_O_GRAVE, 0, 0 },

  { 0x00E0 /*à*/, 0,        IT_MAC_KEY_A_GRAVE, 0, 0 },
  { 0x00C0 /*À*/, MOD_SHIFT,IT_MAC_KEY_A_GRAVE, 0, 0 },

  { 0x00F9 /*ù*/, 0,        IT_MAC_KEY_U_GRAVE, 0, 0 },
  { 0x00D9 /*Ù*/, MOD_SHIFT,IT_MAC_KEY_U_GRAVE, 0, 0 },

  // --------------------------------------------------------------------------
  // High-value symbols specific to Apple Italian
  // --------------------------------------------------------------------------

  // @ is Option + ò on Apple Italian. 
  { '@', MOD_ALT, IT_MAC_KEY_O_GRAVE, 0, 0 },

  // Backtick ` is commonly Option + \ on Italian macOS. 
  { '`', MOD_ALT, IT_MAC_KEY_BACKSLASH, 0, 0 },

  // « » are Option+\ and Option+Shift+\  (Mac typography shortcuts). 
  { 0x00AB /*«*/, MOD_ALT,                      IT_MAC_KEY_BACKSLASH, 0, 0 },
  { 0x00BB /*»*/, (uint8_t)(MOD_ALT|MOD_SHIFT), IT_MAC_KEY_BACKSLASH, 0, 0 },

  // Euro:
  // According to common mac layout shortcut lists: Italian = Option+I, Italian-Pro = Option+E. 
  // If you target Italian-Pro, change HID_KEY_I below to HID_KEY_E.
  { 0x20AC /*€*/, MOD_ALT, HID_KEY_I, 0, 0 },

  // --------------------------------------------------------------------------
  // Dead-key support (generic macOS, works across many input sources)
  // This lets you emit accents even if the user’s layout treats accents as dead keys.
  //
  // Grave: Option+` then vowel.  
  // Acute: Option+E then vowel.  
  // --------------------------------------------------------------------------

  // Grave (Option+` then vowel)
  { 0x00E0 /*à*/, MOD_ALT, IT_MAC_KEY_GRAVE, 0,        HID_KEY_A },
  { 0x00C0 /*À*/, MOD_ALT, IT_MAC_KEY_GRAVE, MOD_SHIFT,HID_KEY_A },

  { 0x00E8 /*è*/, MOD_ALT, IT_MAC_KEY_GRAVE, 0,        HID_KEY_E },
  { 0x00C8 /*È*/, MOD_ALT, IT_MAC_KEY_GRAVE, MOD_SHIFT,HID_KEY_E },

  { 0x00EC /*ì*/, MOD_ALT, IT_MAC_KEY_GRAVE, 0,        HID_KEY_I },
  { 0x00CC /*Ì*/, MOD_ALT, IT_MAC_KEY_GRAVE, MOD_SHIFT,HID_KEY_I },

  { 0x00F2 /*ò*/, MOD_ALT, IT_MAC_KEY_GRAVE, 0,        HID_KEY_O },
  { 0x00D2 /*Ò*/, MOD_ALT, IT_MAC_KEY_GRAVE, MOD_SHIFT,HID_KEY_O },

  { 0x00F9 /*ù*/, MOD_ALT, IT_MAC_KEY_GRAVE, 0,        HID_KEY_U },
  { 0x00D9 /*Ù*/, MOD_ALT, IT_MAC_KEY_GRAVE, MOD_SHIFT,HID_KEY_U },

  // Acute (Option+E then vowel)
  { 0x00E1 /*á*/, MOD_ALT, HID_KEY_E, 0,        HID_KEY_A },
  { 0x00C1 /*Á*/, MOD_ALT, HID_KEY_E, MOD_SHIFT,HID_KEY_A },

  { 0x00E9 /*é*/, MOD_ALT, HID_KEY_E, 0,        HID_KEY_E },
  { 0x00C9 /*É*/, MOD_ALT, HID_KEY_E, MOD_SHIFT,HID_KEY_E },

  { 0x00ED /*í*/, MOD_ALT, HID_KEY_E, 0,        HID_KEY_I },
  { 0x00CD /*Í*/, MOD_ALT, HID_KEY_E, MOD_SHIFT,HID_KEY_I },

  { 0x00F3 /*ó*/, MOD_ALT, HID_KEY_E, 0,        HID_KEY_O },
  { 0x00D3 /*Ó*/, MOD_ALT, HID_KEY_E, MOD_SHIFT,HID_KEY_O },

  { 0x00FA /*ú*/, MOD_ALT, HID_KEY_E, 0,        HID_KEY_U },
  { 0x00DA /*Ú*/, MOD_ALT, HID_KEY_E, MOD_SHIFT,HID_KEY_U },

  // Tilde as a dead key is Option+N then vowel on macOS. 
  // (Italian doesn’t use these normally, but it’s handy for dev / multilingual typing.)
  { 0x00F1 /*ñ*/, MOD_ALT, HID_KEY_N, 0,        HID_KEY_N },
  { 0x00D1 /*Ñ*/, MOD_ALT, HID_KEY_N, MOD_SHIFT,HID_KEY_N },
};

static inline const KbMapEntry* kb_get_IT_MAC_map(uint16_t& count)
{
  count = (uint16_t)(sizeof(KBMAP_IT_MAC) / sizeof(KBMAP_IT_MAC[0]));
  return KBMAP_IT_MAC;
}
