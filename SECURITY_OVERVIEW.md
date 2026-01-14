## Security Model (Conceptual)

At a very high level:

1. **BLE Link Layer**  
   - BLE link encryption + bonding is used for basic link security (prevents casual eavesdropping).

2. **Application MTLS Layer (v1.2.1)**  
   - On top of BLE, a binary "micro-TLS" (MTLS) layer protects all application data:
     - AppKey-based HMAC and HKDF
     - ECDH P-256 key exchange
     - AES-CTR encryption
     - HMAC authentication
     - Sequence-based replay protection

3. **Host Computer**  
   - The host sees a standard USB keyboard, with no special drivers or software.  
   - The security assumption is that the dongle is trusted and the PC treats it as a local keyboard.

4. **Phone / App**  
   - The Android app manages the KeePass database and holds the AppKey.  
   - Sensitive data never needs to be stored or synced on the host computer.

---

## AppKey Onboarding (One-Time per Client Device)

The setup password is never stored on the dongle in clear.  
Instead, the firmware stores a PBKDF2-HMAC-SHA256 verifier (plus salt and iteration count) and uses a small challenge/response protocol so the Android app can securely obtain the AppKey:

1. App asks user for the setup password.
2. App sends `A0` (GET_APPKEY_REQUEST).
3. Dongle replies with `A2` (CHALLENGE) containing:
   - PBKDF2 salt
   - PBKDF2 iteration count
   - Random challenge value.
4. App recomputes PBKDF2 locally and sends an HMAC proof in `A3` (APPKEY_PROOF).
5. If valid, the dongle encrypts its internal AppKey with AES-CTR, appends an HMAC, and returns it in `A1` (WRAPPED_APPKEY).
6. The app verifies and decrypts the payload and stores the AppKey on its side.

After this, both sides share the same AppKey without ever sending it in clear.

---

## MTLS Handshake & Binary Protocol

Once both sides know the AppKey, every BLE connection uses a binary MTLS handshake:

- `B0` – HELLO (dongle → app), contains server ECDH public key + session ID.
- `B1` – KEYX (app → dongle), contains client ECDH public key + HMAC(AppKey, ...).
- `B2` – SFIN (dongle → app), confirms the session key derived from ECDH + AppKey via HKDF.

All subsequent traffic is carried in `B3` records:

- Context-specific IVs derived from the session key, session ID, direction and sequence number.
- Encrypted inner frame: `OP | LEN | PAYLOAD` (application command).
- HMAC-SHA256 (truncated) for integrity.
- Strict sequence number checks for replay protection.

On top of this, the application opcodes handle layout switching, info queries, resets and string sending as described in the v1.2.1 section above.

---
