////////////////////////////////////////////////////////////////////
// mtls.cpp  —  "micro-TLS v1" for the BlueKeyboard dongle
//
// This module implements an application-level secure channel on top
// of the BLE byte stream. The protocol is binary and uses these
// top-level opcodes:
//
//   B0  = server → client  : HELLO
//         payload = srvPub65 (P-256, uncompressed) || sid4 (LE)
//
//   B1  = client → server  : KEYX
//         payload = cliPub65 || mac16
//         mac16 = HMAC(AppKey32, "KEYX"||sid4||srvPub65||cliPub65)[0..15]
//
//   B2  = server → client  : SFIN (server finished)
//         payload = mac16
//         mac16 = HMAC(sessKey32, "SFIN"||sid4||srvPub65||cliPub65)[0..15]
//
//   B3  = encrypted record (both directions, once MTLS is active)
//         payload = seq2 || clen2 || cipher[clen] || mac16
//         iv16   = HMAC(sessKey32, "IV"||sid4||dir||seq)[0..15]
//         mac16  = HMAC(sessKey32, "ENCM"||sid4||dir||seq||cipher)[0..15]
//         dir    = 'C' for client→dongle, 'S' for dongle→client
//
// Session key derivation:
//
//   1.  Z = ECDH(srvPriv, cliPub)    // P-256 shared secret (32 bytes)
//   2.  info = "MT1" || sid4 || srvPub65 || cliPub65
//   3.  sessKey32 = HKDF-SHA256(salt=AppKey32, ikm=Z, info)
//
// So the AppKey from NVS acts as the long-term PSK, and each ECDH
// handshake gives a fresh sessKey32 with forward secrecy.
//
// Once s_active==true, all outbound application frames are wrapped
// via mtls_wrapAndSendBytes_B3() and all inbound B3 records are
// verified/decrypted via mtls_tryConsumeOrDecryptFromBinary().
//
// Created by: Larry Lart
////////////////////////////////////////////////////////////////////
#include "mtls.h"
#include "settings.h"          // getAppKey(), isAppKeyMarkedSet(), markAppKeySet()
#include "commands.h"          // sendFrame(...) signature
#include <mbedtls/ecp.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/md.h>
#include <mbedtls/aes.h>
#include <mbedtls/sha256.h>

// debug
#define DEBUG_ENABLED 1
#include "debug_utils.h"

////////////////////////////////////////////////////////////////////
// B0 retry state
//
// When we send B0 we cache the payload so we can retry it a few times
// if the app doesn't respond with B1. This helps with flaky BLE links
// where the first HELLO notify might get lost.
////////////////////////////////////////////////////////////////////
static std::vector<uint8_t> s_lastB0;     // cached B0 payload = [srvPub65|sid4]
static uint32_t s_b0NextAtMs = 0;         // next scheduled retry time (millis)
static uint8_t  s_b0Retries  = 0;         // how many B0 retries we've done

////////////////////////////////////////////////////////////////////
// Session state
//
//  s_active   : true once B0/B1/B2 handshake has completed
//  s_sessKey  : 32-byte session key derived via HKDF(AppKey, ECDH)
//  s_seqIn    : expected next inbound sequence (for replay protection)
//  s_seqOut   : next outbound sequence to use
//  s_sid      : 32-bit session ID chosen by the dongle (in B0)
////////////////////////////////////////////////////////////////////
static bool     s_active      = false;
static uint8_t  s_sessKey[32] = {0};
static uint16_t s_seqIn  = 0;
static uint16_t s_seqOut = 0;
static uint32_t s_sid    = 0;

// P-256 ECDH server keys: static to the process.
// We re-generate the ephemeral keypair on each B0.
static mbedtls_ecp_group s_grp;
static mbedtls_mpi       s_priv;        // d
static mbedtls_ecp_point s_pub;         // Q


// Link security (provided by .ino)
//extern bool g_linkEncrypted, g_linkAuthenticated;
//bool isLinkSecure(){ return g_linkEncrypted && g_linkAuthenticated; } - no longer needed - to check if logic stands

extern bool sendTX(const uint8_t* data, size_t len);     // from .ino

////////////////////////////////////////////////////////////////////
//  sendWrappedAppKey(verif32, chal16)  — see bottom of file.
//
//  Used by the APPKEY onboarding flow (opcodes A0/A2/A3/A1).
//  Here we only need getAppKey() from settings.h.
//  This comment is intentionally repeated here so a reader
//  scrolling from the top knows why getAppKey() is needed.
//
////////////////////////////////////////////////////////////////////
extern const uint8_t* getAppKey(); // from settings.h

////////////////////////////////////////////////////////////////////
// Small local crypto helpers
//
//  - hx/toHex: debugging helpers for hex logging
//  - esp_mbedtls_rng: RNG adapter for mbedTLS (uses esp_random())
//  - hmac: thin wrapper around mbedtls_md_hmac(SHA256)
//  - hkdf_sha256: single-block HKDF implementation used for sessKey32
////////////////////////////////////////////////////////////////////
static inline char hx(uint8_t v){ return (v<10)?('0'+v):('a'+v-10); }
static void toHex(const uint8_t* in, size_t n, String& out)
{
	out.reserve(out.length()+n*2); 
	for( size_t i=0; i<n; ++i )
	{
		out+=hx(in[i]>>4); 
		out+=hx(in[i]&0xF); 
	}
}

static int esp_mbedtls_rng(void*, unsigned char* buf, size_t len)
{
	for( size_t i=0;i<len;i+=4 )
	{
		uint32_t r=esp_random(); 
		size_t k=((len-i)>=4)?4:(len-i); 
		memcpy(buf+i,&r,k); 
	}
	
	return 0;
}

static void hmac(const uint8_t* key, size_t klen, const uint8_t* msg, size_t mlen, uint8_t out[32])
{
	const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
	mbedtls_md_hmac(md, key, klen, msg, mlen, out);
}

////////////////////////////////////////////////////////////////////
// HKDF-SHA256 with a single 32-byte output block.
// - salt = "AppKey32" in our case
// - ikm  = ECDH shared secret
// - info = "MT1"||sid||srvPub||cliPub transcript
////////////////////////////////////////////////////////////////////
static void hkdf_sha256(const uint8_t* salt, size_t slen, const uint8_t* ikm, size_t ikmlen,
                        const uint8_t* info, size_t ilen, uint8_t out32[32])
{
	uint8_t prk[32];
	hmac(salt, slen, ikm, ikmlen, prk);
	uint8_t T[32]; // single block OKM
	const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
	mbedtls_md_context_t ctx; 
	mbedtls_md_init(&ctx);
	mbedtls_md_setup(&ctx, md, 1);
	mbedtls_md_hmac_starts(&ctx, prk, 32);
	mbedtls_md_hmac_update(&ctx, info, ilen);
	const uint8_t c = 0x01;
	mbedtls_md_hmac_update(&ctx, &c, 1);
	mbedtls_md_hmac_finish(&ctx, out32);
	mbedtls_md_free(&ctx);
}

// Public API: check if an MTLS session is currently active.
////////////////////////////////////////////////////////////////////
bool mtls_isActive(){ return s_active; }

// Public API: wipe all session state (called from B0 and optionally on disconnect).
////////////////////////////////////////////////////////////////////
void mtls_reset()
{
	s_active = false; 
	s_seqIn = s_seqOut = 0; 
	s_sid = 0;
	memset(s_sessKey, 0, sizeof(s_sessKey));
}

////////////////////////////////////////////////////////////////////
// Ensure the P-256 group and key objects are initialized once.
// We keep the group and key structs around for the lifetime of the process.
////////////////////////////////////////////////////////////////////
static void ensureGroupInit()
{
	static bool inited=false; 
	if(inited) return;
	mbedtls_ecp_group_init(&s_grp); 
	mbedtls_mpi_init(&s_priv); 
	mbedtls_ecp_point_init(&s_pub);
	int rc = mbedtls_ecp_group_load(&s_grp, MBEDTLS_ECP_DP_SECP256R1);
	
	/* DEBUG
	if( rc!=0 )
	{ 
		DPRINT("[MTLS] group_load failed rc=%d\n", rc); 
	} else 
	{ 
		DPRINTLN("[MTLS] group loaded: SECP256R1"); 
	}*/
	
	inited=true;
}

////////////////////////////////////////////////////////////////////
// Generate a fresh ephemeral keypair on P-256.
//  - writes private to s_priv, public to s_pub
//  - returns public key as 65-byte uncompressed hex string in pubHex (for debug)
////////////////////////////////////////////////////////////////////
static bool genKeypair( String& pubHex )
{
	ensureGroupInit();
	int rc = mbedtls_ecp_gen_keypair(&s_grp, &s_priv, &s_pub, esp_mbedtls_rng, nullptr);
	if( rc!=0 )
	{ 
		DPRINT("[MTLS] gen_keypair failed rc=%d\n", rc); 
		return( false );  
	}

	uint8_t uncompressed[65]; 
	size_t olen = 0;
	rc = mbedtls_ecp_point_write_binary(&s_grp, &s_pub, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, uncompressed, sizeof(uncompressed));
	if( rc!=0 || olen!=65 )
	{
		DPRINT("[MTLS] point_write_binary rc=%d olen=%u (expect 65)\n", rc, (unsigned)olen);
		return( false );
	}
	
	/* DEBUG
	pubHex=""; 
	toHex(uncompressed,65,pubHex);
	DPRINT("[MTLS] pubQ ok, hexLen=%u\n", (unsigned)pubHex.length());
	*/
	
	return( true );
}

////////////////////////////////////////////////////////////////////
// Derive session key from client's public key:
//
//  1. Parse cliPub65 as an uncompressed P-256 point.
//  2. Z = ECDH(s_priv, cliPub)
//  3. info = "MT1" || sid || srvPub65 || cliPub65
//  4. sessKey32 = HKDF(AppKey32, Z, info)
//
// The resulting sessKey32 is stored in s_sessKey.
////////////////////////////////////////////////////////////////////
static bool deriveSessionKey( const std::vector<uint8_t>& cliPub65 )
{
	if( cliPub65.size()!=65 || cliPub65[0]!=0x04 ) 
	{
		DPRINTLN("[MTLS] DERIVE: bad cli len/lead"); 
		return( false ); 
	}

	// load client point
	mbedtls_ecp_point Qc; mbedtls_ecp_point_init(&Qc);
	int rc = mbedtls_ecp_point_read_binary(&s_grp, &Qc, cliPub65.data(), cliPub65.size());
	if( rc != 0 ) 
	{ 
		DPRINT("[MTLS] DERIVE: read_binary rc=%d\n", rc); 
		mbedtls_ecp_point_free(&Qc); 
		return false; 
	}

	// shared secret Z = d * Qc
	mbedtls_mpi Z; 
	mbedtls_mpi_init(&Z);
	rc = mbedtls_ecdh_compute_shared(&s_grp, &Z, &Qc, &s_priv, esp_mbedtls_rng, nullptr);
	if( rc!=0 )
	{ 
		DPRINT("[MTLS] DERIVE: ecdh_shared rc=%d\n", rc); 
		mbedtls_mpi_free(&Z); 
		mbedtls_ecp_point_free(&Qc); 
		return false; 
	}	

	uint8_t shared[32]={0};
	mbedtls_mpi_write_binary(&Z, shared, 32);
	mbedtls_mpi_free(&Z); 
	mbedtls_ecp_point_free(&Qc);

	// info = "MT1" || sid || srv_pub || cli_pub
	std::vector<uint8_t> info; 
	info.reserve(3+4+65+65);
	info.push_back('M'); 
	info.push_back('T'); 
	info.push_back('1');
	info.push_back((uint8_t)(s_sid>>24)); 
	info.push_back((uint8_t)(s_sid>>16));
	info.push_back((uint8_t)(s_sid>>8));  
	info.push_back((uint8_t)(s_sid));

	uint8_t srvUncompressed[65]; 
	size_t olen = 0;
	if( mbedtls_ecp_point_write_binary(&s_grp, &s_pub, MBEDTLS_ECP_PF_UNCOMPRESSED,
		&olen, srvUncompressed, sizeof(srvUncompressed)) != 0 || olen != 65 ) 
	{
		return false;
	}  

	info.insert(info.end(), srvUncompressed, srvUncompressed+65);
	info.insert(info.end(), cliPub65.begin(), cliPub65.end());

	hkdf_sha256(getAppKey(), 32, shared, 32, info.data(), info.size(), s_sessKey);
	
	return( true );
}

////////////////////////////////////////////////////////////////////
// Convenience: HMAC(key, msg) and truncate to 16 bytes.
////////////////////////////////////////////////////////////////////
static void mac16(const uint8_t* key, const uint8_t* msg, size_t n, uint8_t out16[16])
{
	uint8_t full[32]; 
	hmac(key, 32, msg, n, full); 
	memcpy(out16, full, 16);
}

////////////////////////////////////////////////////////////////////
// Derive IV for AES-CTR:
//
//   iv16 = HMAC(sessKey, "IV"||sid||dir||seq)[0..15]
//
// dir = 'C' (client→server) or 'S' (server→client).
////////////////////////////////////////////////////////////////////
static void ivFrom(uint8_t dir, uint16_t seq, uint8_t iv16[16])
{
	uint8_t buf[3+4+1+2]; // "IV"+sid+dir+seq
	buf[0]='I'; 
	buf[1]='V'; 
	buf[2]=0;
	buf[3]=(uint8_t)(s_sid>>24); 
	buf[4]=(uint8_t)(s_sid>>16);
	buf[5]=(uint8_t)(s_sid>>8);  
	buf[6]=(uint8_t)(s_sid);
	buf[7]=dir; buf[8]=(uint8_t)(seq>>8); 
	buf[9]=(uint8_t)(seq);
	
	uint8_t h[32]; hmac(s_sessKey, 32, buf, sizeof(buf), h);
	memcpy(iv16, h, 16);
}

////////////////////////////////////////////////////////////////////
// AES-CTR wrapper using mbedTLS. The same function implements
// both encryption and decryption.
////////////////////////////////////////////////////////////////////
static bool aesCtr(const uint8_t key[32], const uint8_t iv16[16],
                   const uint8_t* in, uint8_t* out, size_t len)
{
	mbedtls_aes_context aes; 
	mbedtls_aes_init(&aes);
	mbedtls_aes_setkey_enc(&aes, key, 256);
	uint8_t nonce_counter[16];
	memcpy(nonce_counter, iv16, 16);
	uint8_t stream_block[16]; 
	size_t nc_off = 0;
	int rc = mbedtls_aes_crypt_ctr(&aes, len, &nc_off, nonce_counter, stream_block, in, out);
	mbedtls_aes_free(&aes);
	
	return rc==0;
}

////////////////////////
// Binary MTLS (B0..B3) IMPLEMENTATION 
//////////////////////////////////////

////////////////////////////////////////////////////////////////////
// Helper: export current server public key as 65-byte uncompressed form.
// This is used when constructing transcripts (KEYX/SFIN/MT1 info).
////////////////////////////////////////////////////////////////////
static bool get_srv_pub65(uint8_t out65[65]) 
{
	size_t olen=0;
	int rc = mbedtls_ecp_point_write_binary(&s_grp, &s_pub, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, out65, 65);
	
	return( rc==0 && olen==65 );
}

////////////////////////////////////////////////////////////////////
// mtls_sendHello_B0()
//
// Entry point for starting an MTLS handshake.
//
//  - Requires that an AppKey is already provisioned in NVS.
//  - Generates a fresh session id (sid) and ephemeral P-256 keypair.
//  - Builds B0 payload = srvPub65 || sidLE4.
//  - Caches B0 in s_lastB0 so mtls_tick() can retry a few times.
//  - Sends B0 via sendFrame(0xB0, ...).
////////////////////////////////////////////////////////////////////
bool mtls_sendHello_B0()
{
	// Require appkey present (same as mtls_prepareHello)
	if( !isAppKeyMarkedSet() ) 
	{
		DPRINTLN("[MTLS][B0] appKey not set"); 
		return false; 
	}
	ensureGroupInit();
	mtls_reset();

	// sid + ephemeral
	s_sid = esp_random();
	String dummy; // for logs
	if( !genKeypair(dummy) ) 
	{
		DPRINTLN("[MTLS][B0] genKeypair fail"); 
		return false; 
	}

	uint8_t pay[65 + 4];
	if( !get_srv_pub65(pay) ) 
	{
		DPRINTLN("[MTLS][B0] write srv pub fail"); 
		return false; 
	}
	// sid LE
	pay[65] = (uint8_t)(s_sid & 0xFF);
	pay[66] = (uint8_t)((s_sid >> 8) & 0xFF);
	pay[67] = (uint8_t)((s_sid >> 16) & 0xFF);
	pay[68] = (uint8_t)((s_sid >> 24) & 0xFF);

	// Cache for retransmit until B1 arrives
	s_lastB0.assign(pay, pay + sizeof(pay));
	s_b0Retries  = 0;
	s_b0NextAtMs = millis() + 300;   // first retry after 300 ms

	// top-level send via your binary framing (sendFrame in commands.h will pick MTLS path only after active)
	bool ok = sendFrame(0xB0, pay, sizeof(pay));
	DPRINT("[MTLS][B0] sendHello -> %s (sid=0x%08x)\n", ok?"OK":"FAIL", (unsigned)s_sid);
	
	return ok;
}

////////////////////////////////////////////////////////////////////
// mtls_tick()
//
// Called periodically from loop(). Responsible for retransmitting
// B0 while we are waiting for the client to respond with B1.
//
//  - If MTLS is already active or there is no cached B0, do nothing.
//  - Otherwise retry B0 up to 10 times, approx every 300 ms.
//  - After 10 retries we give up and drop the cached B0.
////////////////////////////////////////////////////////////////////
void mtls_tick()
{
	// Stop if session is active or no cached B0 or we exhausted retries
	if( mtls_isActive() || s_lastB0.empty() ) return;
	// aprox 3s window at 300 ms pace
	if( s_b0Retries >= 10 ) 
	{    
		s_lastB0.clear();
		return;
	}
	uint32_t now = millis();
	if( now >= s_b0NextAtMs ) 
	{
		DPRINT("[MTLS][B0] RETRY #%u\n", (unsigned)s_b0Retries + 1);
		sendFrame(0xB0, s_lastB0.data(), (uint16_t)s_lastB0.size());
		s_b0Retries++;
		s_b0NextAtMs = now + 300;
	}
}

////////////////////////////////////////////////////////////////////
// make_sfin_mac()
//
// Build the MAC for B2 (server finished):
//
//   mac16 = HMAC(sessKey32, "SFIN"||sid4||srvPub65||cliPub65)[0..15]
//
// The client uses the same formula to verify that both sides derived
// the same sessKey32 and that the transcript matches.
////////////////////////////////////////////////////////////////////
static void make_sfin_mac(uint8_t out16[16], const std::vector<uint8_t>& cli65) 
{
	uint8_t srv65[65]; get_srv_pub65(srv65);
	uint8_t sid4[4] = {
			(uint8_t)(s_sid & 0xFF), (uint8_t)((s_sid>>8)&0xFF),
			(uint8_t)((s_sid>>16)&0xFF), (uint8_t)((s_sid>>24)&0xFF)
		};
	std::vector<uint8_t> fin; fin.reserve(4+4+65+65);
	fin.insert(fin.end(), {'S','F','I','N'});
	fin.insert(fin.end(), sid4, sid4+4);
	fin.insert(fin.end(), srv65, srv65+65);
	fin.insert(fin.end(), cli65.begin(), cli65.end());
	mac16(s_sessKey, fin.data(), fin.size(), out16);
}

////////////////////////////////////////////////////////////////////
// mtls_tryConsumeOrDecryptFromBinary()
//
// Entry point from the binary dispatcher for B1/B3 frames.
//
//   - If op == B1 (KEYX):
//       * verify MAC with AppKey
//       * run deriveSessionKey()
//       * send B2 ("SFIN" MAC)
//       * set s_active=true, s_seqIn=0, s_seqOut=0
//       * stop any pending B0 retries
//       * return true (frame consumed)
//
//   - If op == B3 (encrypted record):
//       * verify MAC using sessKey32 and sid/dir='C'/seq
//       * check seq == s_seqIn (replay protection)
//       * AES-CTR decrypt cipher → outPlain
//       * bump s_seqIn
//       * return true (frame consumed, inner app frame in outPlain)
//
//   - For any other op: return false so the caller can handle it.
//
////////////////////////////////////////////////////////////////////
bool mtls_tryConsumeOrDecryptFromBinary( uint8_t op, const uint8_t* p, uint16_t n, std::vector<uint8_t>& outPlain )
{
	DPRINT("[B*] entry op=0x%02X n=%u\n", (unsigned)op, (unsigned)n);
	outPlain.clear();

	/////////////////////////
	// B1: KEYX (client→server)
	/////////////////////////
	if (op == 0xB1) 
	{ 
		if( n != (65 + 16) ) 
		{ 
			DPRINTLN("[MTLS][B1] len bad"); 
			return true; // consumed, but invalid
		}
		
		// Split payload into client pubkey (65 bytes) and mac16
		std::vector<uint8_t> cliPub(65); 
		memcpy(cliPub.data(), p, 65);
		const uint8_t* macIn = p + 65;

		 // Recompute MAC over "KEYX"||sid||srv_pub||cli_pub using AppKey
		uint8_t srv65[65]; 
		if( !get_srv_pub65(srv65) )
		{ 
			DPRINTLN("[MTLS][B1] srv pub fail"); 
			return true; 
		}
		uint8_t sid4[4] = {
			(uint8_t)(s_sid&0xFF),
			(uint8_t)((s_sid>>8)&0xFF),
			(uint8_t)((s_sid>>16)&0xFF),
			(uint8_t)((s_sid>>24)&0xFF) };
			
		std::vector<uint8_t> msg; msg.reserve(4+4+65+65);
		msg.insert(msg.end(), {'K','E','Y','X'});
		msg.insert(msg.end(), sid4, sid4+4);
		msg.insert(msg.end(), srv65, srv65+65);
		msg.insert(msg.end(), cliPub.begin(), cliPub.end());
		uint8_t macExp[16]; mac16(getAppKey(), msg.data(), msg.size(), macExp);

		// debug
		//    DPRINT("[MTLS][B1] sid=0x%08x (LE in transcript)\n", (unsigned)s_sid);
		//    DPRINT("[MTLS][B1] macExp[0..7]=");
		//    for (int i=0; i<8; ++i) DPRINT("%02x", (unsigned)macExp[i]);
		//    DPRINT(" macIn[0..7]=");
		//    for (int i=0; i<8; ++i) DPRINT("%02x", (unsigned)macIn[i]);
		//    DPRINTLN("");
		// end of debug

		if( memcmp(macExp, macIn, 16) != 0 ) 
		{ 
			DPRINTLN("[MTLS][B1] BADMAC"); 
			const char* e="BADMAC"; 
			sendFrame(0xFF,(const uint8_t*)e,strlen(e)); 
			return true; 
		}

		// MAC OK → derive session key using ECDH+HKDF
		if( !deriveSessionKey(cliPub) ) 
		{ 
			DPRINTLN("[MTLS][B1] derive failed"); 
			const char* e="DERIVE"; 
			sendFrame(0xFF,(const uint8_t*)e,strlen(e)); 
			return true; 
		}

		// Reply B2 with "SFIN" MAC under sessKey32
		uint8_t macS[16]; 
		make_sfin_mac(macS, cliPub);
		sendFrame(0xB2, macS, 16);

		// Mark session as active and reset seq counters
		s_active = true; 
		s_seqIn = s_seqOut = 0;

		// Stop any B0 retransmits now that handshake progressed
		s_lastB0.clear();
		s_b0Retries = 0;
		s_b0NextAtMs = 0;

		DPRINTLN("[MTLS] ACTIVE (binary)");
		return( true );
	}

	//////////////////////////
	// B3: ENC record (client→server)
	//////////////////////////////
	if( op == 0xB3 ) 
	{ 
		if( !s_active ) 
		{ 
			DPRINTLN("[MTLS][B3] no session"); 
			const char* e="NOSESSION"; 
			sendFrame(0xFF,(const uint8_t*)e,strlen(e)); 
			return true; 
		}
		
		// Frame structure: seq2 | clen2 | cipher[clen] | mac16
		if( n < 2+2+16 ) 
		{ 
			DPRINTLN("[MTLS][B3] short"); 
			return true; 
		}

		uint16_t seq  = (uint16_t)p[0] | ((uint16_t)p[1]<<8);
		uint16_t clen = (uint16_t)p[2] | ((uint16_t)p[3]<<8);
		if( n != (2+2+clen+16) ) 
		{
			DPRINTLN("[MTLS][B3] len mismatch"); 
			return( true ); 
		}
		const uint8_t* cipher = p+4;
		const uint8_t* macIn  = p+4+clen;

		// Recompute MAC = HMAC(sessKey,"ENCM"||sid||'C'||seq||cipher)[0..15]
		std::vector<uint8_t> macData; 
		
		macData.reserve(4+4+1+2+clen);
		macData.insert(macData.end(), {'E','N','C','M'});
		macData.push_back((uint8_t)(s_sid>>24)); 
		macData.push_back((uint8_t)(s_sid>>16));
		macData.push_back((uint8_t)(s_sid>>8));  
		macData.push_back((uint8_t)s_sid);
		macData.push_back('C'); 
		macData.push_back((uint8_t)(seq>>8)); 
		macData.push_back((uint8_t)seq);
		macData.insert(macData.end(), cipher, cipher+clen);
		
		uint8_t macExp[16]; 
		mac16(s_sessKey, macData.data(), macData.size(), macExp);
		
		if( memcmp(macExp, macIn, 16) != 0 ) 
		{ 
			DPRINTLN("[MTLS][B3] BADMAC"); 
			const char* e="BADMAC"; 
			sendFrame(0xFF,(const uint8_t*)e,strlen(e)); 
			return true; 
		}

		// Replay protection: require exact next sequence
		if( seq != s_seqIn ) 
		{
			DPRINT("[MTLS][B3] REPLAY seq=%u expect=%u\n",(unsigned)seq,(unsigned)s_seqIn); 
			const char* e="REPLAY"; 
			sendFrame(0xFF,(const uint8_t*)e,strlen(e)); 
			return true; 
		}

		// MAC OK & seq OK → AES-CTR decrypt to outPlain
		outPlain.resize(clen);
		uint8_t iv[16]; 
		ivFrom('C', seq, iv);
		if( !aesCtr(s_sessKey, iv, cipher, outPlain.data(), clen) )
		{ 
			DPRINTLN("[MTLS][B3] AES fail"); 
			outPlain.clear(); 
			return true; 
		}

		++s_seqIn;

		// At this point outPlain holds the inner app frame:
		// [OP|LENle|PAYLOAD]. The caller (commands.cpp) will
		// recursively feed it back into dispatch_binary_frame().

		// Debug (first 64B)
//		String hx; 
		// reuse toHex 
//		toHex(outPlain.data(), std::min<size_t>(outPlain.size(),64), hx);
//		DPRINT("[MTLS][B3] dec len=%u hex[64]=%s\n", (unsigned)outPlain.size(), hx.c_str());

		return( true );
	}

	// Not a B* frame → let the caller handle other opcodes.
	return( false ); 
}

////////////////////////////////////////////////////////////////////
// mtls_wrapAndSendBytes_B3()
//
// Encrypts a plaintext application frame as a B3 record and sends it.
//
//  Input plaintext format is always the app-level frame:
//    [OP][LENle][PAYLOAD]
//
//  This function:
//    - builds iv16 = HMAC(sessKey,"IV"||sid||'S'||seqOut)[0..15]
//    - AES-CTR encrypts the entire app frame to `enc`
//    - builds MAC over ENCM||sid||'S'||seqOut||cipher
//    - constructs B3 payload: seq2 | clen2 | cipher | mac16
//    - wraps as [0xB3][LENle][payload] and sends via sendTX()
//
//  note: deliberately bypass sendFrame() here to avoid wrapping
//  a B3 frame inside another app-level frame.
//
////////////////////////////////////////////////////////////////////
bool mtls_wrapAndSendBytes_B3(const uint8_t* plain, size_t n)
{
	if (!s_active) return( false );

	// Encrypt with dir='S'
	std::vector<uint8_t> enc(n);
	uint8_t iv[16]; ivFrom('S', s_seqOut, iv);
	if( !aesCtr(s_sessKey, iv, plain, enc.data(), n) ) return( false );

	// mac over ENCM||sid||'S'||seq||cipher
	std::vector<uint8_t> macData; 
	macData.reserve(4+4+1+2+n);
	macData.insert(macData.end(), {'E','N','C','M'});
	macData.push_back((uint8_t)(s_sid>>24)); 
	macData.push_back((uint8_t)(s_sid>>16));
	macData.push_back((uint8_t)(s_sid>>8));  
	macData.push_back((uint8_t)s_sid);
	macData.push_back('S'); 
	macData.push_back((uint8_t)(s_seqOut>>8)); 
	macData.push_back((uint8_t)s_seqOut);
	macData.insert(macData.end(), enc.begin(), enc.end());

	uint8_t mac[16]; 
	mac16( s_sessKey, macData.data(), macData.size(), mac );

	// Build B3 payload: seq2 | clen2 | cipher | mac16
	std::vector<uint8_t> pay; 
	pay.reserve(2 + 2 + n + 16);
	pay.push_back((uint8_t)(s_seqOut & 0xFF));
	pay.push_back((uint8_t)(s_seqOut >> 8));
	pay.push_back((uint8_t)(n & 0xFF));
	pay.push_back((uint8_t)(n >> 8));
	pay.insert(pay.end(), enc.begin(), enc.end());
	pay.insert(pay.end(), mac, mac + 16);

	// SEND TOP-LEVEL: [0xB3][LENle][payload] — bypass sendFrame to avoid re-wrap
	std::vector<uint8_t> out; 
	out.reserve(1 + 2 + pay.size());
	out.push_back(0xB3);
	out.push_back((uint8_t)(pay.size() & 0xFF));
	out.push_back((uint8_t)(pay.size() >> 8));
	out.insert(out.end(), pay.begin(), pay.end());

	extern bool sendTX(const uint8_t* data, size_t len);
	bool ok = sendTX(out.data(), (uint16_t)out.size());

	++s_seqOut;
	
	return( ok );
}

////////////////////////////////////////////////////////////////////
// sendWrappedAppKey()
//
// Used by APPKEY onboarding (opcode A3 handler in commands.h).
//
// Inputs:
//   verif32 : 32-byte password verifier from NVS (PBKDF2 output)
//   chal16  : 16-byte challenge we previously sent to the app
//
// We don't want to send the AppKey in the clear, so we derive a
// one-time wrapping key from (verif32, chal16) and wrap the AppKey:
//
//   wrapKey32 = HMAC(verif32, "AKWRAP" || chal16)
//   iv16      = HMAC(verif32, "AKIV"   || chal16)[0..15]
//   cipher32  = AES-CTR(wrapKey32, iv16, appKey32)
//   mac16     = HMAC(wrapKey32, "AKMAC" || chal16 || cipher32)[0..15]
//
// The resulting payload is:
//    A1 payload = cipher32 || mac16  (48 bytes)
//
// The Android side uses the same formulas to unwrap AppKey.
// On success we call markAppKeySet() so subsequent GET_APPKEY
// requests can be rejected.
//
////////////////////////////////////////////////////////////////////
bool sendWrappedAppKey(const uint8_t verif32[32], const uint8_t chal16[16])
{
    // Build AKWRAP message
    uint8_t t1[6 + 16]; // "AKWRAP" + chal16
    memcpy(t1, "AKWRAP", 6);
    memcpy(t1 + 6, chal16, 16);

    uint8_t wrapKey[32];
    hmac( verif32, 32, t1, sizeof(t1), wrapKey ); // wrapKey = HMAC(verif, "AKWRAP"||chal)

    // IV = HMAC(verif, "AKIV" || chal)[0..15]
    uint8_t t2[5 + 16]; // "AKIV" + chal
    //memcpy(t2, "AKIV", 4); t2[4] = 0; 
    // use "AKIV" + chal16 no null term
    memcpy(t2, "AKIV", 4);
    memcpy(t2 + 4, chal16, 16);
    uint8_t ivfull[32];
    hmac(verif32, 32, t2, 4 + 16, ivfull);
    uint8_t iv16[16]; memcpy(iv16, ivfull, 16);

    // Encrypt appkey (32 bytes)
    const uint8_t* appkey = getAppKey();
    uint8_t cipher32[32];
    if( !aesCtr(wrapKey, iv16, appkey, cipher32, 32) ) 
	{
        DPRINTLN("[APPKEY][A1] AES-CTR encrypt failed");
        return false;
    }

    // MAC = HMAC(wrapKey, "AKMAC" || chal || cipher)[0..15]
    // Build mac input = "AKMAC" + chal16 + cipher32
    size_t macInLen = 5 + 16 + 32;
    std::vector<uint8_t> macIn; macIn.reserve(macInLen);
    macIn.insert(macIn.end(), {'A','K','M','A','C'});
    macIn.insert(macIn.end(), chal16, chal16 + 16);
    macIn.insert(macIn.end(), cipher32, cipher32 + 32);
    uint8_t macFull[32]; hmac(wrapKey, 32, macIn.data(), macIn.size(), macFull);
    uint8_t mac16[16]; memcpy(mac16, macFull, 16);

    // Build payload: cipher32 || mac16
    uint8_t payload[48];
    memcpy(payload, cipher32, 32);
    memcpy(payload + 32, mac16, 16);

    // Send A1 with wrapped payload
    bool ok = sendFrame(0xA1, payload, sizeof(payload));
    if (ok) {
        markAppKeySet();
    } else {
        DPRINTLN("[APPKEY][A1] sendFrame failed");
    }

    // Wipe sensitive material
    memset(wrapKey, 0, sizeof(wrapKey));
    memset(ivfull, 0, sizeof(ivfull));
    memset(iv16, 0, sizeof(iv16));
    memset(cipher32, 0, sizeof(cipher32));
    memset(macFull, 0, sizeof(macFull));
    memset(mac16, 0, sizeof(mac16));

    return ok;
}

