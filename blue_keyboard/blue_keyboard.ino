////////////////////////////////////////////////////////////////////
//  blue_keyboard.ino (v1.2.3)
//  Created by: Larry Lart
//
//  Main firmware for the BlueKeyboard / BluKey dongle.
//
//    - Bring up hardware (USB HID, BLE, TFT display, status LED).
//    - Manage BLE connection state (connected / paired / encrypted).
//    - Provide a raw TX pipe (sendTX) for protocol layers.
//    - Run the Wi-Fi setup portal on first boot to set BLE name,
//      keyboard layout, and setup password (for APPKEY).
//    - Kick off the MTLS handshake to the Android app and call
//      into the binary command/MTLS stack (commands.h + mtls.cpp).
//    - Drive UI feedback: READY / RECV counters / LED blink.
//    - Main loop: poll BLE + MTLS tick + LED/timing housekeeping.
//
//  The actual protocol and crypto:
//    - mtls.cpp/.h      : B0/B1/B2/B3 MTLS handshake and record layer
//    - commands.h       : binary opcodes (APPKEY, layout, SEND_STRING)
//    - settings.h       : NVS-backed config (AppKey, layout, pairing flag)
//    - layout_kb_profiles.h + RawKeyboard.h : layout-aware typing
////////////////////////////////////////////////////////////////////
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <FastLED.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDConsumerControl.h"
#include "pin_config.h"
#include "TFT_eSPI.h" 
#include <Preferences.h>     // NVS key/value
#include <MD5Builder.h>      // Arduino MD5 helper (ESP32 core)
#include <esp_system.h>   // esp_restart()
// esp_read_mac, ESP_MAC_BT 
#include <esp_mac.h>   

// c includes
extern "C" 
{
  #include "nimble/ble.h"
  #include "host/ble_gap.h"
  #include "host/ble_store.h"
}

/////////////////////////////
// *** DEBUG ***
#define DEBUG_ENABLED 1
#include "debug_utils.h"
/////////////////////////////

// locals
#include "mtls.h"
#include "settings.h"
#include "RawKeyboard.h"
#include "layout_kb_profiles.h"
#include "commands.h"
#include "setup_portal.h"

// defines
#define MAX_RX_MESSAGE_LENGTH 4096
// Onboard APA102 pins (T-Dongle-S3)
#define LED_DI 40
#define LED_CI 39
// BLE UUIDs 
#define SERVICE_UUID      "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_WRITE_UUID   "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // RX: phone - dongle
#define CHAR_NOTIFY_UUID  "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // TX: dongle - phone

// BLE write handler (secured by link state) 
////////////////////////////////////////////////////////////////////
// forward decls from commands.h / mtls.cpp
extern bool dispatch_binary_frame(const uint8_t* buf, size_t len);
extern void mtls_tick();
extern "C" void mtls_onDisconnect();

// Global (defined in blue_keyboard.ino)
KeyboardLayout m_nKeyboardLayout = KeyboardLayout::US_WINLIN;

// flags
static volatile bool g_echoOnConnect = false;

Preferences gPrefs;
bool g_allowPairing = true; // cached in RAM
bool g_allowMultiApp = false;     // allow multiple apps to provision when true
bool g_allowMultiDev = false;     // allow multiple devices to pair when true

// Raw fast-path: per-connection, not persisted
bool g_rawFastMode = false;

/////////////
CRGB led[1];
TFT_eSPI tft = TFT_eSPI();

// USB HID keyboard 
RawKeyboard Keyboard;

// Consumer Control device used by RawKeyboard::sendConsumerUsage
USBHIDConsumerControl MediaControl;

NimBLECharacteristic* pWriteChar = nullptr;
static NimBLECharacteristic* g_txChar = nullptr;

volatile uint32_t recvCount = 0;

// keep track of current "base" LED state
CRGB currentColor = CRGB::Black;

// flags - schedule display events in the main loop
static volatile bool g_displayReadyScheduled = false;
static volatile bool g_blinkLedScheduled = false;
// Deferred hello
static volatile bool g_pendingHello = false;
static uint32_t g_helloDueAtMs = 0;
static std::string g_helloPayload;

// -----------------------------------------------------------------------------
// Security / link state flags
//
// g_linkEncrypted      : true once the BLE link is encrypted at controller level
// g_linkAuthenticated  : true once MITM-protected pairing/bonding completed
// g_bootPasskey        : random 6-digit passkey generated on each boot
//
// g_encReady           : “link good enough for UI” (set when both above are true)
// g_pinShowScheduled   : we plan to show the PIN if encryption is *not* ready
// g_pinDueAtMs         : deadline; if we reach this without encryption, show PIN
// PIN_DELAY_MS         : small grace period before we expose the PIN on TFT
// -----------------------------------------------------------------------------
static volatile bool g_linkEncrypted = false;
static volatile bool g_linkAuthenticated = false;
static uint32_t g_bootPasskey = 0;   // random per boot

// Pairing UI gating flags
static volatile bool g_encReady = false;           // becomes true on ENC_CHANGE/auth complete
static volatile bool g_pinShowScheduled = false;   // we will show PIN only if still not encrypted after a short delay
static volatile bool g_pinAllowedThisConn = false;   // only true for real pairing attempts
static uint32_t g_pinDueAtMs = 0;
static const uint32_t PIN_DELAY_MS = 600;          // 400–800ms works well, adjust as needed

///////////////////////////////////
static volatile bool g_isSubscribed = false;

// Remember exactly which char/connection is subscribed
static NimBLECharacteristic* g_txSubChar = nullptr;
static uint16_t g_txConnHandle = 0xFFFF;

// reset to defaults
static bool  g_longPressConsumed = false;
static bool  g_btnWasDown        = false;
static uint32_t g_pressStartMs   = 0;

// flags and stor for app key
uint8_t g_appKey[32] = {0};
bool    g_appKeySet  = false;

String g_BleName;  

// Single-slot RX frame queue to keep heavy MTLS/command processing out of callbacks
static volatile bool   g_framePending = false;
static uint8_t         g_frameBuf[MAX_RX_MESSAGE_LENGTH];
static size_t          g_frameLen     = 0;

// mutex locker for critical section
portMUX_TYPE g_frameMux = portMUX_INITIALIZER_UNLOCKED;

//////////
// exported to settings/commands/mtls
bool isLinkSecure(){ return g_linkEncrypted && g_linkAuthenticated; }

////////////////////////////////////////////////////////////////////
// setLED(color)
//
// Tiny wrapper around FastLED for the single APA102 on the LilyGO T-Dongle-S3.
// Keeps 'currentColor' in sync so higher-level UI code can temporarily override
// the LED (e.g. blink red) and restore the previous state afterwards.
////////////////////////////////////////////////////////////////////
static inline void setLED(const CRGB& c) 
{
	led[0] = c;
	FastLED.show();
}

////////////////////////////////////////////////////////////////////
// Blocking 1-second red blink used as a coarse "activity" indicator:
//
//   - Sets LED to red for ~1s.
//   - Restores whatever color was previously in 'currentColor'.
//
// This is called from loop() when 'g_blinkLedScheduled' is set by
// onStringTyped(), so every received+typed string causes a visible flash.
// NOTE: currently uses delay(1000) and therefore blocks loop(); if needed,
// this can be refactored into a non-blocking two-stage state machine.
////////////////////////////////////////////////////////////////////
void blinkLed() 
{
	led[0] = CRGB::Red;
	FastLED.show();
	
	// short 100ms blink - todo: should probably do a non blocking blink
	delay(80);
	
	led[0] = currentColor;
	FastLED.show();
}

////////////////////////////////////////////////////////////////////
// sendTX(data, len)
//
// Low-level BLE TX primitive used by the binary protocol layer.
//
// Call path:
//   commands.h::sendFrame(...)  -  mtls_wrapAndSendBytes_B3(...) [optional]  -  sendTX()
//
// Expectations:
//   - 'data' already contains a complete framed message:
//         [OP][LENle][PAYLOAD]
//     either in plaintext (pre-MTLS) or already wrapped in a B3 record. 
//   - The caller does *not* need to worry about ATT_MTU; this function will
//     chunk the buffer to (MTU-3) sized notifications.
//
// Safety:
//   - Refuses to send if:
//       - no TX characteristic is set up (g_txChar == nullptr)
//       - the link is not both encrypted *and* authenticated
//       - (on NimBLE-CPP builds) there are no subscribers
//
// Implementation notes:
//   - Computes current ATT_MTU and derives 'maxPayload = MTU-3' to account
//     for the GATT header.
//   - Walks the input buffer in a loop and sends consecutive chunks via
//     g_txChar->setValue() + notify().
//   - Inserts a small delay(1) between chunks so the BLE stack does not get
//     starved on bursty traffic.
////////////////////////////////////////////////////////////////////
bool sendTX(const uint8_t* data, size_t len)
{
	if( !g_txChar ) return( false );
	
	// Guard: do not send anything over an unencrypted or unauthenticated link.
	if( !g_linkEncrypted || !g_linkAuthenticated ) return( false );

#ifdef NIMBLE_CPP
	if( g_txChar->getSubscribedCount() == 0 ) return( false );
#endif

	// ATT_MTU includes 3-byte header -> max notify payload = MTU - 3
	uint16_t mtu = NimBLEDevice::getMTU();
	if( mtu < 23 ) mtu = 23;
	const uint16_t maxPayload = mtu - 3;

	//DPRINT("[SENDTX] data=%s, mtu=%d max_payload=%d\n", data, mtu, maxPayload );

	size_t off = 0;
	while (off < len) 
	{
		size_t n = len - off;
		if( n > maxPayload ) n = maxPayload;

		g_txChar->setValue( reinterpret_cast<const uint8_t*>(data + off), n );

#if defined(ARDUINO_ARCH_ESP32)
		if( !g_txChar->notify() ) return( false );
#else
		g_txChar->notify();
#endif

		off += n;
		delay(1); // keep BLE stack happy on bursts
	
	}
	return( true );
}

////////////////////////////////////////////////////////////////////
// displayStatus(msg, color, big)
//
// Central helper for drawing transient status messages on the TFT
// (READY / SECURED / RESET / PIN / setup hints).
//
// Behaviour:
//   - Clears the bounding box of the previous message only, not the whole
//     screen, to avoid flicker.
//   - Centers the new message horizontally and vertically.
//   - Uses a larger font when 'big == true' (boot, errors, PIN).
//
// This is used throughout setup(), the security callbacks, factoryReset(),
// and the setup portal to keep the UI consistent.
////////////////////////////////////////////////////////////////////
void displayStatus( const String& msg, uint16_t color, bool big = true ) 
{
	static bool hasBox = false;
	static int16_t bx, by; static uint16_t bw, bh;

	tft.setTextDatum(MC_DATUM);
	tft.setTextFont(big ? 4 : 2);
	tft.setTextColor(color, TFT_BLACK);

	if( hasBox ) tft.fillRect(bx - 2, by - 2, bw + 4, bh + 4, TFT_BLACK);

	bw = tft.textWidth(msg);
	bh = tft.fontHeight();
	bx = (tft.width()  - (int)bw) / 2;
	by = (tft.height() - (int)bh) / 2;

	tft.setTextDatum(TL_DATUM);
	tft.drawString(msg, bx, by);
	hasBox = true;
}

////////////////////////////////////////////////////////////////////
// showPin(pin)
//
// Renders the current 6-digit BLE passkey on the TFT as:
//
//      "PIN: 123456"
//
// Used when the stack decides that this is a "real" pairing (no existing
// bond) and we want the user to confirm the PIN on the host side. The actual
// decision to show or hide the PIN is made in schedulePinMaybe() / loop().
////////////////////////////////////////////////////////////////////
static inline void showPin(uint32_t pin) 
{
	char buf[32];
	snprintf( buf, sizeof(buf), "PIN: %06lu", (unsigned long) pin );
	//displayStatus(buf, TFT_YELLOW, /*big*/ true);
	displayStatus(buf, TFT_BLUE, /*big*/ true);
}

static inline void schedulePinMaybe() 
{
	g_pinShowScheduled = true;
	g_pinDueAtMs = millis() + PIN_DELAY_MS;
}

static inline void cancelPin() 
{
	g_pinShowScheduled = false;
	// hidePin(); 
}

////////////////////////////////////////////////////////////////////
// Compact "READY" banner + device name + layout summary drawn when the BLE
// link is fully secured and the app has completed initialisation.
//
// Called from:
//   - ServerCallbacks::onAuthenticationComplete() via g_displayReadyScheduled
//   - loop(), which checks the flag and draws on the next iteration.
//
// Keeps the READY UI out of the NimBLE callback context.
////////////////////////////////////////////////////////////////////
void drawReady() { displayStatus("READY", TFT_GREEN, true); }

////////////////////////////////////////////////////////////////////
// Updates the small "RECV: <count>" counter on the TFT, showing how many
// application strings have been received over MTLS and typed on the host. :contentReference[oaicite:1]{index=1}
//
// Called from onStringTyped(), which bumps 'recvCount' and schedules a LED
// blink. This gives the user a quick visual confirmation that a password or
// payload has just been sent.
////////////////////////////////////////////////////////////////////
void drawRecv() 
{
	char buf[32];
	snprintf(buf, sizeof(buf), "RECV: %lu", (unsigned long)recvCount);
	displayStatus(buf, TFT_WHITE, true);
}

////////////////////////////////////////////////////////////////////
// Called from commands.h once a 0xD0 SEND_STRING frame has:
//
//   1. Been received over BLE,
//   2. Decrypted via MTLS (B3 - inner app frame), and
//   3. Been fully typed as USB HID keystrokes via sendUnicodeAware(). 
//
// Current responsibilities:
//   - Increment 'recvCount' and refresh the "RECV: N" label on the TFT.
//   - Set 'g_blinkLedScheduled' so the main loop performs a 1s red blink.
//
// NOTE: 'numBytes' is the size of the ciphertext payload as seen by the app;
// currently unused but kept for future telemetry or rate-limiting.
////////////////////////////////////////////////////////////////////
void onStringTyped( size_t numBytes )
{
	// not used yet
	(void) numBytes;  

	recvCount++;
	drawRecv();

	// schedule LED blink like before
	g_blinkLedScheduled = true;
}

////////////////////////////////////////////////////////////////////
// handleWrite(val_in)
//
// Central handler for all data written to the NUS RX characteristic
// (CHAR_WRITE_UUID) by the app over BLE. 
//
// Responsibilities:
//   - Optional: log the first few bytes of the incoming buffer in hex for
//     debugging (useful when bringing up new protocol versions).
//   - Enforce a hard cap of MAX_RX_MESSAGE_LENGTH to avoid unbounded RAM use
//     or malformed writes.
//   - Forward the raw byte buffer to dispatch_binary_frame(), which parses
//     the [OP|LEN|PAYLOAD] framing and:
//
//        - Handles B1/B3 (MTLS handshake / encrypted records) in mtls.cpp.
//        - Routes application opcodes (layout/info/reset/send_string) in
//          commands.h.
//
// This function is called from NimBLE's characteristic callbacks
// (WriteCallback::onWrite()) and therefore should return quickly and avoid
// any heavy work in the BLE stack context.
////////////////////////////////////////////////////////////////////
void handleWrite( const std::string& val_in ) 
{
	// Quick hex log (first 64 bytes) - disable in release mode
	/*if( !DEBUG_GLOBAL_DISABLED && DEBUG_ENABLED && !val_in.empty() ) 
	{
		const uint8_t* b0 = reinterpret_cast<const uint8_t*>(val_in.data());
		size_t n0 = val_in.size(), m0 = n0 < 64 ? n0 : 64;
		static const char* H = "0123456789abcdef";
		String hx; hx.reserve(m0*2);
		for( size_t i = 0; i < m0; ++i ) 
		{ 
			hx += H[b0[i] >> 4]; 
			hx += H[b0[i] & 0xF]; 
		}
		DPRINT("[CMD][RX] rawLen=%u hex[64]=%s\n", (unsigned)n0, hx.c_str());
	}*/
	
	// debug heap alignment problem - just keep this in place for now - todo: clear if all good in the future 
	String hx; hx.reserve(128); 
	
	if( val_in.empty() ) return;

	const uint8_t* b = reinterpret_cast<const uint8_t*>(val_in.data());
	size_t         n = val_in.size();

	if( n < 3 ) 
	{ 
		const char* e="short"; 
		sendFrame(0xFF,(const uint8_t*)e,strlen(e)); 
		return; 
	}
	
	uint8_t  op  = b[0];
	uint16_t len = (uint16_t)b[1] | ((uint16_t)b[2]<<8);
	if( n < 3+len ) 
	{ 
		const char* e="len"; 
		sendFrame(0xFF,(const uint8_t*)e,strlen(e)); 
		return; 
	}

/* we do the processing in the main loop
	// IMPORTANT: Always try the binary dispatcher FIRST
	// It will:
	//  - handle B1/B3 (mTLS handshake / encrypted app frames)
	//  - handle A0/A3 (APPKEY provisioning) when unprovisioned
	//  - fall through (return false) for anything not recognized
	// commands.h
	if( dispatch_binary_frame(b, 3 + len) ) 
	{
		// frame was consumed (B1/B3/A* or a post-decrypt inner frame)
		return;  
	}
*/

	//////////////
	// new processing scheduled for main loop
    // At this point we know we have a full framed message [OP][LENle][PAYLOAD]
    const size_t frameLen = 3 + len;

    // Queue the frame for processing in loop(); drop if queue is busy
    if( !g_framePending ) 
	{
		
        if( frameLen <= MAX_RX_MESSAGE_LENGTH ) 
		{
			taskENTER_CRITICAL(&g_frameMux);
			
            memcpy((void*)g_frameBuf, b, frameLen);
            g_frameLen     = frameLen;
            g_framePending = true;
			
			taskEXIT_CRITICAL(&g_frameMux);
			
        } else 
		{
            // too big – send error
            const char* e = "too big";
            sendFrame(0xFF, (const uint8_t*)e, strlen(e));
        }
		
		
    } else 
	{
        // queue busy – optional: send back a "busy" error
        const char* e = "busy";
        sendFrame(0xFF, (const uint8_t*)e, strlen(e));
    }

    // Return quickly; actual processing happens in loop()
	return;

/* moved this section in the main look
	// Legacy plaintext path below (only reached if not a recognized binary frame)

	// Small helper for LOCKED UI
	auto locked_ui = [&]()
	{
		displayStatus("LOCKED", TFT_RED, true);
		g_blinkLedScheduled = true;
	};

	// If we’re unprovisioned and hit here, it means the frame wasn’t A0/A3 (these are handled above).
	if( !isAppKeyMarkedSet() ) 
	{
		locked_ui();
		const char* e="need APPKEY";
		sendFrame( 0xFF,(const uint8_t*)e,strlen(e) );
		return;
	}

	// Provisioned but not an mTLS frame - plaintext is not allowed anymore.
	locked_ui();
	const char* e="need MTLS";
	sendFrame( 0xFF,(const uint8_t*)e,strlen(e) );
	return;
*/

}

////////////////////////////////////////////////////////////////////
// scheduleHello(delayMs)
//
// Schedules an initial "hello" to the connected app once notifications have
// been subscribed on the TX characteristic.
//
// Two modes:
//
//   (1) Unprovisioned (no AppKey in NVS):
//       - Build a short ASCII info banner:
//             "LAYOUT=<SHORT>; PROTO=<PROTO_VER>; FW=<FW_VER>"
//         using the current keyboard layout, protocol version, and firmware
//         version from settings.h. 
//       - Store it in g_helloPayload and set g_pendingHello/g_helloDueAtMs.
//       - The main loop will later send it via sendTX() once the link is
//         encrypted.
//
//   (2) Provisioned (AppKey already set):
//       - Do *not* send any plaintext banner.
//       - Immediately kick off the binary MTLS handshake by calling
//         mtls_sendHello_B0(), which sends a B0 HELLO with the server
//         ephemeral ECDH key. :contentReference[oaicite:5]{index=5}
//
// This indirection keeps NimBLE callbacks simple and lets loop() decide the
// exact timing of the first notify.
////////////////////////////////////////////////////////////////////
static void scheduleHello( uint32_t delayMs = 80 )
{
	DPRINT("[HELLO] layout=%s, appKeySet=%d\n", layoutName(m_nKeyboardLayout), isAppKeyMarkedSet());

	if( !isAppKeyMarkedSet() ) 
	{
		// (1) Unprovisioned: send ONE plaintext banner
		// Build "LAYOUT=...; PROTO=...; FW=..."
		String line = "LAYOUT=";
		const char* full = layoutName(m_nKeyboardLayout); // "LAYOUT_UK_WINLIN" etc. :contentReference[oaicite:0]{index=0}
		// Strip "LAYOUT_"
		const char* shortName = (strncmp(full, "LAYOUT_", 7)==0) ? (full+7) : full;
		line += shortName;
		line += "; PROTO=";
		line += PROTO_VER;
		line += "; FW=";
		line += FW_VER;

		// Queue to send (same mechanism you used before for CONNECTED=...)
		g_helloPayload  = line.c_str(); 
		g_pendingHello  = true;
		g_helloDueAtMs  = millis() + delayMs;
		
		DPRINTLN("[HELLO] plaintext INFO scheduled (unprovisioned).");
		
		return;
	}

	// (2) Provisioned: DO NOT send any plaintext banner.
	// Immediately start MTLS with B0 (binary hello).
	DPRINTLN("[HELLO] appkey present; skipping plaintext, sending B0...");
	// already implemented in mtls.cpp 
	bool ok = mtls_sendHello_B0(); 
	//delay(2); //debug test
	//DPRINT("[HELLO] mtls_sendHello_B0() -> %s\n", ok ? "OK" : "FAILED");
}

////////////////////////////////////////////////////////////////////
// Called once per loop() to actually send the scheduled hello banner, but
// only when it is safe and appropriate to do so.
//
// Guard conditions:
//   - g_pendingHello      : a banner was scheduled by scheduleHello()
//   - g_isSubscribed      : central has enabled notifications
//   - g_txSubChar != null : we know which characteristic to use
//   - millis() >= g_helloDueAtMs
//
// When all guards pass:
//   - Sends g_helloPayload via sendTX(), which itself enforces that the BLE
//     link is encrypted+authenticated.
//   - Clears g_pendingHello on success.
//
// For provisioned devices there is no plaintext banner; MTLS is started
// directly and this function becomes a no-op.
////////////////////////////////////////////////////////////////////
static void flushHelloIfDue() 
{
    if( !g_pendingHello || !g_isSubscribed || g_txSubChar == nullptr ) return;
	
    // NEW: don’t notify until the link is encrypted - done in sendTX
    //if( !g_linkEncrypted ) return;	
	
    if( (int32_t)(millis() - g_helloDueAtMs) < 0 ) return;

	// send with check for enc connection
	bool ok = sendTX( (const uint8_t*)g_helloPayload.data(), g_helloPayload.size() );

//    g_txSubChar->setValue((const uint8_t*)g_helloPayload.data(), g_helloPayload.size());

    // Send to the specific connection that subscribed
//    bool ok = g_txSubChar->notify(g_txConnHandle);
    //DPRINT("[HELLO] notify(conn=%u,char=%u) -> %s\n",
    //              g_txConnHandle, g_txSubChar->getHandle(), ok ? "OK" : "FAIL");

    if( ok ) g_pendingHello = false;
}

////////////////////////////////////////////////////////////////////
// bleGapEvent(event, arg)
//
// Custom NimBLE GAP handler used to enforce pairing policy and keep the
// controller's accept list (whitelist) in sync with NVS settings. :contentReference[oaicite:6]{index=6}
//
// Handles a few key events:
//
//   - BLE_GAP_EVENT_CONNECT
//       - (legacy logic commented out) could reject connections from
//         unbonded centrals when pairing is locked.
//       - Today, most of the security policy is enforced via the advertising
//         filter in applyAdvertisePolicyOnBoot() and ServerCallbacks.
//
//   - BLE_GAP_EVENT_DISCONNECT
//       - Restarts advertising with the same scan/connect filter policy,
//         respecting the persistent "allow pairing" flag from settings.
//
//   - BLE_GAP_EVENT_ENC_CHANGE (old code commented out)
//       - Link-encryption status is now tracked via the higher-level
//         ServerCallbacks::onAuthenticationComplete() path, so this event
//         is mostly left in as reference.
//
// Any events we do not explicitly care about fall through to the default
// case and are ignored.
////////////////////////////////////////////////////////////////////
static int bleGapEvent( struct ble_gap_event *event, void * /*arg*/ ) 
{
	DPRINT("[GAP] event type=%d\n", (int)event->type);
	
	// intercept by event type 
	switch( event->type ) 
	{
		case BLE_GAP_EVENT_CONNECT:
		{
			// used for debug purposes
            ble_gap_conn_desc d{};
            int rc = ble_gap_conn_find(event->connect.conn_handle, &d);

            DPRINT("[GAP] CONNECT: status=%d conn_handle=%u rc=%d\n",
                   event->connect.status,
                   event->connect.conn_handle,
                   rc);
			
			// If device is locked (pairing closed) and central is NOT bonded, disconnect immediately.
			/* - do not do this on connect! to clean//ble_gap_conn_desc d{};
			if (ble_gap_conn_find(event->connect.conn_handle, &d) == 0) 
			{
				if (!getAllowPairing() && !d.sec_state.bonded) 
				{
					ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
					return 0;
				}
			}*/
			break;
		}
		
		// this seems to be the case when pairing
		case BLE_GAP_EVENT_CONN_UPDATE: 
		{			
			// this is called when mtu changes
			// showPin(g_bootPasskey);
			
			break;
		}
		
		case BLE_GAP_EVENT_ENC_CHANGE: 
		{
            DPRINT("[GAP] ENC_CHANGE: status=%d conn_handle=%u\n",
                   event->enc_change.status,
                   event->enc_change.conn_handle);
				   
			g_encReady = (event->enc_change.status == 0);
			g_linkEncrypted = (event->enc_change.status == 0);

			if( g_encReady ) 
			{
				cancelPin();
				g_pinAllowedThisConn = false;

				// LOCK after the very first successful bonded link when pairing is open:
				ble_gap_conn_desc d{};
				if( ble_gap_conn_find(event->enc_change.conn_handle, &d) == 0 ) 
				{
					//if( d.sec_state.bonded && getAllowPairing() ) 
					// todo: see how can I use a whitelist enforcement for multiple devices here - just a thought 
					if( d.sec_state.bonded && getAllowPairing() && !getAllowMultiDevicePairing() ) 
					{
						// Persist lock
						setAllowPairing(false);  // -> writes NVS via settings.h
		
						// Whitelist this peer's identity and switch to whitelist-only connections
						//NimBLEDevice::whiteListAdd(NimBLEAddress(d.peer_id_addr));
						ble_addr_t only = d.peer_id_addr;
						ble_gap_wl_set(&only, 1);
						
						auto* adv = NimBLEDevice::getAdvertising();
						adv->setScanFilter(false /*scan-any*/, true /*connect-whitelist-only*/);
						adv->stop();
						adv->start();
					}
				}
			}
			break;
		}		

		case BLE_GAP_EVENT_DISCONNECT:
		{
            DPRINT("[GAP] DISCONNECT: reason=%d conn_handle=%u\n",
                   event->disconnect.reason,
                   event->disconnect.conn.conn_handle);
				   
			auto* adv = NimBLEDevice::getAdvertising();
			adv->setScanFilter(false /*scan-any*/, getAllowPairing() ? false : true /*connect-whitelist-only*/);
			adv->start();
			break;
		}
		
		/* old to remove??
		case BLE_GAP_EVENT_ENC_CHANGE: {
		  // status==0 => link is encrypted (bond was present or pairing just finished)
		  g_encReady = (event->enc_change.status == 0);
		  
		  // also set this - check duplaicate flag g_encReady - g_linkEncrypted?
		  g_linkEncrypted = (event->enc_change.status == 0);
		  
		  if( g_encReady ) cancelPin();
		  break;
		}*/		
		
		/*case BLE_GAP_EVENT_PASSKEY_ACTION: 
		{
		  // If NimBLE does request a display action, show immediately and skip the delay
		  if( event->passkey.action == BLE_SM_IOACT_DISP ||
			  event->passkey.action == BLE_SM_IOACT_NUMCMP ) 
		  {
			showPin(event->passkey.passkey);
			g_pinShowScheduled = false;
		  }
		  break;
		}*/

/*		// try to catch on connect and send back the connect echo
		case BLE_GAP_EVENT_SUBSCRIBE: 
		{
			//do not know what 8 is
			if( event->subscribe.attr_handle == 8 ) sendConnectedEcho();
				
			break;
		}
*/
		// defaults
		default: 
		{
			// For all other events 
			break;
		}
	}


  return 0;
}

////////////////////////////////////////////////////////////////////
// writer callback class
////////////////////////////////////////////////////////////////////
class WriteCallback : public NimBLECharacteristicCallbacks 
{
	
public:
  void onWrite(NimBLECharacteristic* chr) { handleWrite(chr->getValue()); }
  void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo& info) override { (void)info; handleWrite(chr->getValue()); }
  
	void onSubscribe(NimBLECharacteristic* c, NimBLEConnInfo& info, uint16_t subValue) override 
	{
		const bool notifyOn   = (subValue & 0x0001);
		const bool indicateOn = (subValue & 0x0002);
		g_isSubscribed = (notifyOn || indicateOn);

		DPRINT("SUBSCRIBE: conn=%u attr=%u sub=0x%04x (notify=%d,indicate=%d) UUID=%s\n",
		              info.getConnHandle(), c->getHandle(), subValue, notifyOn, indicateOn,
		              c->getUUID().toString().c_str());

		if( g_isSubscribed ) 
		{
			// Remember the exact char + connection that is subscribed
			g_txSubChar    = c;
			g_txConnHandle = info.getConnHandle();
			
			// If we’re not yet encrypted, request it now; notifications will wait.
			if( !g_linkEncrypted ) 
			{
				NimBLEDevice::startSecurity(g_txConnHandle);  // idempotent
				// give the stack a moment; we’ll only send after ENC_CHANGE flips the flag
				// todo: maybe better logic here
				scheduleHello(800);
				
			} else 
			{
				scheduleHello(500);
			}			
			
			// old impl
			//scheduleHello(80); // defer actual notify to loop()
			
		} else 
		{
			// Unsubscribed
			g_txSubChar = nullptr;
		}
	} 
  
};

////////////////////////////////////////////////////////////////////
// bluetooth server
// todo: need to check if these callbacks still needed, or just use/move all to bleGapEvent
////////////////////////////////////////////////////////////////////
class ServerCallbacks : public NimBLEServerCallbacks 
{
	// IDF 5.x / Arduino-ESP32 3.x styl
	void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override 
	{
		DPRINT("ServerCallbacks::onConnect connHandle addr\n");

		g_isSubscribed = false;

		setLED(CRGB::Green);
		currentColor = CRGB::Green;

		/*// Reset link flags and start security; show pairing UI + PIN.
		g_linkEncrypted = g_linkAuthenticated = false;
		g_encReady = false;

		// Request 30–50 ms interval, 0 latency, 4 s supervision timeout
		server->updateConnParams(
			info.getConnHandle(),
			24,   // min interval  = 24 * 1.25 ms = 30 ms
			40,   // max interval  = 40 * 1.25 ms = 50 ms
			0,    // latency       = 0
			400   // timeout       = 400 * 10 ms  = 4000 ms
		);

		// Kick off security on every link, but DO NOT show the PIN yet.
		NimBLEDevice::startSecurity(info.getConnHandle());

		// Defer showing the PIN — if bond exists, ENC will succeed quickly and we’ll cancel.
		schedulePinMaybe();*/
		
		// Reset link flags and start security
		g_linkEncrypted      = false;
		g_linkAuthenticated  = false;
		g_encReady           = false;
		g_pinShowScheduled   = false;
		g_pinAllowedThisConn = false;

		// Request 30–50 ms interval, 0 latency, 4 s supervision timeout
		/* disable to see if this works with new phones ? YES! avoid this setting on new phone. proved to cause problems
		server->updateConnParams(
			info.getConnHandle(),
			24,   // min interval  = 24 * 1.25 ms = 30 ms
			40,   // max interval  = 40 * 1.25 ms = 50 ms
			0,    // latency       = 0
			400   // timeout       = 400 * 10 ms  = 4000 ms
		);*/

		NimBLEDevice::startSecurity(info.getConnHandle());

		// Decide if this connection is eligible to show a PIN.
		// We only want PIN for *new* pairing while pairing is open.
		ble_gap_conn_desc d{};
		if( ble_gap_conn_find(info.getConnHandle(), &d) == 0 ) 
		{
			const bool alreadyBonded = d.sec_state.bonded;
			const bool pairingOpen   = getAllowPairing();

			if (!alreadyBonded && pairingOpen) 
			{
				g_pinAllowedThisConn = true;
				schedulePinMaybe();  // arm the delay
			}
			
		} else 
		{
			// Fallback: if pairing is open and we can't query, assume it's OK to show PIN.
			if (getAllowPairing()) 
			{
				g_pinAllowedThisConn = true;
				schedulePinMaybe();
			}
		}				

		//displayStatus("PAIRING...", TFT_YELLOW, true); // should implement this latter ?
		//showPin(g_bootPasskey);
		//    NimBLEDevice::startSecurity(info.getConnHandle());

		// Optional: echo current layout so phone/terminal sees readiness quickly? 
		//sendConnectedEcho();
	}

	void onDisconnect(NimBLEServer* server, NimBLEConnInfo& info, int reason) override 
	{
		DPRINT("ServerCallbacks::onDisconnect reason=%d\n", reason);

		// reset all flags to false - todo: check if more need to reset
		g_isSubscribed = false;	  
		g_linkEncrypted = false;
		g_linkAuthenticated = false;
		g_encReady = false;
		g_pinAllowedThisConn = false; 
		  
		// kill raw fast mode on link drop
		g_rawFastMode = false;		  
		  
		mtls_onDisconnect();
		  
		setLED(CRGB::Black);
		//displayStatus("ADVERTISING", TFT_YELLOW, true);
		//displayStatus("READY", TFT_GREEN, true);
		g_displayReadyScheduled = true;

		g_linkEncrypted = g_linkAuthenticated = false;
		NimBLEDevice::startAdvertising();
	}

	void onAuthenticationComplete(NimBLEConnInfo& info) override 
	{
		DPRINT("ServerCallbacks::onAuthenticationComplete enc=%d auth=%d\n", g_linkEncrypted ? 1:0, g_linkAuthenticated ? 1:0);	  
		  
		// Some NimBLE builds report auth status via ConnInfo
		g_linkEncrypted     = info.isEncrypted();
		g_linkAuthenticated = info.isAuthenticated();

		bool enc  = info.isEncrypted();
		bool auth = info.isAuthenticated();		
		DPRINT("[SRV] onAuthenticationComplete enc=%d auth=%d\n", (int)enc, (int)auth);
		
		// debug
		ble_gap_conn_desc d{};
		int rc = ble_gap_conn_find(info.getConnHandle(), &d);
		if (rc == 0) {
			DPRINT("[SRV] sec_state: bonded=%d enc=%d auth=%d key_size=%d\n",
				   (int)d.sec_state.bonded,
				   (int)d.sec_state.encrypted,      
				   (int)d.sec_state.authenticated,
				   (int)d.sec_state.key_size);
		} else {
			DPRINT("[SRV] sec_state: ble_gap_conn_find rc=%d\n", rc);
		}
	
		if( g_linkEncrypted && g_linkAuthenticated ) 
		{
			// Mark link as fully ready before the PIN timeout in loop()
			g_encReady = true;
		
			cancelPin();
			g_pinAllowedThisConn = false; 
			
			displayStatus("SECURED", TFT_GREEN, true);
			// flag for mainb loop to display ready
			g_displayReadyScheduled = true;

			//delay(500);
			//displayStatus("READY", TFT_GREEN, true);
			// after pairing just to let the sender know is ready - also echo so terminals auto-detect TX
			//sendTX("BLUE_KEYBOARD_SRV_READY\n");
			  
			//scheduleHello();

		} else 
		{
			displayStatus("PAIR FAIL", TFT_RED, true);
		}
	}
  
};

////////////////////////////////////////////////////////////////////
// pollResetButtonLongPress()
//
// Non-blocking polling logic for the physical BOOT / reset button.
//
// Behaviour:
//   - Treats BTN_PIN as active-low input with an internal pull-up.
//   - Tracks "press start" time whenever the button transitions from up-down.
//   - If the button is held for ≥ 3 seconds and not yet consumed, calls
//     factoryReset() exactly once (per press).
//   - Resets all internal state when the button is released.
//
// This is called from loop() on every iteration; there are no delays inside,
// so it does not block other work.
////////////////////////////////////////////////////////////////////
void pollResetButtonLongPress() 
{
	const bool btnDown = (digitalRead(BTN_PIN) == LOW); // active-low
	const uint32_t now = millis();

	// 1) Fresh press: mark start time
	if( btnDown && !g_btnWasDown ) 
	{
		g_btnWasDown = true;
		g_pressStartMs = now;
		g_longPressConsumed = false;
	}

	// 2) Held: if 3s reached and not yet consumed -> RESET
	if( btnDown && g_btnWasDown && !g_longPressConsumed ) 
	{
		// 3 seconds
		if( now - g_pressStartMs >= 3000 ) 
		{
			g_longPressConsumed = true;		// one-shot
			factoryReset();					// wipe bonds + unlock pairing
		}
	}

	// 3) Released: clean up state
	if( !btnDown && g_btnWasDown ) 
	{
		g_btnWasDown = false;
		g_pressStartMs = 0;
		g_longPressConsumed = false;
	}
}

////////////////////////////////////////////////////////////////////
// factoryReset()
//
// Full "security factory reset" triggered by a long press on BTN_PIN.
//
// Steps:
//   1) Show a red "RESET" banner on the TFT.
//   2) Delete all BLE bonds from the NimBLE stack.
//   3) Force pairing unlocked:
//        - setAllowPairing(true) so the next boot will advertise as open.
//   4) Wipe AppKey + setup flags from NVS via clearAppKeyAndFlag(), which
//      also removes the web-setup KDF parameters and BLE name overrides. :contentReference[oaicite:7]{index=7}
//   5) Clear the controller accept list (whitelist) and restart advertising
//      in "connect-any" mode.
//   6) Delay ~1.5s so the user can see the message, then reboot the ESP.
//
// After reboot the device behaves as a brand-new dongle and will bring up
// the Wi-Fi setup portal again on first run.
////////////////////////////////////////////////////////////////////
void factoryReset() 
{
	displayStatus("RESET", TFT_RED, true);

	NimBLEDevice::deleteAllBonds();
	setAllowPairing(true);
	// clear app key
	clearAppKeyAndFlag();

	// Clear accept list (whitelist) at the controller level
	ble_gap_wl_set(nullptr, 0);  

	auto* adv = NimBLEDevice::getAdvertising();
	adv->setScanFilter(false, false);
	adv->stop();
	adv->start();

	delay(1500);        // let the user see "RESET"
	esp_restart();
}

////////////////////////////////////////////////////////////////////
// Central place for enforcing the "lock pairing after setup" policy at the
// controller / advertising level.
//
// Reads the persistent flag from NVS via getAllowPairing():
//
//   - If pairing is allowed (unlocked):
//       - Clear the controller accept-list (ble_gap_wl_set(nullptr, 0)).
//       - Advertise with -connect-any- policy so *any* central can pair.
//
//   - If pairing is locked:
//       - Query NimBLE for all stored bonds (ble_store_util_bonded()).
//       - Populate a temporary array of peer identities and install it as the
//         controller accept-list via ble_gap_wl_set(..., count).
//       - Configure advertising with -connect-whitelist-only- so only already
//         bonded centrals can connect.
//
// This function is called once from setup() after NimBLE initialisation and
// before advertising is started.
////////////////////////////////////////////////////////////////////
static void applyAdvertisePolicyOnBoot() 
{
	NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();

	// Read persisting flag
	const bool locked = !getAllowPairing();

	if( !locked ) 
	{
		// Fresh device or pairing unlocked -> connect-any
		// clear controller accept list
		ble_gap_wl_set(nullptr, 0);               
		adv->setScanFilter(false /*scan-any*/, false /*connect-any*/);
		
		return;
	}

	// Pairing locked: rebuild accept list
	ble_addr_t addrs[8];
	const int capacity = (int)(sizeof(addrs) / sizeof(addrs[0]));
	int count = capacity;
	if( ble_store_util_bonded_peers(addrs, &count, capacity) != 0 ) count = 0;

	if( count <= 0 ) 
	{
		// Self-heal: lock says "no new pairs", but there are no bonds -> unlock
		setAllowPairing(true);
		ble_gap_wl_set(nullptr, 0);
		adv->setScanFilter(false, false);
		
		return;
	}

	// Populate accept list
	//ble_gap_wl_set(nullptr, 0);
	//for (int i = 0; i < count; ++i) {
	//  NimBLEAddress id(addrs[i].val, addrs[i].type);
	//  NimBLEDevice::whiteListAdd(id);
	//}
	// Atomically set controller accept-list from stored bonds
	ble_gap_wl_set(addrs, count);

	adv->setScanFilter(false /*scan-any*/, true /*connect-whitelist-only*/);
}

////////////////////////////////////////////////////////////////////
// One-time hardware + stack initialisation:
//
//   - Configures BOOT / reset button and on-board APA102 LED.
//   - Brings up the TFT and draws an initial splash/READY state.
//   - Opens NVS and initialises settings (BLE name, layout, AppKey / KDF,
//     pairing flag) via initSettings(). 
//   - If the "first-run" flag is not set, runs the Wi-Fi setup portal to
//     collect BLE name, layout, and a one-time password, then derives and
//     stores KDF params and AppKey. 
//   - Initialises USB HID (RawKeyboard), TinyUSB device descriptors, and BLE
//     (NimBLEDevice) with the chosen name.
//   - Sets up security parameters (bonding + MITM + LE SC), GAP handler,
//     server/service/characteristics for the NUS-style service.
//   - Applies the advertising policy (locked vs unlocked pairing) and starts
//     advertising.
//
// After setup() returns, loop() drives UI, MTLS retries, and BLE housekeeping.
////////////////////////////////////////////////////////////////////
void setup() 
{
	// BOOT/BTN: active-low
	pinMode(BTN_PIN, INPUT_PULLUP);   
	
	// LEDs
	FastLED.addLeds<APA102, LED_DI, LED_CI, BGR>(led, 1);
	setLED(CRGB::Black);

	pinMode(TFT_LEDA_PIN, OUTPUT);
	tft.init();
	tft.setSwapBytes(true);
	tft.setRotation(1);
	tft.fillScreen(TFT_BLACK);
	digitalWrite(TFT_LEDA_PIN, 0);
	drawReady();

	// load prefs from persisten storage
	initSettings();

	/* extr debug if needed
    // DEBUG: dump pairing policy + bonds at boot
    {
        bool allowPair = getAllowPairing();
        bool allowMultiDev = getAllowMultiDevicePairing();
        bool allowMultiApp = getAllowMultiAppProvisioning(); // if you have this accessor

        ble_addr_t addrs[8];
        int capacity = (int)(sizeof(addrs) / sizeof(addrs[0]));
        int count = capacity;
        int rc = ble_store_util_bonded_peers(addrs, &count, capacity);

        DPRINT("[BOOT][BLE] allowPair=%d allowMultiDev=%d allowMultiApp=%d rc=%d bonds=%d\n",
               (int)allowPair, (int)allowMultiDev, (int)allowMultiApp, rc, count);

        for (int i = 0; i < count; ++i) {
            ble_addr_t &a = addrs[i];
            DPRINT("[BOOT][BLE] bond[%d] type=%d addr=%02X:%02X:%02X:%02X:%02X:%02X\n", i, a.type,
                   a.val[5], a.val[4], a.val[3], a.val[2], a.val[1], a.val[0]);
        }
    }
	*/

	// set BLE name - also used in setup for default
	//initBleNameGlobal();

	// if key was not setup, spin a webserver so you can setup over wifi softap
	if( !isSetupDone() ) 
	{
		// light the dongle blue for the setup mode
		setLED(CRGB::Blue);
		
		// First-run: only bring up the setup portal (no BLE yet)
		runSetupPortal();
		// After portal completes, we reboot (portal triggers /reboot), but in case we return:
		if( !isSetupDone() ) { ESP.restart(); return; }		
	}

#if (!DEBUG_GLOBAL_DISABLED && DEBUG_ENABLED)
	// FOR DEBUG	
	Serial.begin(115200);
	delay(50);                 // tiny pause to allow serial to come up
	DPRINTLN("\n=== BLUE_KEYBOARD boot ===");
	
    esp_reset_reason_t reason = esp_reset_reason();
    Serial.printf("\n=== BLUE_KEYBOARD boot ===\n[BOOT] reset reason = %d\n", (int)reason);	
	
	// USB HID first  
	USB.begin();
	delay(200);
	Keyboard.begin();
	
#else
	// USB HID first  
	USB.begin();
	delay(200);
	Keyboard.begin();
	MediaControl.begin();    // Consumer Control (media keys)
	
#endif
	// just to clean up - comment after
	//NimBLEDevice::deleteAllBonds();

	// ===== NimBLE secure setup =====
	NimBLEDevice::init( g_BleName.c_str() );
	//NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
	NimBLEDevice::setDeviceName( g_BleName.c_str() );
	
	NimBLEDevice::setPower(ESP_PWR_LVL_P9);
	// disable to try to make it work with new phones - or keep it as a hint
	//NimBLEDevice::setMTU(247);

	// Register the GAP handler *right after* init and before advertising
	if( !NimBLEDevice::setCustomGapHandler(bleGapEvent) ) 
	{
	  //displayStatus("GAP FAIL", TFT_RED, true);
	} else 
	{
	  //displayStatus("GAP GOOD", TFT_GREEN, true);
	}

	// Generate a random 6-digit PIN per boot and set it as the passkey
	g_bootPasskey = (esp_random()%900000UL)+100000UL;
	NimBLEDevice::setSecurityPasskey( g_bootPasskey );

	// Bonding + MITM + LE Secure Connections - SC does not seem to work in esp32 2.0.14. now fixed with 3.3.x
	NimBLEDevice::setSecurityAuth(/*bond*/true, /*MITM*/true, /*SC*/true);

	// force show password
	NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
	//NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO); 

	// new add
	NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
	NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
	// Register the GAP handler 

	// not available in 3.3.x
	//static SecCallbacks secCbs;
	//NimBLEDevice::setSecurityCallbacks(&secCbs);
	///////

	NimBLEServer* pServer = NimBLEDevice::createServer();
	pServer->setCallbacks(new ServerCallbacks()); 

	NimBLEService* pService = pServer->createService(SERVICE_UUID);

	// TX = Notify to device connecting 
	g_txChar = pService->createCharacteristic( CHAR_NOTIFY_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ_ENC);

	// ADD THIS so onSubscribe fires for the notify char:
	g_txChar->setCallbacks(new WriteCallback());

	// read from device connecting
	pWriteChar = pService->createCharacteristic( CHAR_WRITE_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC );
	pWriteChar->setCallbacks(new WriteCallback());
	pService->start();

	// Advertising
	NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
	//pAdv->setScanFilter(false /*scan-any*/, getAllowPairing() ? false : true /*connect-whitelist-only*/);
	NimBLEAdvertisementData advData;
	advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
	//advData.setAppearance(0x03C1);
	NimBLEAdvertisementData scanData;
	scanData.setName( g_BleName.c_str() );
	scanData.setCompleteServices(NimBLEUUID(SERVICE_UUID));

	pAdv->setAdvertisementData(advData);
	pAdv->setScanResponseData(scanData);
	pAdv->addServiceUUID(SERVICE_UUID);   // helps terminals auto-detect NUS
	pAdv->setMinInterval(160);
	pAdv->setMaxInterval(320);

	// place strict policy here. only advertise if not paired already
	applyAdvertisePolicyOnBoot();

	pAdv->start();

	//displayStatus("ADVERTISING", TFT_YELLOW, true);
}

////////////////////////////////////////////////////////////////////
// Main firmware event pump. Runs as fast as possible and keeps all
// time-based behaviour non-blocking (except for the 1s LED blink).
//
// Per-iteration tasks:
//   1) pollResetButtonLongPress()
//       - Check for a 3s long-press on BTN_PIN to trigger factoryReset().
//
//   2) flushHelloIfDue()
//       - If a hello banner was scheduled on subscription, send it once
//         the link is ready.
//
//   3) mtls_tick()
//       - Drive B0 retry logic for the MTLS handshake until a session
//         becomes active. 
//
//   4) LED / UI scheduling:
//       - If g_blinkLedScheduled is set - perform a 1s red blink.
//       - Else, if g_pinShowScheduled and encryption is still not up by
//         g_pinDueAtMs - show the 6-digit pairing PIN on the TFT.
//       - Else, if g_displayReadyScheduled - draw the READY UI.
//       - Else - draw a small heartbeat dot in the top-right corner.
//
// BLE callbacks, MTLS, and the command dispatcher never block; all "slow"
// user-visible effects are deferred into this loop via simple flags.
////////////////////////////////////////////////////////////////////
void loop() 
{
	// check for factory reset
	pollResetButtonLongPress();
	
	// just to notify on connect
	flushHelloIfDue();

	////////////////////
	// :: moved from handleWrite
    // Process any pending BLE frame outside of NimBLE callbacks
    if( g_framePending ) 
	{
        // avoid races with onWrite()
        noInterrupts();
		
		taskENTER_CRITICAL(&g_frameMux);
		
        size_t len = g_frameLen;
        static uint8_t localBuf[MAX_RX_MESSAGE_LENGTH];
        memcpy(localBuf, g_frameBuf, len);
        g_framePending = false;
		
		taskEXIT_CRITICAL(&g_frameMux);
		
        interrupts();

        // First try the binary dispatcher (MTLS + commands)
        if( dispatch_binary_frame(localBuf, len) ) 
		{
            // frame was consumed (B1/B3/A* or a post-decrypt inner frame)
			
        } else 
		{
            // Legacy plaintext path (your previous logic from handleWrite)

            // Small helper for LOCKED UI
            auto locked_ui = [&]() {
                displayStatus("LOCKED", TFT_RED, true);
                g_blinkLedScheduled = true;
            };

            // If we’re unprovisioned and hit here, it means the frame wasn’t A0/A3 (these are handled above).
            if( !isAppKeyMarkedSet() ) {
                locked_ui();
                const char* e="need APPKEY";
                sendFrame(0xFF,(const uint8_t*)e,strlen(e));
            } else {
                // Provisioned but not an mTLS frame - plaintext is not allowed anymore.
                locked_ui();
                const char* e="need MTLS";
                sendFrame(0xFF,(const uint8_t*)e,strlen(e));
            }
        }
    }
	
	// drives B0 retries while not active	
	mtls_tick();    
	
	// :: led cmds
	if( g_blinkLedScheduled )
	{
		// this a delay 1s in it. could do better in two steps and not block
		blinkLed();
		g_blinkLedScheduled = false;
	}
	
	// show PIN only if encryption did NOT come up quickly (like true pairing)
	//if( g_pinShowScheduled && !g_encReady && (int32_t)(millis() - g_pinDueAtMs) >= 0) 
	if( g_pinAllowedThisConn &&
		g_pinShowScheduled &&
		!g_encReady &&
		(int32_t)(millis() - g_pinDueAtMs) >= 0)
	{
		// confident this is an actual pairing
		showPin(g_bootPasskey);              
		g_pinShowScheduled = false;          
		
	} else if( g_displayReadyScheduled )	
	{
		drawReady();
		g_displayReadyScheduled = false;
		
	// :: else draw heartbeat dot on the screen
	} else
	{
		// blink dot on screen every second - you can disable this if not needed
		static uint32_t last = 0;
		if( millis() - last > 1000 ) 
		{
			last = millis();
			static bool on = false;
			uint16_t c = on ? TFT_RED : TFT_BLACK;
			tft.fillRect(tft.width()-10, 2, 6, 6, c);
			on = !on;
		}
	}
}
