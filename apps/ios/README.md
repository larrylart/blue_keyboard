# BluKeyborg (iOS)

This repository contains the **main Swift interface and core integration code** for **Blue Keyboard / BluKeyborg on iOS**.

It implements secure BLE communication with the Blue Keyboard dongle and enables sending text, passwords, and special keyboard commands to a host device via:

**BLE → encrypted application protocol (mTLS) → USB HID**

---

## iOS App Repository

This repository is the **active and primary home** of the BluKeyborg iOS app:

👉 https://github.com/larrylart/blukeyborg-ios

It contains all current iOS development, source code, and documentation.

---

## Password Manager Integration

BluKeyborg on iOS is designed to work alongside **KeePassium for iOS**.

- KeePassium handles password storage and autofill
- BluKeyborg provides secure BLE-to-HID output via the Blue Keyboard dongle
- Credentials can be sent securely without exposing them to the host operating system

No direct plugin or embedded integration is required.

---

## Core BLE Communication Code

The following Swift files implement the **core BLE, security, and protocol logic** used to communicate with the Blue Keyboard dongle:

- `BluetoothDeviceManager.swift`  
  BLE scanning, connection management, GATT discovery, MTU handling, and notifications.

- `BleHub.swift`  
  High-level orchestration layer for provisioning, session lifecycle, command dispatch, and state management.

- `BleAppSec.swift`  
  Application-level security logic, including provisioning flow and mTLS-related handling.

- `Crypto.swift`  
  Cryptographic primitives used by the protocol (key derivation, HMACs, encryption/decryption).

- `Framer.swift`  
  Binary protocol framing, packet encoding/decoding, and message sequencing.

These components form the foundation of BluKeyborg’s secure BLE communication stack on iOS.

---

## Architecture Overview

- BLE is treated strictly as a **transport layer**
- Security is enforced at the **application protocol level**
- Keys are provisioned once and stored locally
- All subsequent communication is encrypted and authenticated
- HID commands are issued only after a secure session is established

---

## Status

This repository contains **active iOS development**.

The codebase continues to evolve as BLE behavior, background execution, and provisioning flows are refined to match iOS platform constraints.

---

For firmware, protocol documentation, and other platform clients (Android, Linux CLI), see the main BluKeyborg project repositories.

💙 **BluKeyborg** — open-source, privacy-focused, secure BLE-to-HID input for iOS.
