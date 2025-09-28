#pragma once
#include "USBHIDKeyboard.h"
#include "USB.h"

// Thin helper to send raw HID usages + modifiers.
class RawKeyboard : public USBHIDKeyboard {
public:
  // mods bitmask: bit0 LCtrl, bit1 LShift, bit2 LAlt, bit3 LGUI,
  //               bit4 RCtrl, bit5 RShift, bit6 RAlt (AltGr), bit7 RGUI
  void sendRaw(uint8_t mods, uint8_t usage) {
    KeyReport rpt = {};           // press
    rpt.modifiers = mods;
    rpt.keys[0]   = usage;        // one usage
    this->sendReport(&rpt);
    delay(2);

    KeyReport up = {};            // release
    this->sendReport(&up);
    delay(1);
  }

  // Convenience
  inline void tapUsage(uint8_t usage)           { sendRaw(0x00, usage); }
  inline void shiftUsage(uint8_t usage)         { sendRaw(0x02, usage); } // Left Shift
  inline void altGrUsage(uint8_t usage)         { sendRaw(0x40, usage); } // Right Alt
  inline void chord(uint8_t mods, uint8_t usage){ sendRaw(mods, usage);   }
};
