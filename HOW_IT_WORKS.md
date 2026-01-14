## How It Works (High-Level)

1. Connect the dongle to your computer via USB.  
   - The dongle enumerates as a USB HID keyboard.  
   - The TFT typically shows a status like "READY".

2. On your phone, open **KeePassDX-kb** (modified KeePassDX build)  
   - In KeePassDX-kb, configure the "output device" as the Blue Keyboard dongle.  
   - Pair the phone and dongle if not already paired.

3. From KeePassDX-kb or BluKeyborg
   - The phone uses BLE to connect to the dongle.  
   - Starting from v1.2.1, it performs:
     - AppKey onboarding (once per device) if needed
     - A binary MTLS handshake on top of BLE
   - Afterwards it sends an encrypted SEND_STRING command.

4. The dongle:
   - Decrypts and validates the command using MTLS
   - Converts the characters to HID keycodes using the selected layout profile
   - Types the characters to the host as keypress events
   - Sends a response with an MD5 of the received payload so the app can verify that everything was emitted correctly.

---

## Layout-Aware Typing

The dongle supports multiple keyboard layouts, especially around `WINLIN` (Windows/Linux) and `MAC` variants.

Internally:

- Layout configurations are stored and managed in `layout_kb_profiles.h`.
- Each profile defines how characters map to HID scan codes and modifiers.
- Some keys (like `@`, `"`, `€`, etc.) differ between layouts, so they are handled via per-layout rules instead of hardcoded ASCII mappings.

The app can ask the dongle to:

- **Set a layout** (e.g. `UK_WINLIN`, `IE_WINLIN`, `US_WINLIN`, etc.)
- **Query current layout / info** (useful for diagnostics).

With the move to v1.2.1, these operations now use the **binary MTLS-protected opcodes**:

- `0xC0` – SET_LAYOUT  
- `0xC1` – GET_INFO  
- `0xC2` – INFO_VALUE (response)

---
