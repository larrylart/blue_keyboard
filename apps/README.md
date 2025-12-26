# 📱 Blue Keyboard – Supported Applications

This folder documents client applications that work with the **Blue Keyboard Dongle**.  

---

## ✅ Current Status

- ✅ **Android companion app (BluKeyborg)**
- ✅ **iOS companion app (BluKeyborg)**
- ✅ **Linux command-line client**
- 🔄 **KeePassDX integrations (two approaches)**  

---

## 📘 Available Applications

### **1. BluKeyborg (Android Companion App)**

The primary Android companion app for the Blue Keyboard dongle.  
Acts as a **secure driver/service** for other apps (including KeePassDX).

Features:
- Secure BLE provisioning with mTLS
- Manual text and credential sending
- Full-screen raw HID keyboard
- Special keys & media keys panel
- Auto-reconnect and session handling
- Minimal permissions (BLE-only)

**Repository:**  
https://github.com/larrylart/blukeyborg-android

---

### **2. BluKeyborg (iOS Companion App)**

Native iOS companion app providing the same secure BLE-to-HID functionality on Apple devices.

Features:
- Secure BLE provisioning with mTLS
- Manual text and credential sending
- Special keyboard commands
- Designed for iOS BLE and background constraints

**Repository:**  
https://github.com/larrylart/blukeyborg-ios

---

### **3. KeePassDX Integration – Current Approach**

A modified KeePassDX build that integrates with BluKeyborg **via AIDL**.

- KeePassDX talks to BluKeyborg as a **driver/service**
- BluKeyborg handles BLE, security, and HID output
- Cleaner separation of concerns
- Easier to maintain and audit
- Avoids embedding BLE and crypto logic inside KeePassDX

**Repository:**  
https://github.com/larrylart/KeePassDX

---

### **4. KeePassDX-kb – Original / Legacy Approach**

An early prototype where KeePassDX directly integrated with the Blue Keyboard dongle.

- Direct BLE + protocol handling inside KeePassDX
- Tighter coupling
- Kept for reference and historical context

**Repository:**  
https://github.com/larrylart/KeePassDX-kb

---

### **5. Linux Command-Line Client**

A native Linux CLI tool for interacting with the Blue Keyboard dongle.

Features:
- Secure BLE pairing and provisioning
- Send text and key sequences from the terminal
- Scriptable / automation-friendly
- Suitable for password managers, shell workflows, and headless systems

**Repository:**  
https://github.com/larrylart/blue_keyboard/apps/linux

---


For firmware, protocol documentation, and build tools, see the main project root.

💙 **Blue Keyboard** — open-source, privacy-focused, secure cross-platform HID input.
