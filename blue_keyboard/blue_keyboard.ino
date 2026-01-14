////////////////////////////////////////////////////////////////////
// blue_keyboard.ino (v2.1.0)  - Larry Lart
//
// ESP32-S3 BLE -> USB HID dongle.
// - Optional TFT + status LED
// - Wi-Fi setup portal on first boot (name/layout/AppKey)
// - BLE pairing policy + whitelist/lock after first bond (optional)
// - MTLS session on top of BLE (see mtls.cpp) + binary commands (commands.h)
////////////////////////////////////////////////////////////////////
#include <Arduino.h>
#include <NimBLEDevice.h>
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

#if !NO_LED	&& !NO_RGB_LED
#include <FastLED.h>
#endif

#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDConsumerControl.h"

// include locals first for defines
#include "settings.h"
#include "pin_config.h"

// case for no display dongle
#if !NO_DISPLAY
#include "TFT_eSPI.h" 
#endif


/////////////////////////////
// *** DEBUG ***
#define DEBUG_ENABLED 1
#include "debug_utils.h"
/////////////////////////////

// locals
#include "mtls.h"
#include "RawKeyboard.h"
#include "layout_kb_profiles.h"
#include "commands.h"
#include "setup_portal.h"

// defines
#define MAX_RX_MESSAGE_LENGTH 4096
// Onboard APA102 pins (T-Dongle-S3) - get thsi from pins file instead LED_DI_PIN etc
//#define LED_DI 40
//#define LED_CI 39
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

#if !NO_DISPLAY
TFT_eSPI tft = TFT_eSPI();
#endif

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
// have we called mtls_sendHello_B0() for this connection?
static volatile bool g_mtlsHelloSeeded = false;  

// Link security state (BLE layer)
static volatile bool g_linkEncrypted = false;			 // controller link encrypted
static volatile bool g_linkAuthenticated = false;		// MITM/auth complete
static uint32_t g_bootPasskey = 0;						// 6-digit passkey (NVS)

// PIN UI gating: show passkey only for real "new pairing" attempts
static volatile bool g_encReady = false;            // set when we consider link "ready"
static volatile bool g_pinShowScheduled = false;   // PIN timer armed
static volatile bool g_pinAllowedThisConn = false;   // only for non-bonded + pairing-open
static uint32_t g_pinDueAtMs = 0;
static const uint32_t PIN_DELAY_MS = 600;          // small pin display delay

///////////////////////////////////
static volatile bool g_isSubscribed = false;
bool g_notificationsEnabled = false;

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
// Set the single on-board LED.
////////////////////////////////////////////////////////////////////
static inline void setLED(const CRGB& c) 
{
#if !NO_LED	&& NO_RGB_LED

// Map any non-black color to "ON".
const bool on = (c.r | c.g | c.b) != 0;
digitalWrite(LED_DI_PIN, on ? LOW : HIGH);

#else	
		led[0] = c;
	#if !NO_LED	
		FastLED.show();
	#endif

#endif

}

////////////////////////////////////////////////////////////////////
// Brief red blink (blocks ~80ms). Used as a send string heart beat indicator.
// NOTE: Uses delay() and therefore blocks loop() briefly - this is blocking to change on main loop
////////////////////////////////////////////////////////////////////
void blinkLed() 
{
#if !NO_LED	&& NO_RGB_LED	
	
    // Blink = turn LED on briefly then restore "currentColor"
    digitalWrite(LED_DI_PIN, LOW);   // ON (active-low)
    delay(80);
    const bool restoreOn = (currentColor.r | currentColor.g | currentColor.b) != 0;
    digitalWrite(LED_DI_PIN, restoreOn ? LOW : HIGH);
	
#else
	
	led[0] = CRGB::Red;
#if !NO_LED		
	FastLED.show();
#endif	
	// short blink - todo: should probably do a non blocking blink
	delay(80);
	
	led[0] = currentColor;
#if !NO_LED		
	FastLED.show();
#endif

#endif
}

////////////////////////////////////////////////////////////////////
static bool isLinkSecureForTraffic( uint16_t connHandle )
{
    //if( !g_linkEncrypted ) return false;

    // Treat bonded as good enough even if authenticated flag isn't set.
    ble_gap_conn_desc d{};
    if( ble_gap_conn_find(connHandle, &d) == 0 ) 
	{
        return( d.sec_state.encrypted && (d.sec_state.authenticated || d.sec_state.bonded) );
    }

    // Fallback if we can't query: keep current strict behavior
    return( g_linkEncrypted && g_linkAuthenticated );
}

////////////////////////////////////////////////////////////////////
// BLE TX helper (notifications), chunks to (MTU-3).
// - Handshake ops (B0/B1/B2/D1) may send before link is fully secure.
// - All other ops require encrypted + (authenticated OR bonded).
// - Retries notify() up to 3 times per chunk.
////////////////////////////////////////////////////////////////////
bool sendTX(const uint8_t* data, size_t len)
{
	if( !g_txChar ) return( false );
	
	// Guard: do not send anything over an unencrypted or unauthenticated link.
	//if( !g_linkEncrypted || !g_linkAuthenticated ) return( false );

	// If we have a conn handle (you track g_txConnHandle when subscribed)
//	if (!isLinkSecureForTraffic(g_txConnHandle)) return false;

    uint8_t op = data[0];
    // Handshake opcodes (B0, B1, B2) MUST bypass the BLE link security check - might not be a good idea 
	// this because darn iphone ble encryption cannot keep it straight - might have to do something about this !!! TODO!
    bool isHandshake = (op == 0xB0 || op == 0xB1 || op == 0xB2 || op == 0xD1);

    // Only non-handshake frames require a secure link
    if( !isHandshake && !isLinkSecureForTraffic(g_txConnHandle) ) 
	{
        DPRINTLN("[TX] Blocking, link not secure.");
        return false;
    }

#ifdef NIMBLE_CPP
	if( g_txChar->getSubscribedCount() == 0 ) return( false );
#endif

	// ATT_MTU includes 3-byte header -> max notify payload = MTU - 3
	uint16_t mtu = NimBLEDevice::getMTU();
	if( mtu < 23 ) mtu = 23;
	const uint16_t maxPayload = mtu - 3;

	//DPRINT("[SENDTX] data=%s, mtu=%d max_payload=%d\n", data, mtu, maxPayload );

	// retry if failing
	size_t off = 0;
    while( off < len ) 
    {
        size_t n = len - off;
        if( n > maxPayload ) n = maxPayload;
        
        g_txChar->setValue( reinterpret_cast<const uint8_t*>(data + off), n );

        // 3. RETRY LOGIC
        // We try up to 3 times with increasing delays.
        bool sent = false;
        for(int i=0; i<3; i++) 
		{
            #if defined(ARDUINO_ARCH_ESP32)
                if( g_txChar->notify() ) 
				{
                    sent = true;
                    break;
                }
            #else
                g_txChar->notify();
                sent = true; 
                break;
            #endif
            
            // If failed, wait a bit for the next connection interval to clear buffers
            //delay(10 + (i*10)); 
			delay(2 + (i*2)); 
        }

        if( !sent ) 
		{
             DPRINTLN("[TX] FAILED to notify chunk - aborting");
             return false; 
        }

        off += n;
        // Small yield between chunks is still good practice was 2/use 10
        delay(0); // - do I need this ? used to be 10
    }
    return( true );

}

////////////////////////////////////////////////////////////////////
// Draw a centered status string on TFT.
// Clears only the previous text area to avoid full-screen flicker.
////////////////////////////////////////////////////////////////////
void displayStatus( const String& msg, uint16_t color, bool big = true ) 
{
#if NO_DISPLAY
    (void)msg; (void)color; (void)big;
    // No display attached: do nothing (LED feedback still works elsewhere).
#else	
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
#endif	
}

// UI helper used by provisioning code in commands.h
// Shows a LOCKED message and triggers a red LED blink in the main loop.
void showLockedNeedsReset()
{
#if !NO_DISPLAY	
    displayStatus("LOCKED", TFT_RED, true);
#endif
	
    g_blinkLedScheduled = true;
}

////////////////////////////////////////////////////////////////////
// Show BLE passkey on TFT (only when PIN gating says it's a real pairing flow).
////////////////////////////////////////////////////////////////////
static inline void showPin(uint32_t pin) 
{
#if NO_DISPLAY
    (void)pin;
#else	
	char buf[32];
	snprintf( buf, sizeof(buf), "PIN: %06lu", (unsigned long) pin );
	//displayStatus(buf, TFT_YELLOW, /*big*/ true);
	displayStatus(buf, TFT_BLUE, /*big*/ true);
#endif	
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
// READY banner (called from loop via flag).
////////////////////////////////////////////////////////////////////
void drawReady() 
{ 
#if !NO_DISPLAY
	displayStatus("READY", TFT_GREEN, true); 
#endif
}

////////////////////////////////////////////////////////////////////
// Update RECV counter on TFT. Only used for secure/strings
////////////////////////////////////////////////////////////////////
void drawRecv() 
{
#if NO_DISPLAY
    // nothing
#else	
	char buf[32];
	snprintf(buf, sizeof(buf), "RECV: %lu", (unsigned long)recvCount);
	displayStatus(buf, TFT_WHITE, true);
#endif	
}

////////////////////////////////////////////////////////////////////
// Called after SEND_STRING has been typed; bumps counter + blinks LED.
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
// BLE RX handler (Write callback).
// Validates [OP][LENle][PAYLOAD] then queues one frame for loop() to process.
// Keeps NimBLE callback path short.
////////////////////////////////////////////////////////////////////
void handleWrite( const std::string& val_in ) 
{	
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

}

////////////////////////////////////////////////////////////////////
// Low-level GAP hook (policy + debug).
// - ENC_CHANGE: update flags, lock pairing after first bond (optional), handle fail.
// - DISCONNECT: restart advertising using current pairing policy.
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
				
			} else
			{
				// ENC failed -> clean state and disconnect so pairing can retry without unplug
				g_encReady = false;
				g_linkEncrypted = false;
				g_linkAuthenticated = false;

				cancelPin();
				g_pinAllowedThisConn = false;

#if !NO_DISPLAY
				displayStatus("PAIR FAIL", TFT_RED, true);
#endif
				g_blinkLedScheduled = true;

				ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);				
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

		// Check if the subscription is for the RX Characteristic
		//if( notifyOn ) { g_notificationsEnabled = true;  }
        if( c->getUUID().equals(NimBLEUUID(CHAR_NOTIFY_UUID)) ) 
		{
			g_notificationsEnabled = notifyOn;
        }

		if( g_isSubscribed ) 
		{
			// Remember the exact char + connection that is subscribed
			g_txSubChar    = c;
			g_txConnHandle = info.getConnHandle();

			if( isLinkSecureForTraffic(g_txConnHandle) ) 
			{
				mtls_sendHello_B0();          // seeds cached B0 + retry timer (does not send immediately)
				g_mtlsHelloSeeded = true;
			} else 
			{
				// na
			}
			
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
		g_mtlsHelloSeeded = false;

		//setLED(CRGB::Green);
		//currentColor = CRGB::Green;
		// red-orange = connected but not paired yet
		setLED(CRGB(255, 64, 0));     
		currentColor = CRGB(255, 64, 0);		
		
		// Reset link flags and start security
		g_linkEncrypted      = false;
		g_linkAuthenticated  = false;
		g_encReady           = false;
		g_pinShowScheduled   = false;
		g_pinAllowedThisConn = false;

		// Decide if this connection is eligible to show a PIN.
		// We only want PIN for *new* pairing while pairing is open.
		ble_gap_conn_desc d{};
		if( ble_gap_conn_find(info.getConnHandle(), &d) == 0 ) 
		{
			// gate start security to only if needed
			//if (!d.sec_state.encrypted) 
			if (!d.sec_state.bonded || !d.sec_state.encrypted)				
			{
				NimBLEDevice::startSecurity(info.getConnHandle());
			}			
			
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
		g_mtlsHelloSeeded = false;
		  
		// kill raw fast mode on link drop
		g_rawFastMode = false;		  
		  
		mtls_onDisconnect();
		  
		setLED(CRGB::Black);
		//displayStatus("ADVERTISING", TFT_YELLOW, true);

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

			// yellow = paired/secured, MTLS not active yet
			setLED(CRGB(255, 200, 0)); 
			currentColor = CRGB(255, 200, 0);

#if !NO_DISPLAY				
			displayStatus("SECURED", TFT_GREEN, true);
#endif			
			// flag for mainb loop to display ready
			g_displayReadyScheduled = true;

			// If notifications are already enabled, schedule hello now.
			if( !g_mtlsHelloSeeded && g_isSubscribed && g_txSubChar != nullptr ) 
			{
				mtls_sendHello_B0();          // seeds cached B0 + retry timer (does not send immediately)
				g_mtlsHelloSeeded = true;
			} else 
			{
				// We became secure before subscribe. Remember to send on subscribe later.			
			}

		} else 
		{
#if !NO_DISPLAY				
			displayStatus("PAIR FAIL", TFT_RED, true);
#endif
			g_blinkLedScheduled = true;

			// Reset pairing UI/security flags so we don't get stuck
			g_encReady = false;
			cancelPin();
			g_pinAllowedThisConn = false;
			g_linkEncrypted = false;
			g_linkAuthenticated = false;

			// Delete stale/partial bond for this peer so phone will show pairing UI again
			ble_gap_conn_desc d{};
			if (ble_gap_conn_find(info.getConnHandle(), &d) == 0) {
				ble_store_util_delete_peer(&d.peer_id_addr);
			}

			// Force disconnect so the OS restarts pairing cleanly on next connect
			ble_gap_terminate(info.getConnHandle(), BLE_ERR_REM_USER_CONN_TERM);			
		}
	}
  
};

////////////////////////////////////////////////////////////////////
// Poll BOOT button for a long-press reset.
// - Active-low input (pull-up enabled)
// - Triggers factoryReset() after ~3s hold (one-shot)
// - State resets on release
//
// Called every loop() iteration (non-blocking)
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
// Hard reset of security + provisioning state.
// - Deletes all BLE bonds
// - Unlocks pairing
// - Clears AppKey and setup flags from NVS
// - Clears controller whitelist
// - Reboots after short user-visible delay
////////////////////////////////////////////////////////////////////
void factoryReset() 
{
#if !NO_DISPLAY		
	displayStatus("RESET", TFT_RED, true);
#endif	

	NimBLEDevice::deleteAllBonds();
	setAllowPairing(true);
	// clear app key
	clearAppKeyAndFlag();
	// clear ble pairing key so on reboot it will generate a new one
	clearBlePasskey();

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
// Apply pairing policy to controller whitelist + advertising filter.
// - If pairing unlocked: connect-any.
// - If locked: connect-whitelist-only using stored bonds (self-heal if none).
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
// Boot init: HW + display + settings + optional setup portal.
// Then bring up USB HID + NimBLE + security + characteristics + advertising policy.
////////////////////////////////////////////////////////////////////
void setup() 
{
	// BOOT/BTN: active-low
	pinMode(BTN_PIN, INPUT_PULLUP);   
	
	// LEDs
#if NO_LED	
	// do nothing
#elif (BLUKEY_BOARD == BLUKEY_BOARD_LILYGO_TDONGLE_S3) || !defined(BLUKEY_BOARD)	
	//FastLED.addLeds<APA102, LED_DI, LED_CI, BGR>(led, 1);
	FastLED.addLeds<APA102, LED_DI_PIN, LED_CI_PIN, BGR>(led, 1);
#elif (BLUKEY_BOARD == BLUKEY_BOARD_WAVESHARE_ESP32S3_DISPLAY147)
	FastLED.addLeds<WS2812B, LED_DI_PIN, RGB>(led, 1);
#elif (BLUKEY_BOARD == BLUKEY_BOARD_ESP32S3_ZERO)
	FastLED.addLeds<WS2812B, LED_DI_PIN, GRB>(led, 1);
#elif (BLUKEY_BOARD == BLUKEY_BOARD_ESP32S3_XIAO_PLUS)
    // XIAO user LED use Arduino core LED_BUILTIN, active-low (HIGH=off)
    pinMode(LED_DI_PIN, OUTPUT);
    digitalWrite(LED_DI_PIN, HIGH); // OFF
#endif

	setLED(CRGB::Black);

#if !NO_DISPLAY
	pinMode(TFT_LEDA_PIN, OUTPUT);
	tft.init();
	tft.setSwapBytes(true);
	tft.setRotation(1);
	tft.fillScreen(TFT_BLACK);
#if (BLUKEY_BOARD == BLUKEY_BOARD_WAVESHARE_ESP32S3_DISPLAY147)	
	digitalWrite(TFT_LEDA_PIN, 1);
#else
	digitalWrite(TFT_LEDA_PIN, 0);
#endif	
	drawReady();
#endif

	// load prefs from persisten storage
	initSettings();

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
	
	// USB HID   
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
	// disable to try to make it work with new phones - or keep it as a hint - iphone might need this? need to confirm that causes issues on new phones 
	// it does seem to have a negative impact on android
	//NimBLEDevice::setMTU(247);
	NimBLEDevice::setMTU(185);

	// Register the GAP handler *right after* init and before advertising
	if( !NimBLEDevice::setCustomGapHandler(bleGapEvent) ) 
	{
	  //displayStatus("GAP FAIL", TFT_RED, true);
	} else 
	{
	  //displayStatus("GAP GOOD", TFT_GREEN, true);
	}

	// Generate a random 6-digit PIN per boot and set it as the passkey
	//g_bootPasskey = (esp_random()%900000UL)+100000UL;
	//NimBLEDevice::setSecurityPasskey( g_bootPasskey );
	// Use a STABLE 6-digit passkey (stored in NVS).
	g_bootPasskey = loadOrGenBlePasskey();
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
	g_txChar = pService->createCharacteristic( CHAR_NOTIFY_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC);

	// Explicit CCCD descriptor with encryption required:
	//NimBLEDescriptor* cccd = rxChar->createDescriptor(
	//	"2902", // CCCD UUID
	//	NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE,  // Required for Core Bluetooth
	//	ESP_GATT_PERM_READ_ENC | ESP_GATT_PERM_WRITE_ENC // Protect the descriptor itself!
	//);

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

	// *** Manufacturer Specific Data for BlueKeyboard (BK) ***
	////////////
	// Company ID: 0xFFFF = dev/internal - this will normally be regsitered Bluetooth SIG Company ID
    const uint16_t companyId = 0xFFFF;

	// 0x01 = T-Dongle-S3 display, 0x02 = no display, etc.
#if NO_DISPLAY	
    const uint8_t modelId   = 0x02;  
#else
	const uint8_t modelId   = 0x01;  
#endif	
    const uint8_t areaId    = 0x00;  // 0=Unknown, 1=Home, 2=Work, 3=Portable...
    const uint8_t hostType  = 0x00;  // 0=Universal, 1=PC, 2=Server, 3=TV, 4=MediaPlayer...
    const uint8_t hostBrand = 0x00;  // 0=Unknown, 1=Generic, 2=Samsung, 3=LG, ...
    const uint8_t hostModel = 0x00;  // brand-specific model code (0 for now)
    const uint8_t flags     = 0x00;  // reserved/blank for now - we should avoid using security seinsitive info here

    // MAC tail (BT MAC): last 2 bytes, handy to disambiguate same-named devices
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    const uint8_t macTail0 = mac[4];
    const uint8_t macTail1 = mac[5];

    // Build blob: [CompanyID LE][ 'B''K' model area hostType hostBrand hostModel flags macTail0 macTail1 ]
    uint8_t mfg[2 + 10] = {0};
//    uint8_t mfg[10] = {0};

    size_t i = 0;

    // Company ID 0xFFFF (dev) - little-endian 
    mfg[i++] = (uint8_t)(companyId & 0xFF);
    mfg[i++] = (uint8_t)((companyId >> 8) & 0xFF);

    // Payload
    mfg[i++] = 'B';
    mfg[i++] = 'K';
    mfg[i++] = modelId;
    mfg[i++] = areaId;
    mfg[i++] = hostType;
    mfg[i++] = hostBrand;
    mfg[i++] = hostModel;
    mfg[i++] = flags;
    mfg[i++] = macTail0;
    mfg[i++] = macTail1;

    std::string mfgStr((const char*)mfg, i);

    // ADV so scanners can filter without active scan requests.
    advData.setManufacturerData(mfgStr);

    // we ever over 31-byte ADV limit use scanData instead
    // scanData.setManufacturerData(mfgStr);

	pAdv->setAdvertisementData(advData);
	pAdv->setScanResponseData(scanData);
	
	// helps terminals auto-detect NUS
	//do not need this here as I already do scanData.setCompleteServices(NimBLEUUID(SERVICE_UUID)) ?
	pAdv->addServiceUUID(SERVICE_UUID);   
	
	pAdv->setMinInterval(48); // was 160
	pAdv->setMaxInterval(96); // was 320

	// place strict policy here. only advertise if not paired already
	applyAdvertisePolicyOnBoot();

	pAdv->start();

	//displayStatus("ADVERTISING", TFT_YELLOW, true);
}

////////////////////////////////////////////////////////////////////
// Main pump:
// - long-press reset
// - process queued RX frame
// - mtls_tick() when notifications enabled
// - LED/UI scheduled actions (blink, PIN, READY, heartbeat)
////////////////////////////////////////////////////////////////////
void loop() 
{
	// check for factory reset
	pollResetButtonLongPress();

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

        // dispatch the binaty frame - if case just for debuging
        if( dispatch_binary_frame(localBuf, len) ) 
		{
            // frame was consumed (B1/B3/A* or a post-decrypt inner frame)
			
        } else 
		{
            // Legacy - just drop the case. it should not get here
        }
    }
	
	// drives B0 retries while not active	
	//mtls_tick();    
	// GATED
    if( g_notificationsEnabled ) 
	{
        // This is now delayed until the remote app explicitly says it's ready
        mtls_tick();    
    }	
	
	// When MTLS becomes active, move LED to green (only once)
	static bool s_ledWasMtls = false;
	const bool mtlsNow = mtls_isActive();
	if( mtlsNow && !s_ledWasMtls )
	{
		setLED(CRGB(0, 255, 0));  // green = MTLS established
		currentColor = CRGB(0, 255, 0);
	}
	s_ledWasMtls = mtlsNow;	
	
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
#if !NO_DISPLAY		
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
#endif		
	}
}
