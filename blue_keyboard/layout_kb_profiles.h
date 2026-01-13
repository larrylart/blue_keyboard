////////////////////////////////////////////////////////////////////
// handling various keyboard layouts and special chars/currency symbols
////////////////////////////////////////////////////////////////////
#ifndef LAYOUT_KB_PROFILES_H
#define LAYOUT_KB_PROFILES_H

#include "RawKeyboard.h"

// New mapping-based layout tables
#include "kb_layouts/kb_layout_common.h"

// Include layout mapping headers 
#include "kb_layouts/kb_layout_US_WINLIN.h"
#include "kb_layouts/kb_layout_US_MAC.h"
#include "kb_layouts/kb_layout_UK_WINLIN.h"
#include "kb_layouts/kb_layout_UK_MAC.h"
#include "kb_layouts/kb_layout_IE_WINLIN.h"
#include "kb_layouts/kb_layout_IE_MAC.h"
#include "kb_layouts/kb_layout_DE_WINLIN.h"
#include "kb_layouts/kb_layout_DE_MAC.h"
#include "kb_layouts/kb_layout_FR_WINLIN.h"
#include "kb_layouts/kb_layout_FR_MAC.h"
#include "kb_layouts/kb_layout_ES_WINLIN.h"
#include "kb_layouts/kb_layout_ES_MAC.h"
#include "kb_layouts/kb_layout_IT_WINLIN.h"
#include "kb_layouts/kb_layout_IT_MAC.h"
#include "kb_layouts/kb_layout_PT_PT_WINLIN.h"
#include "kb_layouts/kb_layout_PT_PT_MAC.h"
#include "kb_layouts/kb_layout_PT_BR_WINLIN.h"
#include "kb_layouts/kb_layout_PT_BR_MAC.h"
#include "kb_layouts/kb_layout_SE_WINLIN.h"
#include "kb_layouts/kb_layout_NO_WINLIN.h"
#include "kb_layouts/kb_layout_DK_WINLIN.h"
#include "kb_layouts/kb_layout_FI_WINLIN.h"
#include "kb_layouts/kb_layout_CH_DE_WINLIN.h"
#include "kb_layouts/kb_layout_CH_FR_WINLIN.h"
#include "kb_layouts/kb_layout_TR_WINLIN.h"
#include "kb_layouts/kb_layout_TR_MAC.h"

// tv layouts
#include "kb_layouts/kb_layout_TV_SAMSUNG.h"
#include "kb_layouts/kb_layout_TV_LG.h"
#include "kb_layouts/kb_layout_TV_ANDROID.h"
#include "kb_layouts/kb_layout_TV_ROKU.h"
#include "kb_layouts/kb_layout_TV_FIRETV.h"

// base indexes
//#define BK_MAC_LAYOUT_BASE 100
//#define BK_TV_LAYOUT_BASE 200

// layout_kb_profiles.h
enum class KeyboardLayout : uint8_t 
{
	// windows and linux - might need a split?
    US_WINLIN = 1,
    UK_WINLIN,
    IE_WINLIN,
    DE_WINLIN,
    FR_WINLIN,
    ES_WINLIN,
    IT_WINLIN,
    PT_PT_WINLIN,
    PT_BR_WINLIN,
    SE_WINLIN,
    NO_WINLIN,
    DK_WINLIN,
    FI_WINLIN,
    CH_DE_WINLIN,
    CH_FR_WINLIN,
    TR_WINLIN,

    // sentinel — MAC PROFILES
    _START_MACS,	
	
	// MACs
    US_MAC,
    UK_MAC,
    DE_MAC,
    IE_MAC,
    FR_MAC,
    ES_MAC,
    IT_MAC,
    PT_PT_MAC,
    PT_BR_MAC,
    TR_MAC,

    // sentinel — TV PROFILES
    _START_TV,	
	
    // TV profiles (start at 200)
    TV_SAMSUNG,
    TV_LG,
    TV_ANDROID,
    TV_ROKU,
    TV_FIRETV,

    // sentinel — END OF RECORDS
    _END_INDEX	
};

// Global variable defined in blue_keyboard.ino
extern KeyboardLayout m_nKeyboardLayout;

// Map enum -> string (used in responses)
inline const char* layoutName(KeyboardLayout id) 
{
    switch( id ) 
	{
        case KeyboardLayout::US_WINLIN: return "LAYOUT_US_WINLIN";
        case KeyboardLayout::US_MAC:    return "LAYOUT_US_MAC";
        case KeyboardLayout::UK_WINLIN: return "LAYOUT_UK_WINLIN";
        case KeyboardLayout::UK_MAC:    return "LAYOUT_UK_MAC";
        case KeyboardLayout::IE_WINLIN: return "LAYOUT_IE_WINLIN";
        case KeyboardLayout::IE_MAC:    return "LAYOUT_IE_MAC";
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

		// tvs
        case KeyboardLayout::TV_SAMSUNG: return "LAYOUT_TV_SAMSUNG";
        case KeyboardLayout::TV_LG:      return "LAYOUT_TV_LG";
        case KeyboardLayout::TV_ANDROID: return "LAYOUT_TV_ANDROID";
        case KeyboardLayout::TV_ROKU:    return "LAYOUT_TV_ROKU";
        case KeyboardLayout::TV_FIRETV:  return "LAYOUT_TV_FIRETV";
		
        default:                        return "LAYOUT_US_WINLIN";
    }
}

////////////////////////////////////////////////////////////////////
// :: Layout map selection 
////////////////////////////////////////////////////////////////////
static inline const KbMapEntry* getLayoutMap(KeyboardLayout lay, uint16_t& count)
{
  switch (lay)
  {
    case KeyboardLayout::US_WINLIN: return kb_get_US_WINLIN_map(count);
    case KeyboardLayout::US_MAC: return kb_get_US_MAC_map(count);

    case KeyboardLayout::UK_WINLIN: return kb_get_UK_WINLIN_map(count);
    case KeyboardLayout::UK_MAC: return kb_get_UK_MAC_map(count);

    case KeyboardLayout::IE_WINLIN: return kb_get_IE_WINLIN_map(count);
    case KeyboardLayout::IE_MAC: return kb_get_IE_MAC_map(count);

    case KeyboardLayout::DE_WINLIN: return kb_get_DE_WINLIN_map(count);
    case KeyboardLayout::DE_MAC: return kb_get_DE_MAC_map(count);

    case KeyboardLayout::FR_WINLIN: return kb_get_FR_WINLIN_map(count);
    case KeyboardLayout::FR_MAC: return kb_get_FR_MAC_map(count);

    case KeyboardLayout::ES_WINLIN: return kb_get_ES_WINLIN_map(count);
    case KeyboardLayout::ES_MAC: return kb_get_ES_MAC_map(count);
	  
    case KeyboardLayout::IT_WINLIN: return kb_get_IT_WINLIN_map(count);
    case KeyboardLayout::IT_MAC:    return kb_get_IT_MAC_map(count);

    case KeyboardLayout::PT_PT_WINLIN: return kb_get_PT_PT_WINLIN_map(count);
    case KeyboardLayout::PT_PT_MAC:    return kb_get_PT_PT_MAC_map(count);

    case KeyboardLayout::PT_BR_WINLIN: return kb_get_PT_BR_WINLIN_map(count);
    case KeyboardLayout::PT_BR_MAC:    return kb_get_PT_BR_MAC_map(count);

    case KeyboardLayout::SE_WINLIN: return kb_get_SE_WINLIN_map(count);

    case KeyboardLayout::NO_WINLIN: return kb_get_NO_WINLIN_map(count);

    case KeyboardLayout::DK_WINLIN: return kb_get_DK_WINLIN_map(count);

    case KeyboardLayout::FI_WINLIN: return kb_get_FI_WINLIN_map(count);

    case KeyboardLayout::CH_DE_WINLIN: return kb_get_CH_DE_WINLIN_map(count);
    
    case KeyboardLayout::CH_FR_WINLIN: return kb_get_CH_FR_WINLIN_map(count);

    case KeyboardLayout::TR_WINLIN: return kb_get_TR_WINLIN_map(count);
    case KeyboardLayout::TR_MAC:    return kb_get_TR_MAC_map(count);

	// tv
    case KeyboardLayout::TV_SAMSUNG: return kb_get_TV_SAMSUNG_map(count);
    case KeyboardLayout::TV_LG:      return kb_get_TV_LG_map(count);
    case KeyboardLayout::TV_ANDROID: return kb_get_TV_ANDROID_map(count);
    case KeyboardLayout::TV_ROKU:    return kb_get_TV_ROKU_map(count);
    case KeyboardLayout::TV_FIRETV:  return kb_get_TV_FIRETV_map(count);


    default:
      count = 0;
      return nullptr;
  }
}

// ---------------------------------------------------------------------------
// TV layout helpers (layouts >= 200)
// ---------------------------------------------------------------------------

static inline bool isTvLayout(KeyboardLayout lay)
{
    return( ((uint8_t)lay) > (uint8_t) KeyboardLayout::_START_TV );
}

// If TV layout is active, remap standard consumer usages to brand-specific ones
static inline TvMediaRemap remapConsumerForTv(KeyboardLayout lay, uint8_t u)
{
    switch(lay)
    {
        case KeyboardLayout::TV_SAMSUNG: return tv_samsung_remap_consumer(u);
        case KeyboardLayout::TV_LG:      return tv_lg_remap_consumer(u);
        case KeyboardLayout::TV_ANDROID: return tv_android_remap_consumer(u);
        case KeyboardLayout::TV_ROKU:    return tv_roku_remap_consumer(u);
        case KeyboardLayout::TV_FIRETV:  return tv_firetv_remap_consumer(u);

        default:
            return { false, u }; // default passthrough
    }
}


// On Win/Linux, some systems implement AltGr strictly as Right-Alt, others as Ctrl+Alt.
// Enable this if AltGr requires Ctrl+Alt on target:
// #define ALTGR_IMPL_CTRLALT 1

// *** Unicode fallback mode ***
// Default: Windows Alt+Numpad decimal (best-effort, not guaranteed full Unicode)
// Optional: Linux Ctrl+Shift+U hex Enter (true Unicode on many Linux desktops)
//
// Set to 1 to compile Linux method instead of Windows method.
#ifndef BK_UNICODE_FALLBACK_LINUX
  #define BK_UNICODE_FALLBACK_LINUX 0
#endif

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

////////////////////////////////////////////////////////////////////
// UTF-8 -> codepoint 
////////////////////////////////////////////////////////////////////
static inline bool utf8NextCodepoint(const char*& s, uint32_t& outCp)
{
  const uint8_t c0 = (uint8_t)*s;
  if (!c0) return false;

  if (c0 < 0x80) { outCp = c0; ++s; return true; }

  if ((c0 & 0xE0) == 0xC0)
  {
    const uint8_t c1 = (uint8_t)s[1];
    if ((c1 & 0xC0) != 0x80) { ++s; outCp = 0xFFFD; return true; }
    outCp = ((uint32_t)(c0 & 0x1F) << 6) | (uint32_t)(c1 & 0x3F);
    s += 2;
    return true;
  }

  if ((c0 & 0xF0) == 0xE0)
  {
    const uint8_t c1 = (uint8_t)s[1];
    const uint8_t c2 = (uint8_t)s[2];
    if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80)) { ++s; outCp = 0xFFFD; return true; }
    outCp = ((uint32_t)(c0 & 0x0F) << 12) | ((uint32_t)(c1 & 0x3F) << 6) | (uint32_t)(c2 & 0x3F);
    s += 3;
    return true;
  }

  if ((c0 & 0xF8) == 0xF0)
  {
    const uint8_t c1 = (uint8_t)s[1];
    const uint8_t c2 = (uint8_t)s[2];
    const uint8_t c3 = (uint8_t)s[3];
    if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80) || ((c3 & 0xC0) != 0x80))
      { ++s; outCp = 0xFFFD; return true; }
    outCp = ((uint32_t)(c0 & 0x07) << 18) | ((uint32_t)(c1 & 0x3F) << 12) |
            ((uint32_t)(c2 & 0x3F) << 6)  |  (uint32_t)(c3 & 0x3F);
    s += 4;
    return true;
  }

  ++s;
  outCp = 0xFFFD;
  return true;
}

////////////////////////////////////////////////////////////////////
static inline bool isMacLayout(KeyboardLayout lay)
{
    const uint8_t v = (uint8_t)lay;
    return( (v > (uint8_t) KeyboardLayout::_START_MACS) && (v < (uint8_t) KeyboardLayout::_START_TV) );
}

////////////////////////////////////////////////////////////////////
// ========================= Send one codepoint using current layout map =========================
// - If codepoint exists in map: emit (mods1,key1) then optional (mods2,key2)
// - Else if ASCII (<128): fallback kb.write()
// - Else: drop silently (Option A)
////////////////////////////////////////////////////////////////////
static inline bool sendCodepointMapped(RawKeyboard& kb, uint32_t cp)
{
	uint16_t n = 0;
	const KbMapEntry* map = getLayoutMap(m_nKeyboardLayout, n);

	// 1) Try explicit layout map first (special chars)
	if( map )
	{
		for( uint16_t i = 0; i < n; ++i )
		{
			if( map[i].cp == cp )
			{
				sendChordOrTap(kb, map[i].mods1, map[i].key1);
				sendChordOrTap(kb, map[i].mods2, map[i].key2);
				return( true );
			}
		}
	}

	// 2) ASCII fallback (existing behavior)
	if( cp < 128 )
	{
		kb.write( (char)cp );
		return( true );
	}

	// 3) Unicode fallback (OS-specific input sequence)
	// Note: HID cannot "send unicode" - can only type OS input sequences.

	// Reject invalid Unicode scalars
	if( cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu) ) return( false );

	///////////////////////////////
    // :: If MAC layout active: use macOS Unicode Hex Input (Option+hex)
    // Requires "Unicode Hex Input" enabled on the host.
    if( isMacLayout(m_nKeyboardLayout) )
    {
        // macOS expects UTF-16 code units. For emoji (cp > 0xFFFF),
        // send surrogate pair as two 4-hex sequences.
        auto sendMacHex4 = [&](uint16_t u16) -> void {
            const uint8_t MOD_LALT = 0x04; // Option (Left Alt)

            auto hexNibbleToUsage = [](uint8_t nib) -> uint8_t {
                // 0..9 -> top-row digits (layout-dependent but generally OK on US/UK)
                // a..f -> letter usages a..f (layout-dependent)
                if( nib < 10 )
                {
                    // '1'..'9' => 0x1E..0x26, '0' => 0x27
                    if( nib == 0 ) return 0x27;
                    return (uint8_t)(0x1E + (nib - 1));
                }
                return (uint8_t)(0x04 + (nib - 10)); // a..f
            };

            // press+hold Option
            KeyReport held = {};
            held.modifiers = MOD_LALT;
            kb.sendReport(&held);
            delay(2);

            for( int shift = 12; shift >= 0; shift -= 4 )
            {
                const uint8_t nib = (uint8_t)((u16 >> shift) & 0xF);
                const uint8_t usage = hexNibbleToUsage(nib);
                if( !usage ) break;

                KeyReport down = held;
                down.keys[0] = usage;
                kb.sendReport(&down);
                delay(2);

                // release key, keep Option held
                kb.sendReport(&held);
                delay(2);
            }

            // release Option
            KeyReport up = {};
            kb.sendReport(&up);
            delay(2);
        };

        if( cp > 0xFFFFu )
        {
            const uint32_t v = cp - 0x10000u;
            const uint16_t hi = (uint16_t)(0xD800u + (v >> 10));
            const uint16_t lo = (uint16_t)(0xDC00u + (v & 0x3FFu));
            sendMacHex4(hi);
            sendMacHex4(lo);
        }
        else
        {
            sendMacHex4((uint16_t)cp);
        }

        return( true );
    }
	// end MAC special chars
	//////////////////////////////



#if BK_UNICODE_FALLBACK_LINUX

	// Linux: Ctrl+Shift+U, hex digits, Enter
	// Works on many Linux desktop environments / IMEs, but not all apps.
	// Uses raw usages for digits/letters (layout-dependent physical keys).

	auto digitUsage = [](char d) -> uint8_t {
		// '1'..'9' => 0x1E..0x26, '0' => 0x27
		if( d >= '1' && d <= '9' ) return( (uint8_t)(0x1E + (d - '1')) );
		if( d == '0' ) return( 0x27 );
		return( 0 );
	};

	auto hexLetterUsage = [](char c) -> uint8_t {
		// a..f => usages for letters: a=0x04, b=0x05, ...
		if( c >= 'a' && c <= 'f' ) return (uint8_t)(0x04 + (c - 'a'));
		return 0;
	};

	auto sendHexDigits = [&](uint32_t value) -> bool {
		// minimal hex (no leading zeros), lowercase
		char buf[9] = {0}; // up to 8 hex digits + NUL
		int idx = 0;

		do {
			uint8_t v = (uint8_t)(value & 0xFu);
			buf[idx++] = (v < 10) ? (char)('0' + v) : (char)('a' + (v - 10));
			value >>= 4;
			
		} while( value && idx < 8 );

		for( int i = idx - 1; i >= 0; --i )
		{
			const char c = buf[i];
			uint8_t u = 0;
			if( c >= '0' && c <= '9' ) 
				u = digitUsage(c);
			else
				u = hexLetterUsage(c);

			if (!u) return false;
			kb.sendRaw(0x00, u);
		}
		return( true );
	};

	const uint8_t MOD_LCTRL_LSHIFT = 0x01 | 0x02;
	const uint8_t USAGE_U          = 0x18;
	const uint8_t USAGE_ENTER      = 0x28;

	kb.sendRaw( MOD_LCTRL_LSHIFT, USAGE_U );

	if( !sendHexDigits(cp) ) return false;

	kb.sendRaw(0x00, USAGE_ENTER);
	return( true );

#else

	// Windows: Alt+Numpad decimal digits 
	// - Works well for many "Western" symbols/letters depending on app/codepage.
	// - Not guaranteed full Unicode for arbitrary codepoints.
	//
	// generate decimal digits and feed them via keypad while holding Alt.

	// Convert cp to decimal ASCII string
	char digits[12] = {0}; // enough for up to 10 digits + NUL
	uint32_t v = cp;
	int pos = 0;

	// For <= 255, prefix with '0' to prefer "ANSI" set in many contexts (best-effort).
	// This helps for common accented letters/symbols but still depends on host/app.
	if( v <= 255 )
	{
		// "0" + 3 digits (e.g. 169 -> "0169")
		digits[pos++] = '0';
		digits[pos++] = (char)('0' + ((v / 100) % 10));
		digits[pos++] = (char)('0' + ((v /  10) % 10));
		digits[pos++] = (char)('0' + ((v /   1) % 10));
		digits[pos]   = 0;
		
	} else
	{
		// No leading zeros; reverse-build then reverse back
		char tmp[12] = {0};
		int t = 0;
		while( v && t < 10 )
		{
			tmp[t++] = (char)('0' + (v % 10));
			v /= 10;
		}
		if( t == 0 ) tmp[t++] = '0';

		for( int i = t - 1; i >= 0; --i ) digits[pos++] = tmp[i];
		digits[pos] = 0;
	}

	altNumpadRaw(kb, digits);
	return( true );

#endif

	// dead end - should not reach here
	return( false );
}

////////////////////////////////////////////////////////////////////
// Unicode-aware sender 
////////////////////////////////////////////////////////////////////
static inline void sendUnicodeAware(RawKeyboard& kb, const char* s)
{
  while (*s)
  {
    uint32_t cp = 0;
    if (!utf8NextCodepoint(s, cp))
      break;

    // Keep current behavior for basic controls:
    if( cp == '\t' || cp == '\n' || cp == '\r' )
    {
      kb.write((char)cp);
      continue;
    }

    (void)sendCodepointMapped(kb, cp);
  }
}

#endif
