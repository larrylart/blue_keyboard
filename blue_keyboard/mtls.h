////////////////////////////////////////////////////////////////////
//  MTLS — "Micro-TLS v1"
//  -----------------------------------
//  This module implements an application-level secure channel on
//  top of the BLE byte stream used by the BlueKeyboard dongle.
//
//  The scope:
//    * Mutual authentication using the long-term AppKey (32-byte PSK)
//    * Forward secrecy via ephemeral ECDH (P-256)
//    * Encrypted + MACed application frames
//    * Replay protection using per-session sequence counters
//    * Clean separation from BLE pairing/link security
//
//  Handshake overview:
//    B0 (server → client):
//       srvPub65 (P-256 uncompressed) || sid4 (session id, LE)
//
//    B1 (client → server):
//       cliPub65 || mac16
//       mac16 = HMAC(AppKey,
//                   "KEYX"||sid||srvPub65||cliPub65)[0..15]
//
//    → Server verifies MAC, computes ECDH shared secret,
//      derives sessKey32 = HKDF(AppKey, sharedSecret,
//                               "MT1"||sid||srvPub65||cliPub65)
//
//    B2 (server → client):
//       mac16 = HMAC(sessKey,
//                   "SFIN"||sid||srvPub65||cliPub65)[0..15]
//       (session becomes active)
//
//  Encrypted record format (B3):
//       seq2 || clen2 || cipher[clen] || mac16
//
//    cipher    = AES-CTR(sessKey, IV, plaintext)
//    IV        = HMAC(sessKey, "IV"||sid||dir||seq)[0..15]
//    mac16     = HMAC(sessKey,
//                     "ENCM"||sid||dir||seq||cipher)[0..15]
//    dir       = 'C' (client→dongle) or 'S' (dongle→client)
//
//  Once MTLS is active:
//    • All outbound application frames MUST be wrapped using B3
//    • All inbound B3 records must be passed through the decrypt
//      function before being dispatched to command handlers
//
//  The caller drives the top-level pipe:
//
//     - On connect: call mtls_sendHello_B0()
//     - On notifications from the app: call mtls_tryConsumeOrDecryptFromBinary()
//     - When sending application frames: call mtls_wrapAndSendBytes_B3()
//     - In loop(): call mtls_tick() (in .cpp) for timeout/retry
//
////////////////////////////////////////////////////////////////////
#pragma once
#include <Arduino.h>
#include <string>
#include <vector>


// -----------------------------------------------------------------
// Session state helpers
// -----------------------------------------------------------------

// Returns true once the B0/B1/B2 handshake has completed and a
// session key is available. All encrypted B3 sends depend on this.
bool mtls_isActive();

// Immediately clears all MTLS session state:
//   - s_active = false
//   - zeroes session key
//   - resets inbound/outbound sequence counters
//   - clears sid
// Call this on disconnect or before sending a new B0.
void mtls_reset();


// -----------------------------------------------------------------
// Handshake entry point (server → client)
// -----------------------------------------------------------------

// Builds and sends the B0 "HELLO" frame:
//     payload = srvPub65 || sid4 (LE)
// The function:
//   - checks that AppKey exists in NVS
//   - generates a fresh session id (sid)
//   - generates a fresh ephemeral P-256 keypair
//   - caches B0 for retransmission (handled inside mtls.cpp)
//   - sends the frame using sendFrame()
//
// Returns false only if AppKey missing or elliptic-curve keygen fails.
bool mtls_sendHello_B0();


// -----------------------------------------------------------------
// Frame ingestion (client → server)
// -----------------------------------------------------------------

// Called by the binary frame dispatcher.
//
// Attempts to consume or decrypt a single top-level MTLS frame:
//   B1  → KEYX: verify MAC(AppKey), run ECDH, derive sessKey32,
//               send B2 (SFIN), mark session active.
//
//   B3  → encrypted record: verify MAC(sessKey), check sequence,
//               AES-CTR decrypt cipher → outPlain, increment seqIn.
//               outPlain contains the inner application frame
//               (OP|LEN|payload). Caller will re-dispatch it.
//
// Any non-MTLS opcode returns false so the caller can process it.
//
// Returns true if the frame was handled (whether successful or not).
bool mtls_tryConsumeOrDecryptFromBinary(uint8_t op,
                                        const uint8_t* p,
                                        uint16_t n,
                                        std::vector<uint8_t>& outPlain);


// -----------------------------------------------------------------
// Encrypted send (server → client)
// -----------------------------------------------------------------

// Encrypts and sends a plaintext application frame as a B3 record.
//
// The input plaintext is always the standard app frame structure:
//     [OP][LENle][PAYLOAD]
//
// This function:
//   - selects direction='S' (server→client)
//   - derives IV using HMAC(sessKey,"IV"||sid||'S'||seq)
//   - AES-CTR encrypts the entire structure
//   - computes MAC = HMAC(sessKey,
//                        "ENCM"||sid||'S'||seq||cipher)[0..15]
//   - builds full B3 record and sends it via sendTX()
//
// seqOut is automatically incremented on success.
//
// Returns false if called before handshake completes.
bool mtls_wrapAndSendBytes_B3(const uint8_t* plain, size_t n);

