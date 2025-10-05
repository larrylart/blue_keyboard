////////////////////////////////////////////////////////////////////
// handling various keyboard layout for specia chars/currency symbols
////////////////////////////////////////////////////////////////////
#ifndef LAYOUT_KB_PROFILES_H
#define LAYOUT_KB_PROFILES_H

#include "RawKeyboard.h"

// layout_kb_profiles.h
enum class KeyboardLayout : uint8_t 
{
    UK_WINLIN = 1,
    IE_WINLIN,
    US_WINLIN,
    UK_MAC,
    IE_MAC,
    US_MAC,
	// extra
    DE_WINLIN,
    DE_MAC,
    FR_WINLIN,
    FR_MAC,
    ES_WINLIN,
    ES_MAC,
    IT_WINLIN,
    IT_MAC,
    PT_PT_WINLIN,
    PT_PT_MAC,
    PT_BR_WINLIN,
    PT_BR_MAC,
    SE_WINLIN,
    NO_WINLIN,
    DK_WINLIN,
    FI_WINLIN,
    CH_DE_WINLIN,
    CH_FR_WINLIN,
    TR_WINLIN,
    TR_MAC	
};

// Global variable defined in blue_keyboard.ino
extern KeyboardLayout m_nKeyboardLayout;

// Map enum -> string (used in responses)
inline const char* layoutName(KeyboardLayout id) 
{
    switch( id ) 
	{
        case KeyboardLayout::UK_WINLIN: return "LAYOUT_UK_WINLIN";
        case KeyboardLayout::IE_WINLIN: return "LAYOUT_IE_WINLIN";
        case KeyboardLayout::US_WINLIN: return "LAYOUT_US_WINLIN";
        case KeyboardLayout::UK_MAC:    return "LAYOUT_UK_MAC";
        case KeyboardLayout::IE_MAC:    return "LAYOUT_IE_MAC";
        case KeyboardLayout::US_MAC:    return "LAYOUT_US_MAC";
		// extra
        case KeyboardLayout::DE_WINLIN:   return "LAYOUT_DE_WINLIN";
        case KeyboardLayout::DE_MAC:      return "LAYOUT_DE_MAC";
        case KeyboardLayout::FR_WINLIN:   return "LAYOUT_FR_WINLIN";
        case KeyboardLayout::FR_MAC:      return "LAYOUT_FR_MAC";
        case KeyboardLayout::ES_WINLIN:   return "LAYOUT_ES_WINLIN";
        case KeyboardLayout::ES_MAC:      return "LAYOUT_ES_MAC";
        case KeyboardLayout::IT_WINLIN:   return "LAYOUT_IT_WINLIN";
        case KeyboardLayout::IT_MAC:      return "LAYOUT_IT_MAC";
        case KeyboardLayout::PT_PT_WINLIN:return "LAYOUT_PT_PT_WINLIN";
        case KeyboardLayout::PT_PT_MAC:   return "LAYOUT_PT_PT_MAC";
        case KeyboardLayout::PT_BR_WINLIN:return "LAYOUT_PT_BR_WINLIN";
        case KeyboardLayout::PT_BR_MAC:   return "LAYOUT_PT_BR_MAC";
        case KeyboardLayout::SE_WINLIN:   return "LAYOUT_SE_WINLIN";
        case KeyboardLayout::NO_WINLIN:   return "LAYOUT_NO_WINLIN";
        case KeyboardLayout::DK_WINLIN:   return "LAYOUT_DK_WINLIN";
        case KeyboardLayout::FI_WINLIN:   return "LAYOUT_FI_WINLIN";
        case KeyboardLayout::CH_DE_WINLIN:return "LAYOUT_CH_DE_WINLIN";
        case KeyboardLayout::CH_FR_WINLIN:return "LAYOUT_CH_FR_WINLIN";
        case KeyboardLayout::TR_WINLIN:   return "LAYOUT_TR_WINLIN";
        case KeyboardLayout::TR_MAC:      return "LAYOUT_TR_MAC";
		
        default:                        return "LAYOUT_US_WINLIN";
    }
}

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
inline void sendAltGrCombo(RawKeyboard& kb, uint8_t baseUsage) 
{
#ifdef ALTGR_IMPL_CTRLALT
  chordCtrlAltLeft(kb, baseUsage);
#else
  chordAltGr(kb, baseUsage);
#endif
}

// ---------- Alt+Numpad using RAW reports (Windows US path) ----------
inline void altNumpadRaw( RawKeyboard& kb, const char* digits ) 
{
  // Hold Left-Alt (modifier bit 0x04) and tap keypad digits as raw usages
  // Press Alt only once, then send each digit in the same report sequence.
  // (Windows accepts either style - is this approach reliable?)
  KeyReport rpt = {};
  rpt.modifiers = 0x04; // LAlt
  kb.sendReport(&rpt);  // press Alt
  delay(2);

  for( const char* p = digits; *p; ++p ) 
  {
    uint8_t u = 0;
    switch (*p) 
	{
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
static inline bool sendChar_UK( RawKeyboard& kb, unsigned char c ) 
{
  switch( c ) 
  {
    case '@': chordShift(kb, QUOTE_KEY);            return true; // @ = Shift + '
    case '"': chordShift(kb, 0x1F /* '2' */);       return true; // " = Shift + '2'
    case '#': tap(kb, ISO_HASH_KEY);                return true; // # on ISO 0x32
    case '~': chordShift(kb, ISO_HASH_KEY);         return true; // ~ = Shift + ISO 0x32
    case '\\':tap(kb, ISO_BSLASH_KEY);              return true; // \ on ISO 0x64
    case '|': chordShift(kb, ISO_BSLASH_KEY);       return true; // | = Shift + ISO 0x64
    default: return false;
  }
}

static inline bool sendChar_IE( RawKeyboard& kb, unsigned char c ) 
{
  switch( c ) 
  {
    case '@': chordShift(kb, QUOTE_KEY);            return true; // @ = Shift + '
    case '"': chordShift(kb, 0x1F /* '2' */);       return true; // " = Shift + '2'
    case '#': chordShift(kb, 0x20 /* '3' */);       return true; // # = Shift + '3' (Irish)
    case '~': chordShift(kb, ISO_HASH_KEY);         return true; // ~ = Shift + ISO 0x32
    case '\\':tap(kb, ISO_BSLASH_KEY);              return true; // \ on ISO 0x64
    case '|': chordShift(kb, ISO_BSLASH_KEY);       return true; // | = Shift + ISO 0x64
    default: return false;
  }
}

static inline bool sendChar_US( RawKeyboard& kb, unsigned char) 
{
  (void)kb; return false; // no overrides for US
}

///////////////////////////
// :: EXTRA CHAR
// ======================= EXTRA LAYOUT ASCII OVERRIDES =======================
// Numeric usage IDs (same style as your file):
// '1'..'0' = 0x1E..0x27, '-'=0x2D, '='=0x2E, '['=0x2F, ']'=0x30, '\'=0x31,
// QUOTE_KEY already defined (0x34), ','=0x36, '.'=0x37, '/'=0x38

// ---------- German (DE QWERTZ) ----------
static inline bool sendChar_DE(RawKeyboard& kb, unsigned char c)
{
  switch( c )
  {
    // Letter swaps
    case 'y': tap(kb, 0x1D /* 'Z' */); return true;
    case 'Y': chordShift(kb, 0x1D);    return true;
    case 'z': tap(kb, 0x1C /* 'Y' */); return true;
    case 'Z': chordShift(kb, 0x1C);    return true;

    // High-value symbols
    case '@':   sendAltGrCombo(kb, 0x14 /* 'Q' */); return true;  // AltGr+Q
    case '\\':  sendAltGrCombo(kb, 0x2D /* '-' */); return true;  // AltGr+-
    case '|':   sendAltGrCombo(kb, ISO_BSLASH_KEY);  return true; // AltGr+ISO (common)
    case '/':   chordShift(kb, 0x24 /* '7' */);      return true; // Shift+7
    case '?':   chordShift(kb, 0x2D /* '-' */);      return true; // Shift+-
    default: return false;
  }
}

// macOS often uses Option combos; treat as same overrides (works well enough for ASCII)
static inline bool sendChar_DE_mac(RawKeyboard& kb, unsigned char c) { return sendChar_DE(kb, c); }

// ---------- French (FR AZERTY) ----------
static inline bool sendChar_FR(RawKeyboard& kb, unsigned char c)
{
  switch( c )
  {
    // Letter swaps A<->Q, Z<->W, M on ';'
    case 'a': tap(kb, 0x14 /* 'Q' */); return true;
    case 'A': chordShift(kb, 0x14);    return true;
    case 'q': tap(kb, 0x04 /* 'A' */); return true;
    case 'Q': chordShift(kb, 0x04);    return true;

    case 'z': tap(kb, 0x1A /* 'W' */); return true;
    case 'Z': chordShift(kb, 0x1A);    return true;
    case 'w': tap(kb, 0x1D /* 'Z' */); return true;
    case 'W': chordShift(kb, 0x1D);    return true;

    case 'm': tap(kb, 0x33 /* ';' */); return true;
    case 'M': chordShift(kb, 0x33);    return true;

    // Digits require Shift on classic AZERTY
    case '1': chordShift(kb, 0x1E); return true;
    case '2': chordShift(kb, 0x1F); return true;
    case '3': chordShift(kb, 0x20); return true;
    case '4': chordShift(kb, 0x21); return true;
    case '5': chordShift(kb, 0x22); return true;
    case '6': chordShift(kb, 0x23); return true;
    case '7': chordShift(kb, 0x24); return true;
    case '8': chordShift(kb, 0x25); return true;
    case '9': chordShift(kb, 0x26); return true;
    case '0': chordShift(kb, 0x27); return true;

    // Common ASCII symbols
    case '@':   sendAltGrCombo(kb, 0x27 /* '0' */);  return true;
    case '\\':  sendAltGrCombo(kb, 0x25 /* '8' */);  return true;
    case '|':   sendAltGrCombo(kb, 0x23 /* '6' */);  return true;
    case '{':   sendAltGrCombo(kb, 0x21 /* '4' */);  return true;
    case '}':   sendAltGrCombo(kb, 0x30 /* ']' */);  return true;
    case '[':   sendAltGrCombo(kb, 0x22 /* '5' */);  return true;
    case ']':   sendAltGrCombo(kb, 0x2E /* '=' */);  return true;
    default: return false;
  }
}
static inline bool sendChar_FR_mac(RawKeyboard& kb, unsigned char c) { return sendChar_FR(kb, c); }

// ---------- Spanish (ES) ----------
static inline bool sendChar_ES(RawKeyboard& kb, unsigned char c)
{
  if( c == '@' ) { sendAltGrCombo(kb, 0x1F /* '2' */); return true; } // AltGr+2
  switch( c )
  {
    case '\\': sendAltGrCombo(kb, 0x24 /* '7' */); return true;
    case '|':  sendAltGrCombo(kb, 0x1E /* '1' */); return true;
    default: return false;
  }
}
static inline bool sendChar_ES_mac(RawKeyboard& kb, unsigned char c) { return sendChar_ES(kb, c); }

// ---------- Italian (IT) ----------
static inline bool sendChar_IT(RawKeyboard& kb, unsigned char c)
{
  if (c == '@') { sendAltGrCombo(kb, 0x1F /* '2' */); return true; } // AltGr+2
  switch (c)
  {
    case '\\': sendAltGrCombo(kb, 0x2E /* '=' */); return true;
    case '|':  sendAltGrCombo(kb, 0x1E /* '1' */); return true;
    default: return false;
  }
}
static inline bool sendChar_IT_mac(RawKeyboard& kb, unsigned char c) { return sendChar_IT(kb, c); }

// ---------- Portuguese (Portugal) ----------
static inline bool sendChar_PT_PT(RawKeyboard& kb, unsigned char c)
{
  if (c == '@') { sendAltGrCombo(kb, 0x1F /* '2' */); return true; }
  switch (c)
  {
    case '\\': sendAltGrCombo(kb, 0x2D /* '-' */); return true;
    case '|':  sendAltGrCombo(kb, 0x24 /* '7' */); return true;
    default: return false;
  }
}
static inline bool sendChar_PT_PT_mac(RawKeyboard& kb, unsigned char c) { return sendChar_PT_PT(kb, c); }

// ---------- Portuguese (Brazil, ABNT2) ----------
static inline bool sendChar_PT_BR(RawKeyboard& kb, unsigned char c)
{
  if (c == '@') { sendAltGrCombo(kb, 0x1F /* '2' */); return true; }
  switch (c)
  {
    case '\\': sendAltGrCombo(kb, 0x2D /* '-' */); return true;
    case '|':  sendAltGrCombo(kb, 0x24 /* '7' */); return true;
    default: return false;
  }
}
static inline bool sendChar_PT_BR_mac(RawKeyboard& kb, unsigned char c) { return sendChar_PT_BR(kb, c); }

// ---------- Nordics (SE/NO/DK/FI share similar ASCII moves) ----------
static inline bool sendChar_Nordic(RawKeyboard& kb, unsigned char c)
{
  if (c == '@') { sendAltGrCombo(kb, 0x1F /* '2' */); return true; } // AltGr+2
  switch (c)
  {
    case '\\': sendAltGrCombo(kb, 0x2E /* '=' */);   return true; // AltGr+=
    case '|':  sendAltGrCombo(kb, ISO_BSLASH_KEY);   return true; // AltGr+ISO key
    case '{':  sendAltGrCombo(kb, 0x24 /* '7' */);   return true;
    case '}':  sendAltGrCombo(kb, 0x27 /* '0' */);   return true;
    case '[':  sendAltGrCombo(kb, 0x25 /* '8' */);   return true;
    case ']':  sendAltGrCombo(kb, 0x26 /* '9' */);   return true;
    default: return false;
  }
}

// ---------- Switzerland (CH QWERTZ) ----------
static inline bool sendChar_CH(RawKeyboard& kb, unsigned char c)
{
  if (c == '@') { sendAltGrCombo(kb, 0x1F /* '2' */); return true; } // AltGr+2
  switch (c)
  {
    case '\\': sendAltGrCombo(kb, 0x2D /* '-' */);   return true;
    case '|':  sendAltGrCombo(kb, ISO_BSLASH_KEY);   return true;
    default: return false;
  }
}

// ---------- Turkish Q ----------
static inline bool sendChar_TR(RawKeyboard& kb, unsigned char c)
{
  if (c == '@') { sendAltGrCombo(kb, 0x14 /* 'Q' */); return true; } // AltGr+Q
  switch (c)
  {
    case '\\': sendAltGrCombo(kb, 0x2D /* '-' */);   return true;
    case '|':  sendAltGrCombo(kb, ISO_BSLASH_KEY);   return true;
    default: return false;
  }
}
static inline bool sendChar_TR_mac(RawKeyboard& kb, unsigned char c) { return sendChar_TR(kb, c); }


// END OF EXTRA
/////////////////////////////////

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

// Dispatchers currency symbols per profile - todo: do I need to adjust this for various layouts?
inline void sendPoundProfile(RawKeyboard& kb) 
{
  switch( m_nKeyboardLayout ) 
  {
    case KeyboardLayout::UK_WINLIN:  sendPound_UK_WinLin(kb);  break;
    case KeyboardLayout::IE_WINLIN:  sendPound_IE_WinLin(kb);  break;
    case KeyboardLayout::UK_MAC:
    case KeyboardLayout::IE_MAC:     sendPound_mac(kb);        break;
    case KeyboardLayout::US_MAC:     sendPound_US_Mac(kb);     break;
    case KeyboardLayout::US_WINLIN:  sendPound_US_WinLin(kb);  break;
    default:                         sendPound_UK_WinLin(kb);  break;
  }
}

inline void sendEuroProfile(RawKeyboard& kb) 
{
  switch( m_nKeyboardLayout ) 
  {
    case KeyboardLayout::UK_WINLIN:
    case KeyboardLayout::IE_WINLIN:  sendEuro_WinLin(kb);      break;
    case KeyboardLayout::UK_MAC:
    case KeyboardLayout::IE_MAC:     sendEuro_mac(kb);         break;
    case KeyboardLayout::US_MAC:     sendEuro_US_Mac(kb);      break;
    case KeyboardLayout::US_WINLIN:  sendEuro_US_WinLin(kb);   break;
    default:                         sendEuro_WinLin(kb);      break;
  }
}

// ========================= Unicode-aware sender =========================
// Intercepts UTF-8 for '£' (C2 A3) and '€' (E2 82 AC).
// Applies layout-specific ASCII overrides where needed.
// Everything else uses the library's ASCII path (Keyboard.write).
static inline void sendUnicodeAware( RawKeyboard& kb, const char* s ) 
{
  while (*s) 
  {
    unsigned char c = (unsigned char)*s++;

    // UTF-8 '£' = C2 A3
    if( c == 0xC2 && (unsigned char)*s == 0xA3 ) 
	{
      ++s;
      sendPoundProfile(kb);
      continue;
    }

    // UTF-8 '€' = E2 82 AC
    if( c == 0xE2 && (unsigned char)*s == 0x82 && (unsigned char)*(s+1) == 0xAC ) 
	{
      s += 2;
      sendEuroProfile(kb);
      continue;
    }

    // Basic controls → library path
    if( c == '\t' || c == '\n' || c == '\r' ) 
	{
      kb.write(c);
      continue;
    }

    // Runtime ASCII overrides by layout family
    switch( m_nKeyboardLayout ) 
	{
      case KeyboardLayout::UK_WINLIN:
      case KeyboardLayout::UK_MAC:
        if( sendChar_UK(kb, c) ) continue;
        break;

      case KeyboardLayout::IE_WINLIN:
      case KeyboardLayout::IE_MAC:
        if( sendChar_IE(kb, c) ) continue;
        break;

      case KeyboardLayout::US_WINLIN:
      case KeyboardLayout::US_MAC:
        if( sendChar_US(kb, c) ) continue; // currently always false
        break;
		
		///////////////////////////
		// extra
	case KeyboardLayout::DE_WINLIN:
		if( sendChar_DE(kb, c) ) continue; break;
	case KeyboardLayout::DE_MAC:
		if( sendChar_DE_mac(kb, c) ) continue; break;

	case KeyboardLayout::FR_WINLIN:
		if( sendChar_FR(kb, c) ) continue; break;
	case KeyboardLayout::FR_MAC:
		if( sendChar_FR_mac(kb, c) ) continue; break;

	case KeyboardLayout::ES_WINLIN:
		if( sendChar_ES(kb, c) ) continue; break;
	case KeyboardLayout::ES_MAC:
		if( sendChar_ES_mac(kb, c) ) continue; break;

	case KeyboardLayout::IT_WINLIN:
		if( sendChar_IT(kb, c) ) continue; break;
	case KeyboardLayout::IT_MAC:
		if( sendChar_IT_mac(kb, c) ) continue; break;

	case KeyboardLayout::PT_PT_WINLIN:
		if( sendChar_PT_PT(kb, c) ) continue; break;
	case KeyboardLayout::PT_PT_MAC:
		if( sendChar_PT_PT_mac(kb, c) ) continue; break;

	case KeyboardLayout::PT_BR_WINLIN:
		if( sendChar_PT_BR(kb, c) ) continue; break;
	case KeyboardLayout::PT_BR_MAC:
		if( sendChar_PT_BR_mac(kb, c) ) continue; break;

	case KeyboardLayout::SE_WINLIN:
	case KeyboardLayout::NO_WINLIN:
	case KeyboardLayout::DK_WINLIN:
	case KeyboardLayout::FI_WINLIN:
		if( sendChar_Nordic(kb, c) ) continue; break;

	case KeyboardLayout::CH_DE_WINLIN:
	case KeyboardLayout::CH_FR_WINLIN:
		if( sendChar_CH(kb, c) ) continue; break;

	case KeyboardLayout::TR_WINLIN:
		if( sendChar_TR(kb, c) ) continue; break;
	case KeyboardLayout::TR_MAC:
		if( sendChar_TR_mac(kb, c) ) continue; break;
		
    }

    // Default ASCII
    kb.write(c);
  }
}

#endif
