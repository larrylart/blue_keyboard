#pragma once
#include "../RawKeyboard.h"

////////////////////////////////////////////////////////////////////
// Common key aliases that are stable across TinyUSB naming variants.
// We use raw HID usage IDs as fallbacks so builds are portable.
////////////////////////////////////////////////////////////////////

// QUOTE key (USAGE 0x34)
#ifndef QUOTE_KEY
  #if defined(HID_KEY_APOSTROPHE)
    #define QUOTE_KEY HID_KEY_APOSTROPHE
  #elif defined(HID_KEY_QUOTE)
    #define QUOTE_KEY HID_KEY_QUOTE
  #else
    #define QUOTE_KEY 0x34
  #endif
#endif

// ISO "#/~" (USAGE 0x32)
#ifndef ISO_HASH_KEY
  #if defined(HID_KEY_NON_US_HASH)
    #define ISO_HASH_KEY HID_KEY_NON_US_HASH
  #elif defined(HID_KEY_EUROPE_1)
    #define ISO_HASH_KEY HID_KEY_EUROPE_1
  #else
    #define ISO_HASH_KEY 0x32
  #endif
#endif

// ISO "\ |" (USAGE 0x64)
#ifndef ISO_BSLASH_KEY
  #if defined(HID_KEY_NON_US_BACKSLASH)
    #define ISO_BSLASH_KEY HID_KEY_NON_US_BACKSLASH
  #elif defined(HID_KEY_EUROPE_2)
    #define ISO_BSLASH_KEY HID_KEY_EUROPE_2
  #else
    #define ISO_BSLASH_KEY 0x64
  #endif
#endif

// ANSI "\ |" (USAGE 0x31) — the normal US backslash key position
#ifndef ANSI_BSLASH_KEY
  #define ANSI_BSLASH_KEY 0x31
#endif

////////////////////////////////////////////////////////////////////
// Modifier bitmasks for RawKeyboard::chord(mods, usage)
// (USB HID modifier byte: LCtrl=0x01, LShift=0x02, LAlt=0x04, LGUI=0x08,
//                         RCtrl=0x10, RShift=0x20, RAlt=0x40, RGUI=0x80)
////////////////////////////////////////////////////////////////////
#ifndef MOD_CTRL
  #define MOD_CTRL  0x01
#endif
#ifndef MOD_SHIFT
  #define MOD_SHIFT 0x02
#endif
#ifndef MOD_ALT
  #define MOD_ALT   0x04   // macOS Option is typically L-Alt from HID perspective
#endif
#ifndef MOD_ALTGR
  #define MOD_ALTGR 0x40   // R-Alt (AltGr on Win/Linux)
#endif

////////////////////////////////////////////////////////////////////
// Mapping entry: send one chord, and optionally a second chord (dead-key style).
// For most layouts you’ll use only (mods1,key1) and keep (mods2,key2) = 0.
////////////////////////////////////////////////////////////////////
struct KbMapEntry
{
  uint32_t cp;     // Unicode codepoint (e.g. 0x20AC for €)
  uint8_t  mods1;  // modifier bitmask
  uint8_t  key1;   // HID usage
  uint8_t  mods2;  // optional second chord modifiers
  uint8_t  key2;   // optional second chord key
};

// Convenience: tap or chord
static inline void sendChordOrTap(RawKeyboard& kb, uint8_t mods, uint8_t usage)
{
  if (usage == 0) return;
  if (mods == 0) kb.tapUsage(usage);
  else kb.chord(mods, usage);
}

////////////////////////////////////////////////////////////////////
// TV media remap result
// Used by TV layout profiles to indicate whether a media action
// should be sent as a Consumer Control usage or as a keyboard key.
////////////////////////////////////////////////////////////////////
struct TvMediaRemap
{
    bool    asKeyboard; // true => send as keyboard usage (RawKeyboard::sendRaw)
    uint8_t usage;      // keyboard HID usage OR consumer low byte
};
