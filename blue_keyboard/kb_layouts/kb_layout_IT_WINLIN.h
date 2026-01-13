#pragma once
#include "kb_layout_common.h"

////////////////////////////////////////////////////////////////////
//  Italian layout (IT) for Windows / Linux (ISO)
//  Host OS must be set to Italian layout.
//
//  Source basis:
//   - Physical key legends for IT ISO: 
//   - AltGr provides @ # [ ] { } € (and more on Linux): 
//
//  Goal:
//   - Provide a character->(mods+usage) table for all the “US-different” cases,
//     especially punctuation/symbols and Italian accented letters.
////////////////////////////////////////////////////////////////////

// Modifier bits used in your RawKeyboard chord system
#ifndef MOD_SHIFT
  #define MOD_SHIFT 0x02
#endif
#ifndef MOD_ALTGR
  #define MOD_ALTGR 0x40
#endif

// ---- Key usage aliases (HID Keyboard/Keypad page) ----
// We bind these to the *physical* key positions as used by typical ISO boards.
// The chart shows these keys explicitly. 

// Row: digits 1..0 usages
//  '1'..'0' = 0x1E..0x27

// Key where US has '-' '_' ; Italian has:  '  and  ?
#define IT_KEY_APOSTROPHE     0x2D

// Key where US has '=' '+' ; Italian has:  ì  and  ^
#define IT_KEY_I_GRAVE        0x2E

// Key where US has '[' '{' ; Italian has:  è  and  é
#define IT_KEY_E_GRAVE        0x2F

// Key where US has ']' '}' ; Italian has:  +  and  *  (and AltGr layer gives [ ])
#define IT_KEY_PLUS_STAR      0x30

// Key where US has ';' ':' ; Italian has:  ç
#define IT_KEY_C_CEDILLA      0x33

// Key where US has quote/apostrophe ; Italian has:  ò  and  °
#define IT_KEY_O_GRAVE        QUOTE_KEY   // 0x34 in your common header

// ISO-only key near Enter (usage 0x32 in your code):
// Italian chart shows the right-side “ù” key before Enter. 
#define IT_KEY_U_GRAVE        ISO_HASH_KEY  // 0x32

// The remaining right-side key between ò and ù on many IT ISO boards is the “à / § / #” key.
// Depending on firmware/framework naming, this often corresponds to usage 0x31 on some stacks.
// If your board/stack makes “à” land wrong, this is the *one* constant you may need to tweak.
#define IT_KEY_A_GRAVE        0x31

// ISO key left of Z (usage 0x64 in your code): provides < >
#define IT_KEY_LTGT           ISO_BSLASH_KEY // 0x64


////////////////////////////////////////////////////////////////////
//  Table
////////////////////////////////////////////////////////////////////
//
// Each entry: { Unicode codepoint, modifiers, HID usage }
//
// Notes:
//  - Uppercase accented vowels vary between OSes (some prefer CapsLock paths).
//    We still include explicit Shift variants where the layout normally supports it.
//  - Braces/backslash/pipe/ticks can vary between Windows vs Linux IT.
//    The “core” ones (@ # [ ] € è é ò à ù ç) are consistent.
////////////////////////////////////////////////////////////////////

static const KbMapEntry KBMAP_IT_WINLIN[] = {

  // ----- Digits row: Shift layer symbols (IT differs from US mapping) -----
  { U'!',  MOD_SHIFT, 0x1E, 0, 0 }, // Shift+1
  { U'"',  MOD_SHIFT, 0x1F, 0, 0 }, // Shift+2
  { U'£',  MOD_SHIFT, 0x20, 0, 0 }, // Shift+3
  { U'$',  MOD_SHIFT, 0x21, 0, 0 }, // Shift+4
  { U'%',  MOD_SHIFT, 0x22, 0, 0 }, // Shift+5
  { U'&',  MOD_SHIFT, 0x23, 0, 0 }, // Shift+6
  { U'/',  MOD_SHIFT, 0x24, 0, 0 }, // Shift+7
  { U'(',  MOD_SHIFT, 0x25, 0, 0 }, // Shift+8
  { U')',  MOD_SHIFT, 0x26, 0, 0 }, // Shift+9
  { U'=',  MOD_SHIFT, 0x27, 0, 0 }, // Shift+0

  // Apostrophe key on IT: ' and ?
  { 0x27 /*'*/, 0x00,      IT_KEY_APOSTROPHE, 0, 0 }, // '
  { U'?',        MOD_SHIFT, IT_KEY_APOSTROPHE, 0, 0 }, // ?

  // ì/^ key
  { U'ì',  0x00,      IT_KEY_I_GRAVE, 0, 0 },
  { U'Ì',  MOD_SHIFT, IT_KEY_I_GRAVE, 0, 0 },
  { U'^',  MOD_SHIFT, IT_KEY_I_GRAVE, 0, 0 },

  // ----- Right side alpha block -----
  // è/é key
  { U'è',  0x00,      IT_KEY_E_GRAVE, 0, 0 },
  { U'é',  MOD_SHIFT, IT_KEY_E_GRAVE, 0, 0 },
  { U'È',  MOD_SHIFT, IT_KEY_E_GRAVE, 0, 0 }, // (often same combo as é; keep for completeness)

  // +/* key and its AltGr layer [ ]
  { U'+',  0x00,      IT_KEY_PLUS_STAR, 0, 0 },
  { U'*',  MOD_SHIFT, IT_KEY_PLUS_STAR, 0, 0 },
  { U'[',  MOD_ALTGR, IT_KEY_PLUS_STAR, 0, 0 },                    // AltGr + key
  { U']',  (uint8_t)(MOD_ALTGR | MOD_SHIFT), IT_KEY_PLUS_STAR, 0, 0 }, // AltGr+Shift + key

  // ç key (position of US ';')
  { U'ç',  0x00,      IT_KEY_C_CEDILLA, 0, 0 },
  { U'Ç',  MOD_SHIFT, IT_KEY_C_CEDILLA, 0, 0 },

  // ò/°/@ key (position of US quote)
  { U'ò',  0x00,      IT_KEY_O_GRAVE, 0, 0 },
  { U'Ò',  MOD_SHIFT, IT_KEY_O_GRAVE, 0, 0 },
  { U'°',  MOD_SHIFT, IT_KEY_O_GRAVE, 0, 0 },
  { U'@',  MOD_ALTGR, IT_KEY_O_GRAVE, 0, 0 },

  // à/§/# key (between ò and ù)
  { U'à',  0x00,      IT_KEY_A_GRAVE, 0, 0 },
  { U'À',  MOD_SHIFT, IT_KEY_A_GRAVE, 0, 0 },
  { U'§',  MOD_SHIFT, IT_KEY_A_GRAVE, 0, 0 },
  { U'#',  MOD_ALTGR, IT_KEY_A_GRAVE, 0, 0 },

  // ù key (ISO hash key position)
  { U'ù',  0x00,      IT_KEY_U_GRAVE, 0, 0 },
  { U'Ù',  MOD_SHIFT, IT_KEY_U_GRAVE, 0, 0 },

  // ISO key left of Z: < >
  { U'<',  0x00,      IT_KEY_LTGT, 0, 0 },
  { U'>',  MOD_SHIFT, IT_KEY_LTGT, 0, 0 },

  // ----- Common programming symbols (typical IT AltGr mappings) -----
  // Euro is commonly AltGr+E on IT Windows/Linux
  { U'€',  MOD_ALTGR, 0x08 /*E*/, 0, 0 },

  // backslash/pipe often on AltGr+ù
  { U'\\', MOD_ALTGR, IT_KEY_U_GRAVE, 0, 0 },
  { U'|',  (uint8_t)(MOD_ALTGR | MOD_SHIFT), IT_KEY_U_GRAVE, 0, 0 },

  // grave/tilde (often AltGr on ì/^ on Linux)
  { U'`',  MOD_ALTGR, IT_KEY_I_GRAVE, 0, 0 },
  { U'~',  (uint8_t)(MOD_ALTGR | MOD_SHIFT), IT_KEY_I_GRAVE, 0, 0 },

  // underscore: often Shift on the slash key (usage 0x38)
  { U'_',  MOD_SHIFT, 0x38, 0, 0 }, // Shift + '/'
};



// Accessor used by your dispatcher
static inline const KbMapEntry* kb_get_IT_WINLIN_map(uint16_t& count)
{
  count = (uint16_t)(sizeof(KBMAP_IT_WINLIN) / sizeof(KBMAP_IT_WINLIN[0]));
  return KBMAP_IT_WINLIN;
}