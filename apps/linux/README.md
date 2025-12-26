# BluKeyborg CLI (Linux)

This repository contains a **Linux command-line client** for interacting with the **Blue Keyboard / BluKeyborg dongle** over BLE.

> ⚠️ **Status**  
> This version is under active testing and is intentionally **heavy on debug logging**.

---

## ✨ Features

- BLE device discovery and connection (BlueZ)
- Secure provisioning and session setup (mTLS-like protocol)
- Encrypted command transport
- Send text and key sequences from the terminal
- Scriptable and automation-friendly
- No desktop UI required

---

## 🧩 Prerequisites

Install required build dependencies:

```bash
sudo apt install g++ make libglib2.0-dev
```

You will also need:
- BlueZ (Linux Bluetooth stack)
- Bluetooth adapter with BLE support

## 🔨 Build & Run
```bash
make
```

### List available Blue Keyboard devices:
```bash
./blukeyborg-cli --list
```

### Pairing and provisioning using dongle MAC address:
```bash
./blukeyborg-cli --prov=AA:BB:CC:DD:EE:FF
```

### Pairing and provisioning:
```bash
./blukeyborg-cli --sendstr="teststring" --to=AA:BB:CC:DD:EE:FF
```

## 🚀 Improving BLE Connectivity Speed (BlueZ Tuning)

For faster and more reliable BLE connections, adjust your BlueZ configuration.

Edit:
```
sudo nano /etc/bluetooth/main.conf
```

Add or update the following under the [General] section:

```
[General]
ControllerMode = dual
JustWorksRepairing = always
MultiProfile = off
AutoEnable = true

FastConnectable = true
DiscoverableTimeout = 0
PairableTimeout = 0
```

Restart Bluetooth:

```
sudo systemctl restart bluetooth
```

These settings can improve connection latency and reconnection behavior with the dongle.

## 🗂️ Project Structure

High-level overview of the codebase:

```
blukeyborg_cli.cpp      Entry point, CLI parsing, command dispatch
ble_transport.*        BlueZ / GATT transport layer
ble_proto.*            Binary protocol & mTLS session logic
ble_crypto.*           Cryptographic primitives (keys, HMAC, encryption)
ini_store.*            Local storage for device/app keys and settings
```

### Core Components:

- ble_transport
  - Handles BLE scanning, connection, MTU, notifications
  - Abstracts BlueZ / GLib specifics
- ble_proto
  - Implements the BluKeyborg binary protocol
  - Manages handshake, session IDs, sequencing
  - Wraps and unwraps encrypted messages
- ble_crypto
  - Cryptographic helpers used by the protocol
  - Key derivation, MACs, encryption/decryption
- ini_store
  - Persistent storage for provisioned keys
  - Allows reconnecting without re-pairing
- blukeyborg_cli
  - User-facing CLI
  - Orchestrates discovery, provisioning, and command sending

## 🔐 Security Model (High-Level)

- Uses an application-level encrypted protocol on top of BLE
- Keys are provisioned once and stored locally
- All subsequent communication is encrypted and authenticated
- BLE is treated as a transport only, not a security boundary

## 📌 Notes

- Debug output is verbose by design during this phase
- APIs and CLI flags may change as the interface is refined
- Intended for power users, scripting, and integration with password managers
