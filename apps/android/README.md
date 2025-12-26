# BluKeyborg

This repository contains legacy and experimental work related to **BluKeyborg** and its integration with password managers and external clients.

---

## Android App Repository

> **Note**  
> The BluKeyborg Android companion app has been moved to its own dedicated repository:

👉 https://github.com/larrylart/blukeyborg-android

That repository now contains all active Android development, releases, and documentation.

---

## KeePassDX Integration Prototypes

Work is ongoing on two different prototype approaches for integrating BluKeyborg with **KeePassDX**.

### 1. Recent / Current Approach (Recommended)

👉 https://github.com/larrylart/KeePassDX

- Uses **AIDL** to communicate with the BluKeyborg Android app
- BluKeyborg acts as a **driver/service**
- Cleaner separation of concerns
- Better long-term maintainability and security

### 2. Old / Initial Approach (Deprecated)

👉 https://github.com/larrylart/KeePassDX-kb

- Direct integration of the Blue Keyboard dongle inside KeePassDX
- Tighter coupling
- Kept for reference and historical context

---

## Core BLE Communication Code

The following files contain the **core BLE and security logic** used to communicate with the Blue Keyboard dongle:

- `BluetoothDeviceManager.kt` — BLE scanning, connection, and GATT handling
- `BleHub.kt` — provisioning, session management, and command API
- `BleAppSec.kt` — application-level security and mTLS-related logic

These components form the foundation of BluKeyborg’s secure BLE communication stack.

---

## Status

This repository is kept mainly for reference as development continues in the platform-specific repositories (Android, iOS, firmware, etc.).
