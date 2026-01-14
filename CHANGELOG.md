## 🔄 Update **v2.1.0** (Latest)

### ✨ New & Improved
- **Expanded ESP32-S3 hardware support**  
  Added support for additional ESP32-S3 boards, including compact USB-C devices and
  boards without a display.

- **Support for no-display (“dark mode”) boards**  
  The firmware can now run on ESP32-S3 hardware without a screen or LED, enabling use
  on minimal or embedded devices.

- **Keyboard layout system restructured**  
  Internal keyboard layout handling was refactored to make localization easier and
  improve long-term maintainability.

- **Improved BLE reliability**  
  Further improvements to BLE connectivity and stability, reducing edge-case failures
  during connect, pairing, and provisioning.

### 🛠️ Firmware Artifacts (v2.1.0)

Precompiled firmware is provided for the following boards:

- **LilyGO S3 T-Dongle (with screen)**  
  `bluekb_firmware_v2.1.0_esp32s3_lilygo_tdongle.bin`

- **LilyGO S3 T-Dongle (no screen / dark mode)**  
  `bluekb_firmware_v2.1.0_esp32s3_lilygo_tdongle_nolcd.bin`

- **Waveshare ESP32-S3 Zero**  
  `bluekb_firmware_v2.1.0_esp32s3_waveshare_zero.bin`

- **Seeed Studio XIAO ESP32-S3**  
  `bluekb_firmware_v2.1.0_esp32s3_seeed_studio_xiao.bin`

- **Seeed Studio XIAO ESP32-S3 Plus**  
  `bluekb_firmware_v2.1.0_esp32s3_xiao_plus.bin`

- **Generic ESP32-S3 (no display / no LED)**  
  `bluekb_firmware_v2.1.0_esp32s3_generic_dark.bin`

> **Note:**  
> Boards without a display use the generic or board-specific *dark* firmware and follow
> the same setup flow, with minor differences documented in `docs/setup.md`.

### 🔄 Update **v2.0.0** 
- mTLS provisioning simplified. Change is not backwards compatible, make sure you use the latest client apps for this to work.
- fixes some mTLS protocol inconsistences
- improvements in BLE connectivity
- fixes bugs in pairing, provisioning flow

### 🔄 Update **v1.2.3** 
- Fixes for several connectivity bugs
- Adds support for allowing the dongle to accept multiple apps or devices, depending on the user's choice during the initial setup.

### 🔄 Update **v1.2.2** 
- Fast Keys Support - The dongle now implements the new fast-key HID command mode.
- The release package also includes the BluKeyborg Android companion app for general text sending and testing.

### 🔄 Update **v1.2.1 – Binary MTLS + Wi-Fi Setup Portal**

- 🔐 **New binary MTLS protocol (PROTO 1.2)**  
  - Per-session ECDH P-256 key exchange  
  - Session key derived via HKDF-SHA256, salted with the long-term AppKey  
  - AES-CTR encryption for all application payloads  
  - HMAC-SHA256 (truncated) for integrity  
  - Per-direction sequence numbers and session IDs for replay protection  
  - Implements binary framed commands (`B0/B1/B2/B3/etc`).

- 🌐 **First-run Wi-Fi setup portal**  
  - On initial boot or after factory reset, the dongle starts a Wi-Fi AP (`BLUKBD-XXXX`) and shows the password on the TFT.  
  - A captive web portal at `http://192.168.4.1/` lets you configure:
    - BLE name  
    - Host keyboard layout (e.g. `UK_WINLIN`, `IE_WINLIN`, `US_MAC`, etc.)  
    - A setup password, used later to derive a PBKDF2-SHA256 verifier for secure AppKey onboarding.

- 🔑 **Secure AppKey onboarding**  
  - The AppKey is not sent or stored in plain form.  
  - The Android client uses the setup password to:
    - Re-derive a PBKDF2-HMAC-SHA256 verifier from salt + iteration count returned by the dongle  
    - Perform a challenge/response HMAC check  
    - Receive an AES-CTR encrypted AppKey blob with HMAC integrity  
  - The AppKey is then stored only on the dongle (NVS) and the client app (e.g. in Android Keystore).

- 🧩 **Binary application protocol under MTLS**  
  - Layout switching, info queries, resets and string sends are now carried in encrypted frames:
    - `OP | LEN_lo | LEN_hi | PAYLOAD`
  - Important opcodes (inside MTLS):
    - `0xC0` – SET_LAYOUT  
    - `0xC1` – GET_INFO → `0xC2` INFO_VALUE  
    - `0xC4` – RESET_TO_DEFAULT (clears AppKey + setup flags so Wi-Fi portal runs again)  
    - `0xD0` – SEND_STRING → `0xD1` SEND_RESULT (status + MD5 of received payload bytes).

- 🧠 **Improved layout & typing engine**  
  - Layouts centralized in `layout_kb_profiles.h`.  
  - Layout names like `UK_WINLIN`, `IE_WINLIN`, `US_WINLIN`, `DE_WINLIN`, `FR_MAC`, etc.  
  - Layout-aware Unicode → HID mapping for more reliable typing.
