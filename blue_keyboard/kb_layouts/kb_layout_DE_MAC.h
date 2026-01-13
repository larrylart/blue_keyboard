#pragma once
#include "kb_layout_common.h"

////////////////////////////////////////////////////////////////////
//  German layout (DE) for macOS (ISO)
//  Host OS must be set to a German input source.
//
//  IMPORTANT NOTE (macOS input source difference):
//   - "German" (Apple) often uses @ on Option+L
//   - "German Standard" uses @ on Option+Q (and € on Option+E)
//
//  This table maps characters that differ from US-ASCII typing assumptions.
////////////////////////////////////////////////////////////////////

// If you use "German Standard" input source in macOS, enable this:
// #define DE_MAC_GERMAN_STANDARD 1

// Choose which physical key produces '@' with Option
#ifdef DE_MAC_GERMAN_STANDARD
  #define DE_MAC_AT_KEY 0x14  // Q
#else
  #define DE_MAC_AT_KEY 0x0F  // L
#endif

// Common HID usage
#ifndef KEY_SPACE
  #define KEY_SPACE 0x2C
#endif

////////////////////////////////////////////////////////////////////
//  Table
//    Each entry: { codepoint, mods1, key1, mods2, key2 }
//    - For "dead key then space" we use key2=SPACE.
////////////////////////////////////////////////////////////////////

static const KbMapEntry KBMAP_DE_MAC[] =
{
  // ---- QWERTZ swaps ----
  { 'y',       0,        0x1D, 0, 0 },            // y -> Z key
  { 'Y',       MOD_SHIFT,0x1D, 0, 0 },            // Y -> Shift+Z
  { 'z',       0,        0x1C, 0, 0 },            // z -> Y key
  { 'Z',       MOD_SHIFT,0x1C, 0, 0 },            // Z -> Shift+Y

  // ---- Core German letters ----
  { 0x00E4,    0,        QUOTE_KEY, 0, 0 },       // ä
  { 0x00C4,    MOD_SHIFT,QUOTE_KEY, 0, 0 },       // Ä
  { 0x00F6,    0,        0x33, 0, 0 },            // ö   (physical ; key)
  { 0x00D6,    MOD_SHIFT,0x33, 0, 0 },            // Ö
  { 0x00FC,    0,        0x2F, 0, 0 },            // ü   (physical [ key)
  { 0x00DC,    MOD_SHIFT,0x2F, 0, 0 },            // Ü
  { 0x00DF,    0,        0x2D, 0, 0 },            // ß   (physical - key)

  // ---- Digits row symbols that differ from US typing assumptions ----
  { '"',       MOD_SHIFT,0x1F, 0, 0 },            // " = Shift+2   (DE)
  { 0x00A7,    MOD_SHIFT,0x20, 0, 0 },            // § = Shift+3   (DE)
  { '+',       0,        0x2E, 0, 0 },            // + key (physical '=' key position)
  { '*',       MOD_SHIFT,0x2E, 0, 0 },            // * = Shift on that key

  // ---- ISO key left of Z: < > ----
  { '<',       0,        ISO_BSLASH_KEY, 0, 0 },  // <
  { '>',       MOD_SHIFT,ISO_BSLASH_KEY, 0, 0 },  // >

  // ---- Common punctuation differences you already used on Win/Linux too ----
  { '/',       MOD_SHIFT,0x24, 0, 0 },            // / = Shift+7
  { '?',       MOD_SHIFT,0x2D, 0, 0 },            // ? = Shift+ß
  { '#',       0,        ISO_HASH_KEY, 0, 0 },    // # = ISO hash key (your existing behavior)
  { '\'',      MOD_SHIFT,ISO_HASH_KEY, 0, 0 },    // ' = Shift+ISO hash key

  // ---- macOS Option (Alt) layer common programming symbols ----
  { 0x20AC,    MOD_ALT,  0x08, 0, 0 },            // € = Option+E
  { '@',       MOD_ALT,  DE_MAC_AT_KEY, 0, 0 },   // @ = Option+L or Option+Q (see switch above)

  { '[',       MOD_ALT,  0x22, 0, 0 },            // [ = Option+5
  { ']',       MOD_ALT,  0x23, 0, 0 },            // ] = Option+6
  { '{',       MOD_ALT,  0x25, 0, 0 },            // { = Option+8
  { '}',       MOD_ALT,  0x26, 0, 0 },            // } = Option+9
  { '|',       MOD_ALT,  0x24, 0, 0 },            // | = Option+7
  { '\\',      (uint8_t)(MOD_ALT | MOD_SHIFT), 0x24, 0, 0 }, // \ = Option+Shift+7

  // ---- Dead keys: handle by sending dead key then Space ----
  // On macOS, Option+N is the tilde dead key, then Space yields "~". 
  { '~',       MOD_ALT,  0x11 /* N */, 0, KEY_SPACE }, // ~ = Option+N then Space

  // The German "^ / °" key is often a dead key on macOS too; Space yields "^".
  { '^',       0,        0x35, 0, KEY_SPACE },    // ^ dead -> ^ then Space
  { 0x00B0,    MOD_SHIFT,0x35, 0, 0 },            // ° = Shift + (same key)
};

static inline const KbMapEntry* kb_get_DE_MAC_map(uint16_t& count)
{
  count = (uint16_t)(sizeof(KBMAP_DE_MAC) / sizeof(KBMAP_DE_MAC[0]));
  return KBMAP_DE_MAC;
}
