## How It Works (High-Level)

The Blue Keyboard / BluKeyborg system is a **secure BLE → USB HID bridge**.
A trusted app sends commands over BLE, the dongle validates and decrypts them,
then emits USB keyboard events to the host computer.

The host OS is treated as **untrusted**.

---

## 1. Dongle Connection & Idle State

1. Plug the dongle into a computer via USB.
   - The dongle enumerates as a **standard USB HID keyboard**.
   - No drivers or host software are required.
   - If a display is present, it shows the current state (READY / SETUP / ETC).

**Flow:**  `Dongle → USB → Host OS (HID keyboard)`

---

## 2. First-Time Setup (Provisioning Preparation)

First-time setup purpose is about **preparing secure provisioning**.

### What setup does
- Personalizes the device (BLE name, layout, policy)
- Creates a **provisioning password**
- Stores a **hashed verifier** of that password on the dongle
- Enables later retrieval of **MTLS key material** by an authorized app

⚠️ The provisioning password **is not used for typing** and **is not the MTLS key**.

### How setup is performed
- The dongle starts a **Wi-Fi AP + setup portal**
- The user connects once and sets:
  - BLE device name
  - Keyboard layout
  - Provisioning password

**Flow:** `User → Wi-Fi AP → Setup Portal → Provisioning Password Stored`

(Display-equipped dongles will also show a WIFI/URL QR code to speed this up, headless dongles rely on a hardcoded password)

---

## 3. App Onboarding & MTLS Provisioning (One-Time per App)

From **BluKeyborg** or **KeePassDX-experimental-forks**:

1. The app scans and connects over **BLE**
2. If the app is not yet provisioned:
   - The app asks for the **provisioning password**
   - Performs **one-time MTLS key provisioning**
   - Stores device-specific credentials securely

After this step, the app is authorized.

**Flow:**  `App → BLE → Provisioning Check → MTLS Key Provisioned`

This step happens **once per app per device**.

---

## 4. Secure Session Establishment (Every Connection)

On each subsequent connection:

1. App connects to the dongle over encrypted BLE (AES-CCM 128-bit)
2. A **binary MTLS handshake** is performed:
   - Ephemeral ECDH (P-256)
   - HKDF-derived session keys
   - AES-CTR encryption + HMAC authentication

No plaintext commands or keystrokes are sent.

**Flow:**  `App ↔ BLE ↔ MTLS Handshake ↔ Secure Session`

---

## 5. Sending Text & Commands

Once the secure session is active:

1. The app sends a **binary command** (e.g. SEND_STRING)
2. The payload is encrypted and authenticated (custom mTLS PBKDF2-HMAC-SHA256)
3. The dongle:
   - Decrypts and validates the packet
   - Converts characters to HID keycodes
   - Emits keypresses over USB

4. The dongle sends a response (md5 of the encrypted payload) so the app can verify delivery

**Flow:**  `App → (Encrypted BLE) → Dongle → USB HID → Host`

---

## Layout-Aware Typing

Keyboard layout correctness is handled **on the dongle**, not in the app.

### Internals
- Layout profiles are defined in `kb_layouts/*`
- Each profile maps characters to:
  - HID scan codes
  - Required modifiers
- Separate variants exist for:
  - `WINLIN` (Windows / Linux)
  - `MAC` (macOS)

This avoids unreliable ASCII typing and fixes layout-specific characters such as `@`, `"`, `€`, `#`, `~`, etc.

### Runtime control
The app can:
- Set the active layout
- Query current layout and firmware info

These operations use **binary MTLS-protected opcodes**:

- `0xC0` – SET_LAYOUT  
- `0xC1` – GET_INFO  
- `0xC2` – INFO_VALUE (response)

---

## Security Model Summary

- Host computer is **untrusted**
- BLE traffic is encrypted end-to-end with BLE transport encryption (AES-CCM 128-bit) + custom application-layer “micro-TLS” (payload encryption) - PBKDF2-HMAC-SHA256.
- Apps must be explicitly provisioned
- Optional/default setup, lock-down to one app/device, flags prevents rogue apps from using the USB HID device.

The dongle only types **what an authorized app explicitly sends** — nothing more.
