////////////////////////////////////////////////////////////////////
// commands.h  —  Binary command + APPKEY + MTLS glue
//
// This module sits on top of the raw BLE byte stream and:
//
//   • Defines the "framed command" format:
//        [OP][LENle][PAYLOAD]
//   • Provides sendFrame(...) which either:
//        - sends RAW over BLE (pre-MTLS), or
//        - wraps inside an MTLS B3 record (post-handshake)
//   • Handles APPKEY onboarding opcodes (0xA0, 0xA2, 0xA3)
//   • Handles MTLS-protected application opcodes (C*/D*):
//        - 0xC0 = SET_LAYOUT
//        - 0xC1 = GET_INFO (request)
//        - 0xC2 = INFO_VALUE (response)
//        - 0xC4 = RESET_TO_DEFAULT
//        - 0xD0 = SEND_STRING (UTF-8)
//        - 0xD1 = SEND_RESULT (MD5 of typed string)
//   • Exposes dispatch_binary_frame(...) which is called from the
//     BLE receive path with a single framed message.
//
// The MTLS handshake itself (B0/B1/B2/B3) is implemented in mtls.cpp.
// This file delegates to mtls_*() for KEYX/ENC handling when needed.
//
// Created by: Larry Lart
////////////////////////////////////////////////////////////////////
#ifndef COMMANDS_H
#define COMMANDS_H

#include <Arduino.h>
#include <vector>
#include <memory>
#include <string.h>   
#include <stddef.h>   
#include <mbedtls/md.h>

#include <mbedtls/pkcs5.h>
#include <mbedtls/version.h> 

/////////////////////////////
// *** DEBUG ***
#define DEBUG_ENABLED 1
#include "debug_utils.h"
/////////////////////////////

// locals
#include "settings.h"
#include "layout_kb_profiles.h"   // for KeyboardLayout, layoutName, m_nKeyboardLayout

extern RawKeyboard Keyboard;

// -------------------------------------------------------------------
// Low-level TX + MTLS primitives
// -------------------------------------------------------------------

// Raw TX function implemented in blue_keyboard.ino.
// Sends a single buffer over BLE (internally chunks to ATT_MTU as needed).
extern bool sendTX(const uint8_t* data, size_t len);

// Used by APPKEY onboarding to send the one-time wrapped AppKey
// back to the Android app (implemented in mtls.cpp)
extern bool sendWrappedAppKey(const uint8_t verif32[32], const uint8_t chal16[16]); // from mtls.cpp 

// MTLS session helpers (implemented in mtls.cpp).
//   - mtls_isActive()          : returns true once B0/B1/B2 completed
//   - mtls_wrapAndSendBytes_B3 : encrypt + MAC an app frame as B3 and send
//   - mtls_tryConsumeOrDecryptFromBinary : process inbound B1/B3 frames
extern bool mtls_isActive();
extern bool mtls_wrapAndSendBytes_B3(const uint8_t* plain, size_t n);
extern bool mtls_tryConsumeOrDecryptFromBinary(uint8_t op, const uint8_t* p, uint16_t n, std::vector<uint8_t>& outPlain);

// UI feedback when a string has been typed on the host
extern void onStringTyped(size_t numBytes);

// -------------------------------------------------------------------
// APPKEY challenge state
//
// Used only for the APPKEY onboarding opcodes (0xA0, 0xA2, 0xA3).
// The challenge is never written to NVS - stored in volatile/ram
// -------------------------------------------------------------------
static uint8_t g_appkey_chal[16];
static bool    g_appkey_chal_pending = false;

// -------------------------------------------------------------------
// HMAC-SHA256 helper
//
// Thin wrapper around mbedtls_md_hmac() to compute:
//    out32 = HMAC(key, msg)
// -------------------------------------------------------------------
static bool hmac_sha256(const uint8_t* key, size_t keyLen,
                        const uint8_t* msg, size_t msgLen,
                        uint8_t out32[32])
{
	const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
	if( !md ) return( false );
	if( mbedtls_md_hmac(md, key, (int)keyLen, msg, msgLen, out32) != 0 ) return( false );
	
	return( true );
}

// -------------------------------------------------------------------
// Small binary helpers
//
// rd16le: read uint16 from little-endian byte buffer
// wr16le: write uint16 to little-endian byte buffer (currently unused)
// -------------------------------------------------------------------
static inline uint16_t rd16le(const uint8_t* p){ return( (uint16_t)p[0] | ((uint16_t)p[1]<<8) ); }
static inline void wr16le(uint8_t* p, uint16_t v){ p[0]=(uint8_t)(v&0xFF); p[1]=(uint8_t)(v>>8); }

// -------------------------------------------------------------------
// MD5 helper
//
// md5_of(buf,n) → out16
// Used for the SEND_STRING (0xD0) → SEND_RESULT (0xD1) round-trip,
// where the app gets an MD5 over the exact typed payload.
// -------------------------------------------------------------------
static inline void md5_of( const uint8_t* buf, size_t n, uint8_t out16[16] )
{
	const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_MD5);
	mbedtls_md_context_t ctx; 
	mbedtls_md_init(&ctx);
	mbedtls_md_setup(&ctx, md, 0);
	mbedtls_md_starts(&ctx);
	mbedtls_md_update(&ctx, buf, n);
	unsigned char full[16];
	mbedtls_md_finish(&ctx, full);
	memcpy(out16, full, 16);
	mbedtls_md_free(&ctx);
}

////////////////////////////////////////////////////////////////////
// sendFrame(op, payload, n)
//
// Common framing helper used everywhere in this module:
//   - Builds [OP][LENle][PAYLOAD]
//   - If MTLS is active, wraps inside an encrypted B3 record.
//   - Otherwise sends RAW over BLE.
//
// This is the *only* function that should construct framed messages
// for the wire. All higher-level code just passes (op, payload,len).
////////////////////////////////////////////////////////////////////
static bool sendFrame( uint8_t op, const uint8_t* payload, uint16_t n )
{
	// Frame header: OP + length (LE)
	uint8_t hdr[3] = { op, (uint8_t)(n & 0xFF), (uint8_t)(n >> 8) };

	// If MTLS is active, wrap header+payload inside a B3 record
	if( mtls_isActive() )
	{
		// MTLS: still send as one plaintext blob inside the encrypted record
		std::vector<uint8_t> blob;
		blob.reserve(3 + n);
		blob.insert(blob.end(), hdr, hdr + 3);
		if (n) blob.insert(blob.end(), payload, payload + n);

		DPRINT("[TX][MTLS] op=0x%02X len=%u\n", op, (unsigned)n);
		
		return( mtls_wrapAndSendBytes_B3(blob.data(), blob.size()) );
	
	} else
	{
		// RAW: **send header+payload in ONE notify**
		// sendTX() already chunks to (ATT_MTU-3) if needed, so just pass one buffer.
		std::vector<uint8_t> one;
		one.reserve(3 + n);
		one.insert(one.end(), hdr, hdr + 3);
		if( n ) one.insert(one.end(), payload, payload + n);

		DPRINT("[TX][RAW ] op=0x%02X len=%u (coalesced)\n", op, (unsigned)n);
		
		return( sendTX(one.data(), (uint16_t)one.size()) );
	}
}

// Convenience wrapper for sending C-strings via sendTX(...)
static inline bool sendTX( const char* s ) 
{
	return( sendTX(reinterpret_cast<const uint8_t*>(s), ::strlen(s)) );
}

////////////////////////////////////////////////////////////////////
// setLayoutByName()
//
// Helper for opcode 0xC0 (SET_LAYOUT).
// Accepts either:
//   - "UK_WINLIN"
//   - "LAYOUT_UK_WINLIN"
//
// On success:
//   - updates m_nKeyboardLayout
//   - persists to NVS via saveLayoutToNVS()
////////////////////////////////////////////////////////////////////
static inline bool setLayoutByName(const String& raw) 
{
	String name = raw; name.trim();
	String full = name.startsWith("LAYOUT_") ? name : ("LAYOUT_" + name);

	if(full == "LAYOUT_UK_WINLIN") m_nKeyboardLayout = KeyboardLayout::UK_WINLIN;
	else if(full == "LAYOUT_IE_WINLIN") m_nKeyboardLayout = KeyboardLayout::IE_WINLIN;
	else if(full == "LAYOUT_US_WINLIN") m_nKeyboardLayout = KeyboardLayout::US_WINLIN;
	else if(full == "LAYOUT_UK_MAC") m_nKeyboardLayout = KeyboardLayout::UK_MAC;
	else if(full == "LAYOUT_IE_MAC") m_nKeyboardLayout = KeyboardLayout::IE_MAC;
	else if(full == "LAYOUT_US_MAC") m_nKeyboardLayout = KeyboardLayout::US_MAC;
	else if(full == "LAYOUT_DE_WINLIN") m_nKeyboardLayout = KeyboardLayout::DE_WINLIN;
	else if(full == "LAYOUT_DE_MAC") m_nKeyboardLayout = KeyboardLayout::DE_MAC;
	else if(full == "LAYOUT_FR_WINLIN") m_nKeyboardLayout = KeyboardLayout::FR_WINLIN;
	else if(full == "LAYOUT_FR_MAC") m_nKeyboardLayout = KeyboardLayout::FR_MAC;
	else if(full == "LAYOUT_ES_WINLIN") m_nKeyboardLayout = KeyboardLayout::ES_WINLIN;
	else if(full == "LAYOUT_ES_MAC") m_nKeyboardLayout = KeyboardLayout::ES_MAC;
	else if(full == "LAYOUT_IT_WINLIN") m_nKeyboardLayout = KeyboardLayout::IT_WINLIN;
	else if(full == "LAYOUT_IT_MAC") m_nKeyboardLayout = KeyboardLayout::IT_MAC;
	else if(full == "LAYOUT_PT_PT_WINLIN") m_nKeyboardLayout = KeyboardLayout::PT_PT_WINLIN;
	else if(full == "LAYOUT_PT_PT_MAC") m_nKeyboardLayout = KeyboardLayout::PT_PT_MAC;
	else if(full == "LAYOUT_PT_BR_WINLIN") m_nKeyboardLayout = KeyboardLayout::PT_BR_WINLIN;
	else if(full == "LAYOUT_PT_BR_MAC") m_nKeyboardLayout = KeyboardLayout::PT_BR_MAC;
	else if(full == "LAYOUT_SE_WINLIN") m_nKeyboardLayout = KeyboardLayout::SE_WINLIN;
	else if(full == "LAYOUT_NO_WINLIN") m_nKeyboardLayout = KeyboardLayout::NO_WINLIN;
	else if(full == "LAYOUT_DK_WINLIN") m_nKeyboardLayout = KeyboardLayout::DK_WINLIN;
	else if(full == "LAYOUT_FI_WINLIN") m_nKeyboardLayout = KeyboardLayout::FI_WINLIN;
	else if(full == "LAYOUT_CH_DE_WINLIN") m_nKeyboardLayout = KeyboardLayout::CH_DE_WINLIN;
	else if(full == "LAYOUT_CH_FR_WINLIN") m_nKeyboardLayout = KeyboardLayout::CH_FR_WINLIN;
	else if(full == "LAYOUT_TR_WINLIN") m_nKeyboardLayout = KeyboardLayout::TR_WINLIN;
	else if(full == "LAYOUT_TR_MAC") m_nKeyboardLayout = KeyboardLayout::TR_MAC;  
	else return false;

	saveLayoutToNVS(m_nKeyboardLayout);
	return( true );
}

////////////////////////////////////////////////////////////////////
// handle_appkey_ops()
//
// Handles APPKEY onboarding opcodes:
//
//   0xA0 = GET_APPKEY_REQUEST
//          → checks if AppKey already set, and if not:
//              - loads KDF params from NVS (salt/verif/iters)
//              - generates random challenge
//              - replies with A2: [salt16 | iters4 | chal16]
//              - records chal in g_appkey_chal + sets g_appkey_chal_pending
//
//   0xA3 = APPKEY_PROOF
//          → app sends MAC32 = HMAC(verif32, "APPKEY"||chal16)
//              - verifies chal is pending and payload size == 32
//              - recomputes expected MAC using stored verif32+chal16
//              - if OK, calls sendWrappedAppKey() to send opaque A1:
//                    cipher(AppKey) + MAC
//              - clears chal + pending flag
//
// Any other opcode returns false so caller can handle it.
// All APPKEY traffic is pre-MTLS (we do not require mtls_isActive()).
////////////////////////////////////////////////////////////////////
static bool handle_appkey_ops(uint8_t op, const uint8_t* p, uint16_t n)
{
	// :: GET_APPKEY (0xA0) — request KDF params + challenge
	if( op == 0xA0 ) 
	{ 
		if (isAppKeyMarkedSet())
		{
			const char* msg = "already set";
			sendFrame(0xFF, (const uint8_t*)msg, (uint16_t)strlen(msg));
			return( true );
		}

		uint8_t salt16[16], verif32[32]; uint32_t iters=0;
		if (!loadPwKdf(salt16, verif32, &iters)) 
		{
		  const char* msg = "KDF missing";
		  sendFrame(0xFF, (const uint8_t*)msg, (uint16_t)strlen(msg));
		  return( true );
		}

		// DEBUG: show KDF params from NVS
//		DPRINT("[APPKEY][A0] salt=");
//		for (int i=0;i<16;i++) DPRINT("%02x", (unsigned)salt16[i]);
//		DPRINT(" iters=%u verif32[0..7]=", (unsigned)iters);
//		for (int i=0;i<8;i++) DPRINT("%02x", (unsigned)verif32[i]);
//		DPRINT("\n");


		for( int i=0; i<16; i++ ) g_appkey_chal[i] = (uint8_t)esp_random();
		g_appkey_chal_pending = true;

		uint8_t pay[16 + 4 + 16];
		memcpy(pay, salt16, 16);
		pay[16] = (uint8_t)(iters & 0xFF);
		pay[17] = (uint8_t)((iters >> 8) & 0xFF);
		pay[18] = (uint8_t)((iters >> 16) & 0xFF);
		pay[19] = (uint8_t)((iters >> 24) & 0xFF);
		memcpy(pay + 20, g_appkey_chal, 16);

		DPRINT("[APPKEY] chal issued: iters=%u\n", (unsigned)iters);
		sendFrame( 0xA2, pay, sizeof(pay) ); // APPKEY_CHALLENGE
		
		return( true );
	}

	// :: APPKEY_PROOF (0xA3) — client proves knowledge of setup password
	//      payload = MAC32 = HMAC(verif32, "APPKEY"||chal16)
	if( op == 0xA3 )
	{
		/*// debug: show first 8 of incoming MAC
		DPRINT("[APPKEY] got A3 mac32[0..8]=");
		for( int i=0; i<8 && i<n; i++ ) DPRINT("%02x", p[i]);
		DPRINT("\n");*/

		 // Basic sanity: must have outstanding challenge and correct size
		if (!g_appkey_chal_pending || n != 32) 
		{
			const char* msg = "no pending chal or bad mac size";
			sendFrame(0xFF, (const uint8_t*)msg, (uint16_t)strlen(msg));
			g_appkey_chal_pending = false;
			
			return( true );
		}

		uint8_t salt16[16], verif32[32]; uint32_t iters = 0;
		if( !loadPwKdf(salt16, verif32, &iters) ) 
		{
			const char* msg = "KDF missing";
			sendFrame(0xFF, (const uint8_t*)msg, (uint16_t)strlen(msg));
			g_appkey_chal_pending = false;
			return( true );
		}

		// Build message once (no duplicate declarations)
		uint8_t msgbuf[6 + 16];
		memcpy(msgbuf, "APPKEY", 6);
		memcpy(msgbuf + 6, g_appkey_chal, 16);

		/* DEBUG
		// ---------- BEGIN VERY VERBOSE DEBUG ----------
		DPRINT("[APPKEY][A3] chal16=");
		for (int i=0; i<16; i++) DPRINT("%02x", (unsigned)g_appkey_chal[i]); DPRINT("\n");

		DPRINT("[APPKEY][A3] msg(APPKEY||chal)[0..21]=");
		for (int i=0; i<22; i++) DPRINT("%02x", (unsigned)msgbuf[i]); DPRINT("\n");

		DPRINT("[APPKEY][A3] verif32(NVS)[0..7]=");
		for (int i=0; i<8; i++) DPRINT("%02x", (unsigned)verif32[i]); DPRINT("\n");
		// ---------- END VERY VERBOSE DEBUG ---------- */

		uint8_t expect[32];
		if( !hmac_sha256(verif32, 32, msgbuf, sizeof(msgbuf), expect) ) 
		{
			const char* e = "HMAC fail";
			sendFrame(0xFF, (const uint8_t*)e, (uint16_t)strlen(e));
			g_appkey_chal_pending = false;
			return( true );
		}

		/* DEBUG
		// More verbose
		DPRINT("[APPKEY][A3] expect[0..7]=");
		for (int i=0; i<8; i++) DPRINT("%02x", (unsigned)expect[i]); DPRINT("\n");
		DPRINT("[APPKEY][A3] proof [0..7]=");
		for (int i=0; i<8; i++) DPRINT("%02x", (unsigned)p[i]); DPRINT("\n");
		*/

		if (memcmp(expect, p, 32) != 0) 
		{
			DPRINTLN("[APPKEY] proof BAD");
			const char* e = "bad proof";
			sendFrame(0xFF, (const uint8_t*)e, (uint16_t)strlen(e));
			g_appkey_chal_pending = false;
			return( true );
		}

		// OK - send wrapped appkey (opaque to passive sniffers)
		DPRINTLN("[APPKEY] proof OK → returning wrapped APPKEY");
		
		// verif32 and g_appkey_chal are in scope
		bool okWrap = sendWrappedAppKey(verif32, g_appkey_chal);
		// zero challenge and pending flag regardless
		memset(g_appkey_chal, 0, sizeof(g_appkey_chal));
		g_appkey_chal_pending = false;
		if (!okWrap) 
		{
			const char* e = "send fail";
			sendFrame(0xFF, (const uint8_t*)e, (uint16_t)strlen(e));
		}
		
		return( true );	  
	}

	// Not an APPKEY opcode
	return( false ); 
}

////////////////////////////////////////////////////////////////////
// handle_mtls_ops()
//
// Handles application opcodes that MUST be run under MTLS,
// i.e. only when mtls_isActive() == true.
//
//   0xC0 = SET_LAYOUT
//          payload: ASCII "UK_WINLIN" or "LAYOUT_UK_WINLIN"
//
//   0xC1 = GET_INFO (request)
//          payload: empty
//          → replies 0xC2 with:
//              "LAYOUT=<SHORT>; PROTO=<ver>; FW=<ver>"
//
//   0xC4 = RESET_TO_DEFAULT
//          clears AppKey and setup flag in NVS
//
//   0xD0 = SEND_STRING
//          payload: UTF-8 text to type as keystrokes
//          → types via sendUnicodeAware()
//          → replies 0xD1 with:
//              [0x00 | md5(payload)]
//
// Any unrecognized opcode returns false (caller will handle).
////////////////////////////////////////////////////////////////////
static bool handle_mtls_ops( uint8_t op, const uint8_t* p, uint16_t n )
{
	// :: SET_LAYOUT (0xC0)
	if( op == 0xC0 ) 
	{ 
        // Payload is an ASCII name, not necessarily NUL-terminated.
		// We copy into a temporary buffer and trim, then delegate to setLayoutByName().
		std::unique_ptr<char[]> tmp(new char[n+1]);
		memcpy(tmp.get(), p, n); tmp[n] = 0;

		String name = String(tmp.get());
		name.trim();

		bool ok = setLayoutByName(name);
		DPRINT("[LAYOUT] set by name '%s' -> %d\n", name.c_str(), ok ? 1 : 0);
		
		if( ok ) 
		{
			// ACK_OK
			sendFrame(0x00, nullptr, 0);
			
		} else 
		{
			const char* e = "bad layout";
			sendFrame(0xFF, (const uint8_t*)e, (uint16_t)strlen(e));
		}
		return( true );
	}

	// :: GET_INFO (0xC1)
	// Replies with 0xC2 = INFO_VALUE containing a short ASCII summary:
	// "LAYOUT=<SHORT>; PROTO=<PROTO_VER>; FW=<FW_VER>"
	if( op == 0xC1 )
	{ 
		// Build "LAYOUT=UK_WINLIN; PROTO=1.2; FW=1.1.1" as ASCII payload
		String s = "LAYOUT=";

        // layoutName(...) returns strings like "LAYOUT_UK_WINLIN";
        // strip the "LAYOUT_" prefix if present.}
		const char* full = layoutName(m_nKeyboardLayout); 
		const char* shortName = (strncmp(full, "LAYOUT_", 7)==0) ? (full+7) : full;
		s += shortName;
		s += "; PROTO=" PROTO_VER "; FW=" FW_VER;

		// Send inside MTLS using standard frame: [0xC2][LEN][BYTES]
		sendFrame(0xC2, reinterpret_cast<const uint8_t*>(s.c_str()), (uint16_t)s.length());
		return( true );
	}
  
    // :: RESET_TO_DEFAULT (0xC4)
	if( op == 0xC4 ) 
	{ 
		DPRINTLN("[RESET] clear appkey+setup");
		clearAppKeyAndFlag();
		sendFrame(0x00, nullptr, 0); // ACK_OK
		return( true );
	}
	
    // :: SEND_STRING (0xD0)
    // Types arbitrary UTF-8 text received from the app.
	if( op == 0xD0 ) 
	{ 
		// Make a NUL-terminated copy for sendUnicodeAware(...)
		std::unique_ptr<char[]> tmp(new char[n+1]);
		memcpy(tmp.get(), p, n); tmp[n]=0;

		// Type it via RawKeyboard + layout-aware helper
		sendUnicodeAware(Keyboard, tmp.get());

		// UI feedback - string was typed
		onStringTyped( n );

		 // Compute MD5 over the exact payload bytes, no trimming.
		uint8_t md5[16]; 
		md5_of(p, n, md5);

		uint8_t out[1+16]; 
		out[0]=0; 							// status = 0 (OK)
		memcpy(out+1, md5, 16);
		sendFrame(0xD1, out, sizeof(out)); // SEND_RESULT
		
		return( true );
	}
	
	// Not a known MTLS-protected opcode
	return( false );
}

////////////////////////////////////////////////////////////////////
// dispatch_binary_frame()
//
// Top-level binary dispatcher for ONE incoming frame from BLE.
//
// Input buffer has the form:
//   [OP][LENle][PAYLOAD]
//
// Dispatch strategy:
//
//   1) If OP is B1 or B3:
//        - call mtls_tryConsumeOrDecryptFromBinary()
//        - if it returns true and produces an inner frame (B3 case),
//          recursively feed that inner frame back here.
//        - this is how encrypted app frames get re-dispatched.
//
//   2) If MTLS is NOT active:
//        - allow only APPKEY onboarding opcodes via handle_appkey_ops()
//        - any other opcode → error "need MTLS"
//
//   3) If MTLS IS active:
//        - handle MTLS-protected app opcodes via handle_mtls_ops()
//        - anything else → "bad op"
//
// Returns true if the frame was consumed (handled or errored).
// Returns false only on obvious framing errors (too short, length mismatch).
////////////////////////////////////////////////////////////////////
static bool dispatch_binary_frame( const uint8_t* buf, size_t len )
{
	if( len < 3 ) return( false );
	uint8_t  op = buf[0];
	uint16_t L  = rd16le(buf+1);
	if( len < 3 + L ) return( false );

	const uint8_t* p = buf + 3;
	DPRINT("[RX][%s] op=0x%02X len=%u\n", mtls_isActive() ? "MTLS":"RAW", op, (unsigned)L);

	// 1) Pre-handle MTLS frames B1 (KEYX) and B3 (ENC)
	if( op == 0xB1 || op == 0xB3 ) 
	{
		DPRINT("[DISPATCH] entering B* pre-handler: op=0x%02X len=%u\n", (unsigned)op, (unsigned)L);
		std::vector<uint8_t> inner;
		if( mtls_tryConsumeOrDecryptFromBinary(op, p, L, inner) ) 
		{
			DPRINT("[DISPATCH] B* handler returned TRUE (consumed=%s)\n", inner.empty()?"yes":"no");
			
			 // B1: handshake-only, no inner frame
			if (inner.empty()) return true;
			
			// B3: inner now holds decrypted app frame [OP|LEN|PAYLOAD]
			return( dispatch_binary_frame(inner.data(), inner.size()) );
		}
		// If mtls_* did not consume, fall through and treat it as a normal op
		DPRINTLN("[DISPATCH] B* handler returned FALSE — falling through");
	}

	// 2) APPKEY onboarding is allowed only pre-MTLS
	if( !mtls_isActive() ) 
	{
		if( handle_appkey_ops(op, p, L) ) return( true );
		
		// Anything else before MTLS is considered an error
		const char* e = "need MTLS";
		sendFrame( 0xFF, (const uint8_t*)e, (uint16_t)strlen(e) );
		
		return( true );
	}

	// 3) MTLS-protected application ops (layout, info, reset, typing)
	if( handle_mtls_ops(op, p, L) ) return( true );

	// Unknown op at this point
	const char* e = "bad op";
	sendFrame( 0xFF, (const uint8_t*)e, (uint16_t)strlen(e) );
	
	return( true );
}

#endif // COMMANDS_H
