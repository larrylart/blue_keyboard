////////////////////////////////////////////////////////////////////
// Bluetooth Software Keyboard (v1.2)
// Created by: Larry Lart
////////////////////////////////////////////////////////////////////
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <FastLED.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "pin_config.h"
#include "TFT_eSPI.h" 
#include <Preferences.h>     // NVS key/value
#include <MD5Builder.h>      // Arduino MD5 helper (ESP32 core)
#include <esp_system.h>   // esp_restart()

// put these includes at top of file (if not already present)
extern "C" 
{
  #include "nimble/ble.h"
  #include "host/ble_gap.h"
}

// local
#include "settings.h"
#include "RawKeyboard.h"
#include "layout_kb_profiles.h"
#include "commands.h"

// defines
#define MAX_RX_MESSAGE_LENGTH 4096
// Onboard APA102 pins (T-Dongle-S3)
#define LED_DI 40
#define LED_CI 39
// BLE UUIDs 
#define SERVICE_UUID      "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_WRITE_UUID   "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // RX: phone → dongle
#define CHAR_NOTIFY_UUID  "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // TX: dongle → phone"

// Global (defined in blue_keyboard.ino)
KeyboardLayout m_nKeyboardLayout = KeyboardLayout::US_WINLIN;
//static const char* PREF_NS = "bluekb";  // pick a stable namespace string

// persistent preferences
//Preferences gPrefs;

// flags
static volatile bool g_echoOnConnect = false;

/////////////
CRGB led[1];
TFT_eSPI tft = TFT_eSPI();

// USB HID keyboard 
RawKeyboard Keyboard;

NimBLECharacteristic* pWriteChar = nullptr;
static NimBLECharacteristic* g_txChar = nullptr;

volatile uint32_t recvCount = 0;

// keep track of current "base" LED state
CRGB currentColor = CRGB::Black;

// --- Pairing window flag persisted in NVS (allow one-time pairing, then lock) ---
//static bool g_allowPairing = true;

// flags
static volatile bool g_displayReadyScheduled = false;
static volatile bool g_blinkLedScheduled = false;

// Security/link state 
// check duplaicate flag g_encReady - g_linkEncrypted?
static volatile bool g_linkEncrypted = false;
static volatile bool g_linkAuthenticated = false;
static uint32_t g_bootPasskey = 0;   // random per boot

// --- Pairing UI gating ---
static volatile bool g_encReady = false;           // becomes true on ENC_CHANGE/auth complete
static volatile bool g_pinShowScheduled = false;   // we will show PIN only if still not encrypted after a short delay
static uint32_t g_pinDueAtMs = 0;
static const uint32_t PIN_DELAY_MS = 600;          // 400–800ms works well; adjust to taste

///////////////////////////////////
static volatile bool g_isSubscribed = false;

// Remember exactly which char/connection is subscribed
static NimBLECharacteristic* g_txSubChar = nullptr;
static uint16_t g_txConnHandle = 0xFFFF;

// Deferred hello
static volatile bool g_pendingHello = false;
static uint32_t g_helloDueAtMs = 0;
static std::string g_helloPayload;

// reset to defaults
static bool  g_longPressConsumed = false;
static bool  g_btnWasDown        = false;
static uint32_t g_pressStartMs   = 0;

// Rebuild controller whitelist from bonded peers in NVS.
static void rebuildWhitelistFromBonds() {
  ble_addr_t addrs[8];
  const int capacity = (int)(sizeof(addrs) / sizeof(addrs[0]));
  int count = capacity;

  int rc = ble_store_util_bonded_peers(addrs, &count, capacity);
  if (rc != 0 || count <= 0) return;

  for (int i = 0; i < count; ++i) {
    // NimBLEAddress expects (const uint8_t* val, uint8_t type)
    NimBLEAddress id(addrs[i].val, addrs[i].type);
    NimBLEDevice::whiteListAdd(id);
  }
}

/////////////////////
// ---- tiny helpers ----
static inline void setLED(const CRGB& c) 
{
  led[0] = c;
  FastLED.show();
}

// Blink helper: show red for 1s, then restore currentColor
void blinkLed() 
{
  led[0] = CRGB::Red;
  FastLED.show();
  delay(1000);
  led[0] = currentColor;
  FastLED.show();
}

////////////////////////////////////////////////////////////////////
// Send data back to phone via NUS TX notify (chunk to MTU-3)
////////////////////////////////////////////////////////////////////
bool sendTX(const uint8_t* data, size_t len)
{
  if( !g_txChar ) return false;
  // check security - make sure connection is encrypted
  if( !g_linkEncrypted || !g_linkAuthenticated ) return false;

#ifdef NIMBLE_CPP
  if( g_txChar->getSubscribedCount() == 0 ) return false;
#endif

  // ATT_MTU includes 3-byte header -> max notify payload = MTU - 3
  uint16_t mtu = NimBLEDevice::getMTU();
  if( mtu < 23 ) mtu = 23;
  const uint16_t maxPayload = mtu - 3;

  size_t off = 0;
  while (off < len) 
  {
    size_t n = len - off;
    if( n > maxPayload ) n = maxPayload;

    g_txChar->setValue( reinterpret_cast<const uint8_t*>(data + off), n );

#if defined(ARDUINO_ARCH_ESP32)
    if( !g_txChar->notify() ) return false;
#else
    g_txChar->notify();
#endif

    off += n;
    delay(1); // keep BLE stack happy on bursts
  }
  return true;
}

// helper for string types
bool sendTX_str(const String& s)
{
  return sendTX( reinterpret_cast<const uint8_t*>(s.c_str()),
                static_cast<size_t>(s.length()) );
}

// calculate and return md5 of string
////////////////////////////////////////////////////////////////////
// ---- MD5 (MD5Builder) helpers ----
// Feed exact bytes (handles >64k by chunking; safe const_cast for legacy API)
static String md5Of(const uint8_t* data, size_t len) 
{
  if( !data || len == 0 ) return String("d41d8cd98f00b204e9800998ecf8427e"); // MD5("")
  MD5Builder md5;
  md5.begin();

  // MD5Builder::add() takes uint16_t; feed in chunks
  while( len > 0 ) 
  {
    uint16_t chunk = (len > 0xFFFFu) ? 0xFFFFu : static_cast<uint16_t>(len);
    md5.add(const_cast<uint8_t*>(data), chunk);   // const_cast OK: MD5Builder doesn't modify
    data += chunk;
    len  -= chunk;
  }

  md5.calculate();
  return md5.toString(); // lowercase hex
}

/////////////////////////////
// UI helpers 
void displayStatus(const String& msg, uint16_t color, bool big = true) 
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

static inline void showPin(uint32_t pin) 
{
  char buf[32];
  snprintf(buf, sizeof(buf), "PIN: %06lu", (unsigned long)pin);
  displayStatus(buf, TFT_YELLOW, /*big*/ true);
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

///////////////////////
// status helpers 
void drawReady() { displayStatus("READY", TFT_GREEN, true); }
// draw received msg counter
void drawRecv() 
{
  char buf[32];
  snprintf(buf, sizeof(buf), "RECV: %lu", (unsigned long)recvCount);
  displayStatus(buf, TFT_WHITE, true);
}

// BLE write handler (secured by link state) 
////////////////////////////////////////////////////////////////////
void handleWrite(const std::string& val) 
{
  // Security gate - make sure that connection encrypted - else reject the message
  if( !g_linkEncrypted || !g_linkAuthenticated ) 
  {
    displayStatus("LOCKED", TFT_RED, true);
    //blinkLed();
	g_blinkLedScheduled = true;
	
    sendTX_str("R:ERR:LOCKED");
    return;
  }

  // Basic sanity
  if( val.empty() || val.size() > MAX_RX_MESSAGE_LENGTH ) return;

  // Helper lambdas
  auto startsWith = [](const std::string& s, const char* pfx) -> bool 
  {
    size_t n = strlen(pfx);
    return s.size() >= n && memcmp(s.data(), pfx, n) == 0;
  };

  // C: commands 
  if( startsWith(val, "C:") )
  {
    // Trim whitespace/EOL *for commands only*
    // Convert once to Arduino String for your handleCommand(...) in commands.h
    String cmd = String(val.c_str() + 2);  // whole tail after "C:"
    cmd.trim();
    if( cmd.length() == 0 ) { sendTX("R:ERR"); return; }
    handleCommand(cmd);
    return;
  }

  // S: strings 
  if( startsWith(val, "S:") ) 
  {
    // Do NOT trim: type exactly as received (including CR/LF)
    const char* payload = val.c_str() + 2;
	
	// test special chars
	//Keyboard.print("@ \" # ~ \\ | { } [ ] ? /\n"); 
	//sendUnicodeAware(Keyboard, "@ \" # ~ \\ | { } [ ] ? /\n"); 
	//Keyboard.print("£$€\n"); 
	//sendUnicodeAware(Keyboard, "TEST: £$€\n");	
	
    sendUnicodeAware( Keyboard, payload );

    // MD5 over the payload *without trailing CR/LF*, but still typed
    size_t n = val.size() - 2;                       // payload length
    while( n && (payload[n-1] == '\n' || payload[n-1] == '\r') ) --n;
    String md5 = md5Of(reinterpret_cast<const uint8_t*>(payload), n);
    sendTX_str(String("R:H=") + md5);

    // UI feedback like before
    recvCount++;
	
    drawRecv();
    //blinkLed();
	g_blinkLedScheduled = true;
	
    return;
  }

}

//////////////////////////////////
static void scheduleHello(uint32_t delayMs = 80) 
{
    g_helloPayload = "CONNECTED=";
    g_helloPayload += layoutName(m_nKeyboardLayout); // layoutName -> const char*
    g_pendingHello = true;
    g_helloDueAtMs = millis() + delayMs;
    //Serial.println("[HELLO] scheduled");
}

// display the connected string 
//////////////////////////////////////////////////////
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
    //Serial.printf("[HELLO] notify(conn=%u,char=%u) -> %s\n",
    //              g_txConnHandle, g_txSubChar->getHandle(), ok ? "OK" : "FAIL");

    if( ok ) g_pendingHello = false;
}

// GAP event handler: called by NimBLE host for security/pairing and other events
////////////////////////////////////////////////////////////////////
static int bleGapEvent(struct ble_gap_event *event, void * /*arg*/) 
{
	// intercept by event type 
	switch (event->type) 
	{
		case BLE_GAP_EVENT_CONNECT:
		{
			// If device is locked (pairing closed) and central is NOT bonded, disconnect immediately.
			/*ble_gap_conn_desc d{};
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
		
		case BLE_GAP_EVENT_ENC_CHANGE: {
			g_encReady = (event->enc_change.status == 0);
			g_linkEncrypted = (event->enc_change.status == 0);

			if( g_encReady ) 
			{
				cancelPin();

				// LOCK after the very first successful bonded link when pairing is open:
				ble_gap_conn_desc d{};
				if( ble_gap_conn_find(event->enc_change.conn_handle, &d) == 0 ) 
				{
					if( d.sec_state.bonded && getAllowPairing() ) 
					{
						// Persist lock
						setAllowPairing(false);  // -> writes NVS via settings.h

						// Set accept list to EXACTLY this peer - hard method
						//ble_addr_t only = d.peer_id_addr;   
						//ble_gap_wl_set(&only, 1);
		
						// Whitelist this peer's identity and switch to whitelist-only connections
						NimBLEDevice::whiteListAdd(NimBLEAddress(d.peer_id_addr));
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
			auto* adv = NimBLEDevice::getAdvertising();
			adv->setScanFilter(false /*scan-any*/, getAllowPairing() ? false : true /*connect-whitelist-only*/);
			adv->start();
			break;
		}

		
		/* old to remove
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
  
    // Arduino-ESP32 3.x variant:
/*    void onSubscribe(NimBLECharacteristic* c,
                     NimBLEConnInfo& info,
                     uint16_t subValue) override 
	{
        (void)info;
        handleSubscribe(c, info, subValue);
    }
*/

	void onSubscribe(NimBLECharacteristic* c, NimBLEConnInfo& info, uint16_t subValue) override 
	{
		const bool notifyOn   = (subValue & 0x0001);
		const bool indicateOn = (subValue & 0x0002);
		g_isSubscribed = (notifyOn || indicateOn);

		//Serial.printf("SUBSCRIBE: conn=%u attr=%u sub=0x%04x (notify=%d,indicate=%d) UUID=%s\n",
		//              info.getConnHandle(), c->getHandle(), subValue, notifyOn, indicateOn,
		//              c->getUUID().toString().c_str());

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
				scheduleHello(500);
				
			} else 
			{
				scheduleHello(300);
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
////////////////////////////////////////////////////////////////////
class ServerCallbacks : public NimBLEServerCallbacks 
{
	// IDF 5.x / Arduino-ESP32 3.x styl
  void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override 
  {
	//Serial.printf("ServerCallbacks::onConnect connHandle addr\n");

	g_isSubscribed = false;
	
    setLED(CRGB::Green);
    currentColor = CRGB::Green;

    // Reset link flags and start security; show pairing UI + PIN.
    g_linkEncrypted = g_linkAuthenticated = false;
	g_encReady = false;

	// Kick off security on every link, but DO NOT show the PIN yet.
	NimBLEDevice::startSecurity(info.getConnHandle());

	 // Defer showing the PIN — if bond exists, ENC will succeed quickly and we’ll cancel.
	 schedulePinMaybe();

    //displayStatus("PAIRING...", TFT_YELLOW, true); // should implement this latter ?
    //showPin(g_bootPasskey);
//    NimBLEDevice::startSecurity(info.getConnHandle());

    // Optional: echo current layout so phone/terminal sees readiness quickly.
    //sendConnectedEcho();
  }

  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& info, int reason) override 
  {
	//Serial.printf("ServerCallbacks::onDisconnect reason=%d\n", reason);
	
	// reset all flags to false - todo: check if more need to reset
	g_isSubscribed = false;	  
	g_linkEncrypted = false;
	g_linkAuthenticated = false;
	g_encReady = false;
	  
    setLED(CRGB::Black);
    //displayStatus("ADVERTISING", TFT_YELLOW, true);
    //displayStatus("READY", TFT_GREEN, true);
	g_displayReadyScheduled = true;

    g_linkEncrypted = g_linkAuthenticated = false;
    NimBLEDevice::startAdvertising();
  }

  void onAuthenticationComplete(NimBLEConnInfo& info) override 
  {
    //Serial.printf("ServerCallbacks::onAuthenticationComplete enc=%d auth=%d\n", g_linkEncrypted ? 1:0, g_linkAuthenticated ? 1:0);	  
	  
    // Some NimBLE builds report auth status via ConnInfo
    g_linkEncrypted     = info.isEncrypted();
    g_linkAuthenticated = info.isAuthenticated();
    if( g_linkEncrypted && g_linkAuthenticated ) 
	{
	  cancelPin();
      displayStatus("SECURED", TFT_GREEN, true);
	  // flag for mainb loop to display ready
	  g_displayReadyScheduled = true;
	  
      //delay(500);
      //displayStatus("READY", TFT_GREEN, true);
      // after pairing just to let the sender know is ready - also echo so terminals auto-detect TX
      //sendTX("KPKB_SRV_READY\n");
	  	  
	  //scheduleHello();

    } else 
    {
      displayStatus("PAIR FAIL", TFT_RED, true);
    }
  }
  
};

////////////////////////////////////////////////////////////////////
// call factory reset on long press 6+s
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

  // 2) Held: if 6s reached and not yet consumed -> RESET
  if( btnDown && g_btnWasDown && !g_longPressConsumed ) 
  {
	// 6 seconds
    if (now - g_pressStartMs >= 6000) 
	{
      g_longPressConsumed = true;              // one-shot
      factoryReset();                   // wipe bonds + unlock pairing
    }
  }

  // 3) Released: clean up state
  if (!btnDown && g_btnWasDown) 
  {
    g_btnWasDown = false;
    g_pressStartMs = 0;
    g_longPressConsumed = false;
  }

}

/////////////////////////
// factory reset, clean settings and reboot
/////////////////////////////////////////////
void factoryReset() 
{
  displayStatus("RESET", TFT_RED, true);
  
  NimBLEDevice::deleteAllBonds();
  setAllowPairing(true);

  // Clear accept list (whitelist) at the controller level
  ble_gap_wl_set(nullptr, 0);  

  auto* adv = NimBLEDevice::getAdvertising();
  adv->setScanFilter(false, false);
  adv->stop();
  adv->start();

  delay(1500);        // let the user see "RESET"
  esp_restart();
}

static void applyAdvertisePolicyOnBoot() {
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();

  // Read your persisted flag
  const bool locked = !getAllowPairing();

  if (!locked) {
    // Fresh device or pairing unlocked -> connect-any
    ble_gap_wl_set(nullptr, 0);               // clear controller accept list
    adv->setScanFilter(false /*scan-any*/, false /*connect-any*/);
    return;
  }

  // Pairing locked: rebuild accept list
  ble_addr_t addrs[8];
  const int capacity = (int)(sizeof(addrs) / sizeof(addrs[0]));
  int count = capacity;
  if (ble_store_util_bonded_peers(addrs, &count, capacity) != 0) count = 0;

  if (count <= 0) {
    // Self-heal: lock says "no new pairs", but there are no bonds -> unlock
    setAllowPairing(true);
    ble_gap_wl_set(nullptr, 0);
    adv->setScanFilter(false, false);
    return;
  }

  // Populate accept list
  ble_gap_wl_set(nullptr, 0);
  for (int i = 0; i < count; ++i) {
    NimBLEAddress id(addrs[i].val, addrs[i].type);
    NimBLEDevice::whiteListAdd(id);
  }
  adv->setScanFilter(false /*scan-any*/, true /*connect-whitelist-only*/);
}

////////////////////////////////////////////////////////////////////
// MAIN SETUP
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

	// FOR DEBUG
//	Serial.begin(115200);
//	delay(50);                 // tiny pause to allow serial to come up
//	Serial.println("\n=== KPKB_SRV01 boot ===");


	// USB HID first  
	USB.begin();
	delay(200);
	Keyboard.begin();

	// just to clean up - comment after
	//NimBLEDevice::deleteAllBonds();

	// ===== NimBLE secure setup =====
	NimBLEDevice::init("KPKB_SRV01");
	//NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
	NimBLEDevice::setDeviceName("KPKB_SRV01");
	NimBLEDevice::setPower(ESP_PWR_LVL_P9);
	NimBLEDevice::setMTU(247);

	// Register the GAP handler *right after* init and before advertising
	//NimBLEDevice::setCustomGapHandler(bleGapEvent);

	// Generate a random 6-digit PIN per boot and set it as the passkey
	g_bootPasskey = (esp_random()%900000UL)+100000UL;
	NimBLEDevice::setSecurityPasskey( g_bootPasskey );

	// Bonding + MITM + LE Secure Connections - SC does not seem to work in esp32 2.0.14
	NimBLEDevice::setSecurityAuth(/*bond*/true, /*MITM*/true, /*SC*/true);
	//NimBLEDevice::setSecurityAuth(/*bond*/true, /*MITM*/true, /*SC*/false);

	// force show password
	NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
	//NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO); 

	// new add
	NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
	NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
	// Register the GAP handler 
	if( !NimBLEDevice::setCustomGapHandler(bleGapEvent) ) 
	{
	  //displayStatus("GAP FAIL", TFT_RED, true);
	} else 
	{
	  //displayStatus("GAP GOOD", TFT_GREEN, true);
	}

	// If pairing is locked, repopulate whitelist from bonded devices
	//if (!getAllowPairing()) 
	//{
	//	rebuildWhitelistFromBonds();  // <-- new
	//}

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
	pWriteChar = pService->createCharacteristic(
	  CHAR_WRITE_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC);
	pWriteChar->setCallbacks(new WriteCallback());
	pService->start();

	// Advertising
	NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
//	pAdv->setScanFilter(false /*scan-any*/, getAllowPairing() ? false : true /*connect-whitelist-only*/);
	NimBLEAdvertisementData advData;
	advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
	//advData.setAppearance(0x03C1);
	//advData.setCompleteServices(NimBLEUUID(SERVICE_UUID));
	NimBLEAdvertisementData scanData;
	scanData.setName("KPKB_SRV01");
	scanData.setCompleteServices(NimBLEUUID(SERVICE_UUID));

	pAdv->setAdvertisementData(advData);
	pAdv->setScanResponseData(scanData);
	pAdv->addServiceUUID(SERVICE_UUID);   // helps terminals auto-detect NUS
	pAdv->setMinInterval(160);
	pAdv->setMaxInterval(320);

	// place strict policy here?
	applyAdvertisePolicyOnBoot();

	pAdv->start();

	//displayStatus("ADVERTISING", TFT_YELLOW, true);

}

//////////////////////////
// main loop
////////////////////////////////////////////////////////////////////
void loop() 
{
	// check for factory reset
	pollResetButtonLongPress();
	
	// just to notify on connect
	flushHelloIfDue();
	
	// :: led cmds
	if( g_blinkLedScheduled )
	{
		// this a delay 1s in it. could do better in two steps and not block
		blinkLed();
		g_blinkLedScheduled = false;
	}
	
	// show PIN only if encryption did NOT come up quickly (like true pairing)
	if( g_pinShowScheduled && !g_encReady && (int32_t)(millis() - g_pinDueAtMs) >= 0) 
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
