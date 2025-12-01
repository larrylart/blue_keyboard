# 📱 Blue Keyboard – Supported Applications

This folder contains client applications that work with the **Blue Keyboard Dongle**.  
Each app implements the BLE → encrypted protocol → USB HID workflow in a platform-specific way.

Current status:

- ✅ **Android apps available**
- ⏳ **Linux C client in research**
- 🖥️ More platforms planned

---

## 📘 Available Applications

### **1. KeePassDX-kb (Modified KeePassDX Fork)**
A modified KeePassDX build with native Blue Keyboard Dongle support:

- Encrypted BLE handshake  
- Automatic AppKey provisioning  
- “Send Password via Dongle” integration  
- No additional desktop software required  

**Repository:**  
https://github.com/larrylart/KeePassDX-kb


---

### **2. BluKeyborg (Android Companion App)**
A standalone Android app for typing and sending text to the dongle:

- Manual text sending  
- Full-screen raw HID keyboard  
- Special keys panel  
- Auto-reconnect to your dongle  
- Lightweight & BLE-only permissions  

**Repository:**  
https://github.com/larrylart/blue_keyboard/apps/android

---

## 🛠️ Coming Soon

### **Linux C Client**
A native terminal-friendly tool for:

- Secure pairing with the dongle  
- Sending text or key sequences from Linux  
- CLI automation (password filling, scripting)  

**Status:** In research.

---

For firmware, protocol documentation, and build tools, see the main project root.

💙 Blue Keyboard Project – Open source, privacy-focused, cross-platform.
