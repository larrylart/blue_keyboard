////////////////////////////////////////////////////////////////////
// commands.h — binary protocol dispatcher + APPKEY onboarding
//
// Wire format: [OP][LENle][PAYLOAD]
//
// TX:
//   sendFrame() builds the frame and sends it either:
//     - RAW over BLE (pre-MTLS), or
//     - inside MTLS B3 (post-handshake)
//
// RX:
//   dispatch_binary_frame() consumes one framed message from BLE.
//   B1/B3 are handed to mtls.cpp (handshake / decrypt) and any decrypted
//   inner frame is re-dispatched here.
//
// Op groups:
//   A*: AppKey onboarding (A0/A2/A3)   (pre-MTLS)
//   C*/D*/E*: app commands             (require MTLS; E0 also needs rawFastMode)
//
// MTLS handshake/record layer lives in mtls.cpp.
// Larry Lart
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
#include "RawKeyboard.h"
#include "layout_kb_profiles.h"   // for KeyboardLayout, layoutName, m_nKeyboardLayout

extern RawKeyboard Keyboard;
extern bool g_rawFastMode;   // defined in blue_keyboard.ino

// TX and UI hooks implemented in blue_keyboard.ino / mtls.cpp

// Raw BLE TX (chunks to MTU-3 internally)
extern bool sendTX(const uint8_t* data, size_t len);

// UI helper: show LOCKED + blink
extern void showLockedNeedsReset();

// APPKEY response: send opaque wrapped AppKey back to app
extern bool sendWrappedAppKey(const uint8_t verif32[32], const uint8_t chal16[16]); // from mtls.cpp 

// MTLS helpers (mtls.cpp)
extern bool mtls_isActive();
extern bool mtls_wrapAndSendBytes_B3(const uint8_t* plain, size_t n);
extern bool mtls_tryConsumeOrDecryptFromBinary(uint8_t op, const uint8_t* p, uint16_t n, std::vector<uint8_t>& outPlain);

// UI feedback when a string has been typed on the host
extern void onStringTyped(size_t numBytes);

// APPKEY onboarding (A0/A2/A3) state (RAM only; not persisted)
static uint8_t g_appkey_chal[16];
static bool    g_appkey_chal_pending = false;
static uint16_t g_appkey_fail_count = 0;   // consecutive failed APPKEY proofs this boot
static const uint16_t APPKEY_FAIL_LIMIT = 100; // 100 - should be reasonable enough

// out32 = HMAC-SHA256(key, msg)
static bool hmac_sha256(const uint8_t* key, size_t keyLen,
                        const uint8_t* msg, size_t msgLen,
                        uint8_t out32[32])
{
	const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
	if( !md ) return( false );
	if( mbedtls_md_hmac(md, key, (int)keyLen, msg, msgLen, out32) != 0 ) return( false );
	
	return( true );
}

// Small binary helpers - no longer relevant?
static inline uint16_t rd16le(const uint8_t* p){ return( (uint16_t)p[0] | ((uint16_t)p[1]<<8) ); }
static inline void wr16le(uint8_t* p, uint16_t v){ p[0]=(uint8_t)(v&0xFF); p[1]=(uint8_t)(v>>8); }

// MD5(payload) for SEND_RESULT (D1). Used as a lightweight “what was typed” checksum.
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
// sendFrame(op,payload,n)
// Builds [OP][LENle][PAYLOAD] and sends:
//   - MTLS active: wraps as B3
//   - else: RAW over BLE
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
// SET_LAYOUT (C0): accepts "UK_WINLIN" or "LAYOUT_UK_WINLIN", updates + persists.
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
// handle_appkey_ops(op,p,n)
//
// A0: issue KDF params + random challenge (reply A2)
// A3: verify proof (HMAC over "APPKEY"||chal) and return wrapped AppKey
//
// Runs pre-MTLS. Returns true if consumed.
////////////////////////////////////////////////////////////////////
static bool handle_appkey_ops(uint8_t op, const uint8_t* p, uint16_t n)
{
    // Rate-limit APPKEY onboarding attempts: after too many failed proofs
    // in this boot, block further GET_APPKEY / APPKEY_PROOF handling.
    if( (op == 0xA0 || op == 0xA3) && g_appkey_fail_count >= APPKEY_FAIL_LIMIT ) 
    {
        const char* msg = "GET_APPKEY blocked";
        sendFrame(0xFF, (const uint8_t*)msg, (uint16_t)strlen(msg));
        return( true );
    }	
	
	// :: GET_APPKEY (0xA0) — request KDF params + challenge
	if( op == 0xA0 ) 
	{ 
		// Strict single-app / single-device mode:
		// If the AppKey was already provisioned AND both multi flags are disabled,
		// reject further provisioning attempts and require factory reset.
		bool allowMultiApp = getAllowMultiAppProvisioning();
		bool allowMultiDev = getAllowMultiDevicePairing();

		if( isAppKeyMarkedSet() && !allowMultiApp && !allowMultiDev )
		{
			const char* msg = "LOCKED_SINGLE_NEED_RESET";
			sendFrame(0xFF, (const uint8_t*)msg, (uint16_t)strlen(msg));
			return( true );
		}

		uint8_t salt16[16], verif32[32]; uint32_t iters=0;
		if( !loadPwKdf(salt16, verif32, &iters) ) 
		{
		  const char* msg = "KDF missing";
		  sendFrame(0xFF, (const uint8_t*)msg, (uint16_t)strlen(msg));
		  return( true );
		}

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
		 // Basic sanity: must have outstanding challenge and correct size
		if( !g_appkey_chal_pending || n != 32 ) 
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

		uint8_t expect[32];
		if( !hmac_sha256(verif32, 32, msgbuf, sizeof(msgbuf), expect) ) 
		{
			const char* e = "HMAC fail";
			sendFrame(0xFF, (const uint8_t*)e, (uint16_t)strlen(e));
			g_appkey_chal_pending = false;
			return( true );
		}

		if( memcmp(expect, p, 32) != 0 ) 
		{
			DPRINTLN("[APPKEY] proof BAD");
			g_appkey_fail_count++;
			const char* e = "bad proof";
			sendFrame(0xFF, (const uint8_t*)e, (uint16_t)strlen(e));
			g_appkey_chal_pending = false;
			return( true );
		}

		// OK - send wrapped appkey (opaque to passive sniffers)
		DPRINTLN("[APPKEY] proof OK - returning wrapped APPKEY");
		
		// verif32 and g_appkey_chal are in scope
		bool okWrap = sendWrappedAppKey(verif32, g_appkey_chal);
		// zero challenge and pending flag regardless
		memset(g_appkey_chal, 0, sizeof(g_appkey_chal));
		g_appkey_chal_pending = false;
		// reset failure counter on success
		g_appkey_fail_count = 0;  
		
		if( !okWrap ) 
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
// handle_mtls_ops(op,p,n)
//
// Requires mtls_isActive().
// C0: set layout
// C1: get info (reply C2)
// C4: clear AppKey/setup (factory-unlock)
// D0: type UTF-8 string (reply D1 = status + MD5(payload))
// C8: toggle raw fast mode
// E0: raw key tap (only when raw fast mode enabled; no ACK)
//
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

    // :: SET_RAW_FAST_MODE (0xC8)
    // payload: [mode1]
    //   mode1 = 0x00 - disable raw fast mode
    //   mode1 = 0x01 - enable raw fast mode
    if( op == 0xC8 )
    {
        if( n != 1 )
        {
            const char* e = "bad len";
            sendFrame( 0xFF, (const uint8_t*)e, (uint16_t)strlen(e) );
            return( true );
        }

        uint8_t mode = p[0];
        g_rawFastMode = (mode != 0);

        DPRINT("[RAW] fast_mode=%d\n", g_rawFastMode ? 1 : 0);

        // ACK_OK, no extra payload
        sendFrame(0x00, nullptr, 0);
        return( true );
    }

	// :: RAW_KEY_TAP (0xE0)
	// Fast-path: send a single HID usage (mods + usage), no MD5, no ACK.
	// Only honored when g_rawFastMode == true.
	// Payload:
	//    [mods1][usage1]            (len = 2)
	// or [mods1][usage1][repeat1]   (len = 3)
	if( op == 0xE0 )
	{
		// we only accept this if raw mode is enabled
		if( !g_rawFastMode )
		{
			// Raw mode not enabled - treat as error but still consume.
			const char* e = "raw off";
			sendFrame(0xFF, (const uint8_t*)e, (uint16_t)strlen(e));
			return true;
		}

		if( n < 2 )
		{
			const char* e = "bad len";
			sendFrame( 0xFF, (const uint8_t*)e, (uint16_t)strlen(e) );
			return true;
		}

		uint8_t mods   = p[0];
		uint8_t usage  = p[1];
		uint8_t repeat = (n >= 3 && p[2] != 0) ? p[2] : 1;

		// by layout type:
		//  - If a TV layout is selected (layout id >= BK_TV_LAYOUT_BASE), remap the
		//    standard consumer usage bytes to the TV's expected mapping.
		//  - Some TV mappings may require sending a *keyboard* usage (e.g. Samsung
		//    volume uses F8/F9/F10), which is supported via TvMediaRemap.
		if( mods == 0x00 && isTvLayout(m_nKeyboardLayout) && RawKeyboard::isConsumerUsage(usage) )
		{
			TvMediaRemap r = remapConsumerForTv(m_nKeyboardLayout, usage);
			for( uint8_t i = 0; i < repeat; ++i )
			{
				// If asKeyboard=true, r.usage is a keyboard HID usage (F8/F9/F10...).
				// If asKeyboard=false, r.usage is a consumer low byte (0xCD/0xB7/0xE9...).
				Keyboard.sendRaw(0x00, r.usage);
			}
		}
		else
		{
			// Normal raw keyboard usage path (also handles consumer usages automatically when mods==0)
			for( uint8_t i = 0; i < repeat; ++i )
			{
				Keyboard.sendRaw(mods, usage);
			}
		}

		// NOTE: no ACK (no sendFrame back), no MD5, no UI update.
		// Pure fire-and-forget for maximum throughput.
		return true;
	}
	
	// Not a known MTLS-protected opcode
	return( false );
}

////////////////////////////////////////////////////////////////////
// dispatch_binary_frame(buf,len)
//
// Consumes one framed message: [OP][LENle][PAYLOAD].
//
// - B1/B3: hand to mtls.cpp; if B3 decrypts an inner frame, re-dispatch it.
// - Pre-MTLS: only APPKEY ops (A0/A2/A3).
// - Post-MTLS: handle app ops (C*/D*/E*).
//
// Returns true if handled (including errors).
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
			if( inner.empty() ) return true;
			
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
	// Note: we process commands even if the were not encapsulated in B3, 
	// for max security all highly sensitive data needs to be encapsulated in B3
	if( handle_mtls_ops(op, p, L) ) return( true );

	// Unknown op at this point
	const char* e = "bad op";
	sendFrame( 0xFF, (const uint8_t*)e, (uint16_t)strlen(e) );
	
	return( true );
}

#endif // COMMANDS_H
