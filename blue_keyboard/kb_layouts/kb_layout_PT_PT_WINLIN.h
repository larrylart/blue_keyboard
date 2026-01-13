#pragma once
#include "kb_layout_common.h"

////////////////////////////////////////////////////////////////////
//  Portuguese (Portugal) layout (pt-PT) for Windows / Linux (ISO)
//  Host OS must be set to Portuguese (Portugal) layout.
//
//  This table maps characters that differ from US layout and/or are commonly
//  accessed via AltGr on pt-PT.
//  Accented vowels (á, à, â, ã, é, ê, í, ó, ô, õ, ú, ü, etc.) are typically
//  produced via DEAD KEYS on the host (´ ` ^ ~ ¨), so we include the dead-key
//  characters themselves, but we do NOT attempt multi-key dead-key composition
//  here (that would require sequence support beyond a single chord).
////////////////////////////////////////////////////////////////////

// If your kb_layout_common.h already defines these, these guards will do nothing.
#ifndef MOD_SHIFT
  #define MOD_SHIFT 0x02
#endif
#ifndef MOD_ALTGR
  #define MOD_ALTGR 0x40
#endif

// ---- Key usage aliases (HID Keyboard/Keypad page) ----
// USAGE NOTES:
//  '1'..'0' = 0x1E..0x27
//  '-'      = 0x2D
//  '='      = 0x2E
//  '['      = 0x2F
//  ']'      = 0x30
//  '\'      = 0x31
//  ';'      = 0x33
//  QUOTE    = 0x34 (QUOTE_KEY from common header)
//  ','      = 0x36
//  '.'      = 0x37
//  '/'      = 0x38
//
// pt-PT is ISO and uses an extra ISO key left of Z (< >), provided via ISO_BSLASH_KEY (0x64).

// On pt-PT the key at US '-' (0x2D) typically yields:  ' (base)  ? (shift)
// and also « » on AltGr states (see kbdlayout view). 
#define PT_KEY_QUOTE_QMARK    0x2D

// On pt-PT the key at US '=' (0x2E) yields:  + (base)  * (shift)
#define PT_KEY_PLUS_STAR      0x2E

// On pt-PT the key at US '[' (0x2F) is a dead-key cluster shown as: ¨  `  ´  ] in the kbdlayout view. 
// We treat it as "the deadkey physical key". (It also produces ']' in some states.)
#define PT_KEY_DEAD_DIA_GR_ACUTE_RBR  0x2F

// On pt-PT the key at US ']' (0x30) is often another dead-key cluster (commonly ^ and ~ shown near the Ç/ªº area). 
// In the kbdlayout view, "^" and "~" appear on the row with Ç/ªº. Those are typically on the key next to Ç.
// We'll map them using the usage that corresponds to that physical key.
// On most ISO layouts, that key is QUOTE_KEY (0x34) on US, but on pt-PT QUOTE_KEY is not that symbol.
// Instead, we directly bind ^/~ to the key shown after ª/º (commonly usage 0x32 on ISO, but pt-PT uses ISO_HASH_KEY for an extra key near Enter).
// To avoid guessing wrong, we map ^/~ to the KEY shown on the layout: it’s right after ª/º on the row with Ç in the kbdlayout listing,
// which corresponds to the key at US '\' (0x31) position in many ISO layouts.
// If this lands wrong on your tests, this is the one constant to tweak.
#define PT_KEY_CIRC_TILDE     0x31

// Dedicated pt-PT Ç key is where US ';' (0x33) sits, per the kbdlayout view. 
#define PT_KEY_C_CEDILLA      0x33

// Ordinals ª/º appear next to Ç on pt-PT (same row). In the kbdlayout listing, ª and º are shown right after ç. 
// That key is typically QUOTE_KEY (0x34) on ANSI US; on pt-PT it’s a distinct physical key.
#define PT_KEY_ORDINALS       QUOTE_KEY

// ISO key left of Z: < >
#define PT_KEY_LTGT           ISO_BSLASH_KEY


////////////////////////////////////////////////////////////////////
//  Table: { Unicode codepoint, modifiers, HID usage }
////////////////////////////////////////////////////////////////////
static const KbMapEntry KBMAP_PT_PT_WINLIN[] =
{
  // ----- Digits row (Shift layer on pt-PT) -----
  { U'!',  MOD_SHIFT, 0x1E }, // Shift+1
  { U'"',  MOD_SHIFT, 0x1F }, // Shift+2
  { U'#',  MOD_SHIFT, 0x20 }, // Shift+3
  { U'$',  MOD_SHIFT, 0x21 }, // Shift+4
  { U'%',  MOD_SHIFT, 0x22 }, // Shift+5
  { U'&',  MOD_SHIFT, 0x23 }, // Shift+6
  { U'/',  MOD_SHIFT, 0x24 }, // Shift+7
  { U'(',  MOD_SHIFT, 0x25 }, // Shift+8
  { U')',  MOD_SHIFT, 0x26 }, // Shift+9
  { U'=',  MOD_SHIFT, 0x27 }, // Shift+0

  // ----- AltGr layer on digits row (pt-PT) -----
  { U'@',  MOD_ALTGR, 0x1F }, // AltGr+2 
  { U'£',  MOD_ALTGR, 0x20 }, // AltGr+3 
  { U'§',  MOD_ALTGR, 0x21 }, // AltGr+4 
  { 0x20AC /*€*/, MOD_ALTGR, 0x22 }, // AltGr+5 

  // Braces / brackets
  { U'{',  MOD_ALTGR, 0x24 }, // AltGr+7 
  { U'[',  MOD_ALTGR, 0x25 }, // AltGr+8 
  { U']',  MOD_ALTGR, 0x26 }, // AltGr+9 
  { U'}',  MOD_ALTGR, 0x27 }, // AltGr+0 

  // ----- Key after 0 (physical US '-') :  ' and ? and also « » on AltGr states -----
  { U'\'', 0x00,      PT_KEY_QUOTE_QMARK }, // ' 
  { U'?',  MOD_SHIFT, PT_KEY_QUOTE_QMARK }, // ? 
  { 0x00AB /*«*/, MOD_ALTGR, PT_KEY_QUOTE_QMARK }, // « 
  { 0x00BB /*»*/, MOD_ALTGR | MOD_SHIFT, PT_KEY_QUOTE_QMARK }, // » 

  // ----- Plus/Star key (physical US '=') -----
  { U'+',  0x00,      PT_KEY_PLUS_STAR },
  { U'*',  MOD_SHIFT, PT_KEY_PLUS_STAR }, 

  // ----- Dead-key characters (so users can send them literally if needed) -----
  // From the kbdlayout view: ¨ ` ´ appear on the key near P. 
  { 0x00A8 /*¨*/, 0x00,      PT_KEY_DEAD_DIA_GR_ACUTE_RBR },
  { U'`',          MOD_SHIFT, PT_KEY_DEAD_DIA_GR_ACUTE_RBR },
  { 0x00B4 /*´*/,  MOD_ALTGR, PT_KEY_DEAD_DIA_GR_ACUTE_RBR },

  // ^ and ~ shown on the row with Ç/ªº. 
  { U'^',  0x00,      PT_KEY_CIRC_TILDE },
  { U'~',  MOD_SHIFT, PT_KEY_CIRC_TILDE },

  // ----- Dedicated Portuguese letters / ordinals -----
  { U'ç',  0x00,      PT_KEY_C_CEDILLA },   
  { U'Ç',  MOD_SHIFT, PT_KEY_C_CEDILLA },   
  { 0x00AA /*ª*/, 0x00,      PT_KEY_ORDINALS },      
  { 0x00BA /*º*/, MOD_SHIFT, PT_KEY_ORDINALS },   

  // ----- ISO key left of Z: < > -----
  { U'<',  MOD_SHIFT, PT_KEY_LTGT }, // In kbdlayout it lists > then < (visual order), but mapping is < and > on this ISO key. 
  { U'>',  0x00,      PT_KEY_LTGT }, 

  // ----- Bottom-row punctuation differs: comma/semicolon/dot/colon/underscore/hyphen are as shown -----
  // The kbdlayout listing shows "; ,", ": .", "_ -" on the last letter row. 
  { U';',  0x00,      0x36 /* ',' key */ },
  { U',',  MOD_SHIFT, 0x36 /* ',' key */ },
  { U':',  0x00,      0x37 /* '.' key */ },
  { U'.',  MOD_SHIFT, 0x37 /* '.' key */ },
  { U'_',  0x00,      0x38 /* '/' key */ },
  { U'-',  MOD_SHIFT, 0x38 /* '/' key */ },

  // ----- Alternate Euro (pt-PT also shows € on AltGr+E) -----
  { 0x20AC /*€*/, MOD_ALTGR, 0x08 /* 'E' */ }, 
};


// Accessor used by your dispatcher
static inline const KbMapEntry* kb_get_PT_PT_WINLIN_map(uint16_t& count)
{
  count = (uint16_t)(sizeof(KBMAP_PT_PT_WINLIN) / sizeof(KBMAP_PT_PT_WINLIN[0]));
  return KBMAP_PT_PT_WINLIN;
}
