#pragma once
// Requires in your .ino BEFORE this header:
//   #include "RawKeyboard.h"

// ========================= Profile selection =========================
// Pick exactly ONE that matches the HOST OS + keyboard layout:
//
// Windows / Linux:
// #define PROFILE_UK_WINLIN
// #define PROFILE_IE_WINLIN
// #define PROFILE_US_WINLIN
//
// macOS:
// #define PROFILE_UK_MAC
// #define PROFILE_IE_MAC
// #define PROFILE_US_MAC
//
// If none defined, default to UK on Windows/Linux:
#if !defined(PROFILE_UK_WINLIN) && !defined(PROFILE_IE_WINLIN) && !defined(PROFILE_US_WINLIN) && \
    !defined(PROFILE_UK_MAC)    && !defined(PROFILE_IE_MAC)    && !defined(PROFILE_US_MAC)
  #define PROFILE_UK_WINLIN
#endif

// On Win/Linux, some systems implement AltGr strictly as Right-Alt, others as Ctrl+Alt.
// Enable this if AltGr requires Ctrl+Alt on your target:
// #define ALTGR_IMPL_CTRLALT 1


// ===================== Raw usage IDs for ISO-only keys =====================
// Use numeric HID usage values to avoid any TinyUSB name differences.
// 0x32 = "Keyboard Non-US # and ~"; 0x64 = "Keyboard Non-US \ and |"
#define USAGE_NON_US_HASH        0x32
#define USAGE_NON_US_BACKSLASH   0x64

// Keypad (only needed for US Win Alt+Numpad — you can ignore if not using US/Win)
#ifndef HID_KEYPAD_0
  #ifdef HID_KEY_KEYPAD_0
    #define HID_KEYPAD_0 HID_KEY_KEYPAD_0
    #define HID_KEYPAD_1 HID_KEY_KEYPAD_1
    #define HID_KEYPAD_2 HID_KEY_KEYPAD_2
    #define HID_KEYPAD_3 HID_KEY_KEYPAD_3
    #define HID_KEYPAD_4 HID_KEY_KEYPAD_4
    #define HID_KEYPAD_5 HID_KEY_KEYPAD_5
    #define HID_KEYPAD_6 HID_KEY_KEYPAD_6
    #define HID_KEYPAD_7 HID_KEY_KEYPAD_7
    #define HID_KEYPAD_8 HID_KEY_KEYPAD_8
    #define HID_KEYPAD_9 HID_KEY_KEYPAD_9
  #endif
#endif

// ---------- Keypad usages (fallback to raw usage IDs if TinyUSB names differ) ----------
#ifndef KEYPAD_0_USAGE
  #if defined(HID_KEYPAD_0)
    #define KEYPAD_0_USAGE HID_KEYPAD_0
    #define KEYPAD_1_USAGE HID_KEYPAD_1
    #define KEYPAD_2_USAGE HID_KEYPAD_2
    #define KEYPAD_3_USAGE HID_KEYPAD_3
    #define KEYPAD_4_USAGE HID_KEYPAD_4
    #define KEYPAD_5_USAGE HID_KEYPAD_5
    #define KEYPAD_6_USAGE HID_KEYPAD_6
    #define KEYPAD_7_USAGE HID_KEYPAD_7
    #define KEYPAD_8_USAGE HID_KEYPAD_8
    #define KEYPAD_9_USAGE HID_KEYPAD_9
  #else
    // HID Keyboard/Keypad Page: KP1..KP9 = 0x59..0x61, KP0 = 0x62
    #define KEYPAD_1_USAGE 0x59
    #define KEYPAD_2_USAGE 0x5A
    #define KEYPAD_3_USAGE 0x5B
    #define KEYPAD_4_USAGE 0x5C
    #define KEYPAD_5_USAGE 0x5D
    #define KEYPAD_6_USAGE 0x5E
    #define KEYPAD_7_USAGE 0x5F
    #define KEYPAD_8_USAGE 0x60
    #define KEYPAD_9_USAGE 0x61
    #define KEYPAD_0_USAGE 0x62
  #endif
#endif

// ---------- Canonical key aliases (works across TinyUSB name variants) ----------
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

// ===== Helpers that use RAW reports (modifier bitmasks) =====
inline void tap(RawKeyboard& kb, uint8_t usage)                 { kb.tapUsage(usage); }
inline void chordShift(RawKeyboard& kb, uint8_t usage)          { kb.shiftUsage(usage); }
inline void chordAltLeft(RawKeyboard& kb, uint8_t usage)        { kb.chord(0x04, usage); } // LAlt
inline void chordAltGr(RawKeyboard& kb, uint8_t usage)          { kb.chord(0x40, usage); } // RAlt
inline void chordCtrlAltGr(RawKeyboard& kb, uint8_t usage)      { kb.chord(0x01 | 0x40, usage); } // LCtrl+RAlt
inline void chordCtrlAltLeft(RawKeyboard& kb, uint8_t usage)    { kb.chord(0x01 | 0x04, usage); } // LCtrl+LAlt

// AltGr on Win/Linux (some systems = RAlt, others = Ctrl+Alt)
inline void sendAltGrCombo(RawKeyboard& kb, uint8_t baseUsage) {
#ifdef ALTGR_IMPL_CTRLALT
  chordCtrlAltLeft(kb, baseUsage);
#else
  chordAltGr(kb, baseUsage);
#endif
}

// ---------- Alt+Numpad using RAW reports (Windows US path) ----------
inline void altNumpadRaw(RawKeyboard& kb, const char* digits) {
  // Hold Left-Alt (modifier bit 0x04) and tap keypad digits as raw usages
  // Press Alt only once, then send each digit in the same report sequence.
  // (Windows accepts either style; this approach is reliable.)
  KeyReport rpt = {};
  rpt.modifiers = 0x04; // LAlt
  kb.sendReport(&rpt);  // press Alt
  delay(2);

  for (const char* p = digits; *p; ++p) {
    uint8_t u = 0;
    switch (*p) {
      case '0': u = KEYPAD_0_USAGE; break;
      case '1': u = KEYPAD_1_USAGE; break;
      case '2': u = KEYPAD_2_USAGE; break;
      case '3': u = KEYPAD_3_USAGE; break;
      case '4': u = KEYPAD_4_USAGE; break;
      case '5': u = KEYPAD_5_USAGE; break;
      case '6': u = KEYPAD_6_USAGE; break;
      case '7': u = KEYPAD_7_USAGE; break;
      case '8': u = KEYPAD_8_USAGE; break;
      case '9': u = KEYPAD_9_USAGE; break;
      default: continue;
    }

    // press digit
    KeyReport down = rpt;
    down.keys[0] = u;
    kb.sendReport(&down);
    delay(2);

    // release digit (keep Alt held)
    kb.sendReport(&rpt);
    delay(2);
  }

  // release Alt
  KeyReport up = {};
  kb.sendReport(&up);
  delay(2);
}

// ======================= Layout-specific ASCII overrides =======================
static inline bool sendChar_UK(RawKeyboard& kb, unsigned char c) {
  switch (c) {
    case '@': chordShift(kb, QUOTE_KEY);            return true; // @ = Shift + '
    case '"': chordShift(kb, 0x1F /* '2' */);       return true; // " = Shift + '2'
    case '#': tap(kb, ISO_HASH_KEY);                return true; // # on ISO 0x32
    case '~': chordShift(kb, ISO_HASH_KEY);         return true; // ~ = Shift + ISO 0x32
    case '\\':tap(kb, ISO_BSLASH_KEY);              return true; // \ on ISO 0x64
    case '|': chordShift(kb, ISO_BSLASH_KEY);       return true; // | = Shift + ISO 0x64
    default: return false;
  }
}

static inline bool sendChar_IE(RawKeyboard& kb, unsigned char c) {
  switch (c) {
    case '@': chordShift(kb, QUOTE_KEY);            return true; // @ = Shift + '
    case '"': chordShift(kb, 0x1F /* '2' */);       return true; // " = Shift + '2'
    case '#': chordShift(kb, 0x20 /* '3' */);       return true; // # = Shift + '3' (Irish)
    case '~': chordShift(kb, ISO_HASH_KEY);         return true; // ~ = Shift + ISO 0x32
    case '\\':tap(kb, ISO_BSLASH_KEY);              return true; // \ on ISO 0x64
    case '|': chordShift(kb, ISO_BSLASH_KEY);       return true; // | = Shift + ISO 0x64
    default: return false;
  }
}

static inline bool sendChar_US(RawKeyboard& kb, unsigned char) {
  (void)kb; return false; // no overrides for US
}

// ============================ £ / € senders ============================
// UK/IE on Win/Linux
inline void sendPound_UK_WinLin(RawKeyboard& kb) { chordShift(kb, 0x20 /* '3' */); }     // £ = Shift+3
inline void sendPound_IE_WinLin(RawKeyboard& kb) { sendAltGrCombo(kb, 0x20 /* '3' */); } // £ = AltGr+3
inline void sendEuro_WinLin   (RawKeyboard& kb) { sendAltGrCombo(kb, 0x21 /* '4' */); }  // € = AltGr+4

// macOS (Option combos)
inline void sendPound_mac     (RawKeyboard& kb) { chordAltLeft(kb, 0x20 /* '3' */); }
inline void sendEuro_mac      (RawKeyboard& kb) { chordAltLeft(kb, 0x1F /* '2' */); }

// US (macOS ok; Windows uses Alt+Numpad via your existing altNumpad if desired)
inline void sendPound_US_Mac  (RawKeyboard& kb) { chordAltLeft(kb, 0x20 /* '3' */); }
inline void sendEuro_US_Mac   (RawKeyboard& kb) { chordAltLeft(kb, 0x1F /* '2' */); }

// ---------- US on Windows/Linux: Alt+Numpad for £/€ ----------
inline void sendPound_US_WinLin(RawKeyboard& kb) { altNumpadRaw(kb, "0163"); } // £
inline void sendEuro_US_WinLin (RawKeyboard& kb) { altNumpadRaw(kb, "0128"); } // €

// Dispatchers per profile
inline void sendPoundProfile(RawKeyboard& kb) {
#if defined(PROFILE_UK_WINLIN)
  sendPound_UK_WinLin(kb);
#elif defined(PROFILE_IE_WINLIN)
  sendPound_IE_WinLin(kb);
#elif defined(PROFILE_UK_MAC) || defined(PROFILE_IE_MAC)
  sendPound_mac(kb);
#elif defined(PROFILE_US_MAC)
  sendPound_US_Mac(kb);
#elif defined(PROFILE_US_WINLIN)
  sendPound_US_WinLin(kb);
#else
  // Sensible default
  sendPound_UK_WinLin(kb);
#endif
}

inline void sendEuroProfile(RawKeyboard& kb) {
#if defined(PROFILE_UK_WINLIN) || defined(PROFILE_IE_WINLIN)
  sendEuro_WinLin(kb);
#elif defined(PROFILE_UK_MAC) || defined(PROFILE_IE_MAC)
  sendEuro_mac(kb);
#elif defined(PROFILE_US_MAC)
  sendEuro_US_Mac(kb);
#elif defined(PROFILE_US_WINLIN)
  sendEuro_US_WinLin(kb);
#else
  // Sensible default
  sendEuro_WinLin(kb);
#endif
}

// ========================= Unicode-aware sender =========================
// Intercepts UTF-8 for '£' (C2 A3) and '€' (E2 82 AC).
// Applies layout-specific ASCII overrides where needed.
// Everything else uses the library's ASCII path (Keyboard.write).

static inline void sendUnicodeAware(RawKeyboard& kb, const char* s) {
  while (*s) {
    unsigned char c = (unsigned char)*s++;

    // '£'
    if (c == 0xC2 && (unsigned char)*s == 0xA3) { ++s; sendPoundProfile(kb); continue; }
    // '€'
    if (c == 0xE2 && (unsigned char)*s == 0x82 && (unsigned char)*(s+1) == 0xAC) { s += 2; sendEuroProfile(kb); continue; }

    // Basic controls through library path
    if (c == '\t' || c == '\n' || c == '\r') { kb.write(c); continue; }

    // Layout-specific ASCII overrides
#if   defined(PROFILE_UK_WINLIN)  || defined(PROFILE_UK_MAC)
    if (sendChar_UK(kb, c)) continue;
#elif defined(PROFILE_IE_WINLIN)  || defined(PROFILE_IE_MAC)
    if (sendChar_IE(kb, c)) continue;
#elif defined(PROFILE_US_WINLIN)  || defined(PROFILE_US_MAC)
    if (sendChar_US(kb, c)) continue; // (always false)
#endif

    // Default ASCII
    kb.write(c);
  }
}
