////////////////////////////////////////////////////////////////////
// Open the NVS "app" namespace once for read/write access.
// Safe to call multiple times; it will only open on first use.
////////////////////////////////////////////////////////////////////
#pragma once
#include <Arduino.h>
#include <Preferences.h>
// esp_read_mac, ESP_MAC_BT 
#include <esp_mac.h>  


// Protocol / firmware identifiers 
#define PROTO_VER "1.3"
#define FW_VER    "1.2.2"

/////////////////////////////
// *** DEBUG ***
#define DEBUG_ENABLED 1
#include "debug_utils.h"
/////////////////////////////

// locals
#include "layout_kb_profiles.h"  // for KeyboardLayout + extern m_nKeyboardLayout


// Global Preferences object 
extern Preferences gPrefs;

extern String g_BleName;  

extern bool g_allowPairing; 

extern uint8_t g_appKey[32];
extern bool    g_appKeySet;

// --- New NVS keys for initial web setup ---
static const char* const NVS_KEY_SETUP_DONE   = "setup_done";   // 0/1
static const char* const NVS_KEY_BLE_NAME     = "ble_name";     // string
static const char* const NVS_KEY_PW_SALT      = "pw_salt";      // 16 bytes
static const char* const NVS_KEY_PW_VERIF     = "pw_verif";     // 32 bytes (HMAC/PBKDF2 result)
static const char* const NVS_KEY_PW_ITERS     = "pw_iters";     // uint32


////////////////////////////////////////////////////////////////////
// Ensure NVS namespace is open for RW access
////////////////////////////////////////////////////////////////////
static void ensurePrefsOpenRW()
{
    static bool opened = false;
    if (!opened)
    {
        // Change "app" to any other namespace if you prefer
        if (!gPrefs.begin("app", /*readOnly=*/false))
        {
            DPRINTLN("[Settings] Failed to open NVS namespace 'app'");
        }
        else
        {
            opened = true;
            // DPRINTLN("[Settings] NVS namespace 'app' opened");
        }
    }
}

// -----------------------------------------------------------------------------
// Internal NVS keys
// -----------------------------------------------------------------------------
static const char* const NVS_KEY_LAYOUT    = "kb_layout";
static const char* const NVS_KEY_ALLOWPAIR = "allowPair";
// app key generation/persistent storage
static const char* const NVS_KEY_APPKEY     = "app_key32";   // 32 bytes
static const char* const NVS_KEY_APPKEY_SET = "app_key_set"; // 0/1

////////////////////////////////////////////////////////////////////
// Keyboard layout (stored as uint8_t enum in NVS)
////////////////////////////////////////////////////////////////////
static void loadLayoutFromNVS()
{
    ensurePrefsOpenRW();
    uint8_t raw = gPrefs.getUChar(
        NVS_KEY_LAYOUT,
        static_cast<uint8_t>(KeyboardLayout::US_WINLIN));

    if (raw < 1 || raw > 100)
        raw = static_cast<uint8_t>(KeyboardLayout::US_WINLIN);

    m_nKeyboardLayout = static_cast<KeyboardLayout>(raw);
}

static void saveLayoutToNVS(KeyboardLayout id)
{
    ensurePrefsOpenRW();
    gPrefs.putUChar(NVS_KEY_LAYOUT, static_cast<uint8_t>(id));
}

////////////////////////////////////////////////////////////////////
// Pairing-lock flag
//
// true  = pairing window open (any central can pair)
// false = locked to already-bonded centrals only
////////////////////////////////////////////////////////////////////
static bool getAllowPairing()
{
    return( g_allowPairing );
}

static void savePairingFlagToNVS(bool allow)
{
    ensurePrefsOpenRW();
    gPrefs.putUChar(NVS_KEY_ALLOWPAIR, allow ? 1 : 0);
    g_allowPairing = allow;
}

static void loadPairingFlagFromNVS()
{
    ensurePrefsOpenRW();
    uint8_t v = gPrefs.getUChar(NVS_KEY_ALLOWPAIR, 1);
    g_allowPairing = (v != 0);
}

static void setAllowPairing(bool allow)
{
    savePairingFlagToNVS(allow);
}

////////////////////////////////////////////////////////////////////
// If a 32-byte AppKey exists in NVS, load it into g_appKey.
// Otherwise generate a random 32-byte key and store it.
//
// g_appKeySet only indicates whether the key was marked "provisioned"
// (e.g. derived from user password); the key itself always exists after this.
////////////////////////////////////////////////////////////////////
static void loadOrGenAppKeyForMTLS()
{
    ensurePrefsOpenRW();
    //g_appKeySet = (gPrefs.getUChar(NVS_KEY_APPKEY_SET, 0) != 0);
	g_appKeySet = (gPrefs.getUChar(NVS_KEY_APPKEY_SET, 0) != 0);	

    size_t sz = 32;
    if( gPrefs.getBytesLength(NVS_KEY_APPKEY) == 32 &&
        gPrefs.getBytes(NVS_KEY_APPKEY, g_appKey, sz) == 32 ) 
	{
        return; // key exists
    }

    for( int i=0; i<32; ++i ) g_appKey[i] = (uint8_t)esp_random();
    gPrefs.putBytes( NVS_KEY_APPKEY, g_appKey, 32 );
}

static const uint8_t* getAppKey() { return g_appKey; }
static bool isAppKeyMarkedSet()   { return g_appKeySet; }
static void markAppKeySet()       
{
	ensurePrefsOpenRW(); 
	gPrefs.putUChar(NVS_KEY_APPKEY_SET, 1); 
	g_appKeySet = true; 
}

////////////////////////////////////////////////////////////////////
// Remove AppKey + setup-related keys from NVS and clear in-RAM copy.
// Used by factory reset.
////////////////////////////////////////////////////////////////////
static inline void clearAppKeyAndFlag()
{
    ensurePrefsOpenRW();
    gPrefs.remove(NVS_KEY_APPKEY);
    gPrefs.remove(NVS_KEY_APPKEY_SET);
	
	// remove all setup flags back to default
	gPrefs.remove(NVS_KEY_SETUP_DONE);
	gPrefs.remove(NVS_KEY_BLE_NAME);
	gPrefs.remove(NVS_KEY_PW_SALT);
	gPrefs.remove(NVS_KEY_PW_VERIF);
	gPrefs.remove(NVS_KEY_PW_ITERS);
		
    memset(g_appKey, 0, 32);
    g_appKeySet = false;
}

// Has the initial web setup been completed 
static inline bool isSetupDone() 
{
    ensurePrefsOpenRW();
    return gPrefs.getUChar(NVS_KEY_SETUP_DONE, 0) != 0;
}

// Set or clear the "setup completed" marker in NVS
static inline void setSetupDone(bool v) 
{
    ensurePrefsOpenRW();
    gPrefs.putUChar(NVS_KEY_SETUP_DONE, v ? 1 : 0);
}

////////////////////////////////////////////////////////////////////
// - loadBleName() / saveBleName() store a user-defined name in NVS.
// - computeDefaultBleName() builds a short unique name from BLE MAC.
// - initBleNameGlobal() picks NVS name or falls back to MAC-based default.
////////////////////////////////////////////////////////////////////
static inline String loadBleName() 
{
    ensurePrefsOpenRW();
    return gPrefs.getString(NVS_KEY_BLE_NAME, "BlueKeyboard");
}

static inline void saveBleName(const String& name) 
{
    ensurePrefsOpenRW();
    gPrefs.putString( NVS_KEY_BLE_NAME, name );
}

////////////////////////////////////////////////////////////////////
// Password KDF params used by web setup (salt, verifier, iterations)
////////////////////////////////////////////////////////////////////
static inline void savePwKdf(const uint8_t* salt16, const uint8_t* verif32, uint32_t iters) 
{
    ensurePrefsOpenRW();
    gPrefs.putBytes(NVS_KEY_PW_SALT,  salt16, 16);
    gPrefs.putBytes(NVS_KEY_PW_VERIF, verif32, 32);
    gPrefs.putUInt(NVS_KEY_PW_ITERS, iters);
}

static inline bool loadPwKdf(uint8_t* salt16_out, uint8_t* verif32_out, uint32_t* iters_out) 
{
    ensurePrefsOpenRW();
    if (gPrefs.getBytesLength(NVS_KEY_PW_SALT)  != 16) return false;
    if (gPrefs.getBytesLength(NVS_KEY_PW_VERIF) != 32) return false;
    size_t sz = 16; gPrefs.getBytes(NVS_KEY_PW_SALT,  salt16_out, sz);
    sz = 32; gPrefs.getBytes(NVS_KEY_PW_VERIF, verif32_out, sz);
    *iters_out = gPrefs.getUInt(NVS_KEY_PW_ITERS, 10000);
    return( true );
}

////////////////////////////////////////////////////////////////////
// Build a short, user-friendly default BLE name from the last 2 bytes of MAC.
////////////////////////////////////////////////////////////////////
static inline String computeDefaultBleName()
{
	uint8_t mac[6];
	// Read BLE MAC directly from eFuse-derived address (no BLE stack needed)
	esp_read_mac(mac, ESP_MAC_BT);

	char buf[32];
	// Use last two bytes for compact ID (same style as your AP SSID)
	snprintf(buf, sizeof(buf), "BluKbd_%02X%02X", mac[4], mac[5]);
	
	return( String(buf) );
}

// init the ble name here
static inline void initBleNameGlobal()
{
	g_BleName = loadBleName();

	// If name is empty or still default, generate a unique MAC-based name
	if( g_BleName.length() < 3 || g_BleName == "BlueKeyboard" ) 
	{
		// fallback to auto-generated default
		g_BleName = computeDefaultBleName();
		// Store the generated default so it stays stable across reboots
		saveBleName( g_BleName );	
	}
}

////////////////////////////////////////////////////////////////////
// - Opens NVS namespace.
// - Initialises global BLE name (NVS or MAC-based default).
// - Loads keyboard layout (with sane default).
// - Loads pairing flag (default: pairing allowed).
// - Loads or generates MTLS AppKey.
// Call once from setup().
////////////////////////////////////////////////////////////////////
static void initSettings()
{
    ensurePrefsOpenRW();

	// initiliaze load ble name
	initBleNameGlobal();

    // ---- Load keyboard layout ----
    uint8_t raw = gPrefs.getUChar(
        NVS_KEY_LAYOUT,
        static_cast<uint8_t>(KeyboardLayout::US_WINLIN));

    if (raw < 1 || raw > 100)
        raw = static_cast<uint8_t>(KeyboardLayout::US_WINLIN);

    m_nKeyboardLayout = static_cast<KeyboardLayout>(raw);

    // ---- Load pairing flag ----
    uint8_t v = gPrefs.getUChar(NVS_KEY_ALLOWPAIR, 1);
    g_allowPairing = (v != 0);
	
    // load if app key was set
   loadOrGenAppKeyForMTLS();
}
