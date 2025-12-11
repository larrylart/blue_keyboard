////////////////////////////////////////////////////////////////////
// helper to send raw HID usages + modifiers
////////////////////////////////////////////////////////////////////
#pragma once
#include "USBHIDKeyboard.h"
#include "USBHIDConsumerControl.h"
#include "USB.h"

extern USBHIDConsumerControl MediaControl;

// Thin helper to send raw HID usages + modifiers.
class RawKeyboard : public USBHIDKeyboard 
{
public:
	// mods bitmask: bit0 LCtrl, bit1 LShift, bit2 LAlt, bit3 LGUI,
	//               bit4 RCtrl, bit5 RShift, bit6 RAlt (AltGr), bit7 RGUI
	void sendRaw(uint8_t mods, uint8_t usage) 
	{
		// --- NEW: check if this usage should be sent as Consumer Control ---
		// We only treat it as media if there are *no* modifiers.
		if (mods == 0 && isConsumerUsage(usage)) {
			sendConsumerUsage(usage);
			return;
		}

		// Default: behave as a normal keyboard key
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
	inline void tapUsage(uint8_t usage)            { sendRaw(0x00, usage); }
	inline void shiftUsage(uint8_t usage)          { sendRaw(0x02, usage); } // Left Shift
	inline void altGrUsage(uint8_t usage)          { sendRaw(0x40, usage); } // Right Alt
	inline void chord(uint8_t mods, uint8_t usage) { sendRaw(mods, usage);   }

private:
	// --- NEW: classify media usage IDs coming from Android fast-keys ---
	inline bool isConsumerUsage(uint8_t usage) const
	{
		// Standard HID Consumer Control usages we mapped on Android:
		// Play/Pause: 0xCD
		// Stop:       0xB7
		// Next:       0xB5
		// Prev:       0xB6
		// FF:         0xB3
		// Rew:        0xB4
		// VolUp:      0xE9
		// VolDown:    0xEA
		// Mute:       0xE2
		switch (usage) {
			case 0xCD: // Play/Pause
			case 0xB7: // Stop
			case 0xB5: // Next
			case 0xB6: // Prev
			case 0xB3: // Fast Forward
			case 0xB4: // Rewind
			case 0xE9: // Volume Up
			case 0xEA: // Volume Down
			case 0xE2: // Mute
				return true;
			default:
				return false;
		}
	}

	// delegate to core's USBHIDConsumerControl device ---
	void sendConsumerUsage(uint8_t usage) const
	{
		// The ESP32 core's USBHIDConsumerControl uses 16-bit usages.
		// Your Android side already sends the low byte (0xCD, 0xB5, 0xE9, etc.),
		// which matches the constants defined in USBHIDConsumerControl.h.
		MediaControl.press(static_cast<uint16_t>(usage));
		MediaControl.release();
	}

};

