////////////////////////////////////////////////////////////////////
// Bluetooth Software Keyboard
// Created by: Larry Lart
////////////////////////////////////////////////////////////////////
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <FastLED.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "pin_config.h"
#include "TFT_eSPI.h" 

#include "RawKeyboard.h"

// === PICK ONE host profile ===
//#define PROFILE_UK_WINLIN
//#define PROFILE_IE_WINLIN
//#define PROFILE_US_WINLIN
//#define PROFILE_UK_MAC
//#define PROFILE_IE_MAC
#define PROFILE_US_MAC
// If AltGr acts as Ctrl+Alt on your Win/Linux host, also:
//#define ALTGR_IMPL_CTRLALT 1

#include "layout_kb_profiles.h"

/////////////
// Onboard APA102 pins (T-Dongle-S3)
#define LED_DI 40
#define LED_CI 39
CRGB led[1];

TFT_eSPI tft = TFT_eSPI();

// ---- USB HID keyboard ----
//USBHIDKeyboard Keyboard;
RawKeyboard Keyboard;

// ---- BLE UUIDs ----
//#define SERVICE_UUID    "0000feed-0000-1000-8000-00805f9b34fb"
//#define CHAR_WRITE_UUID "0000beef-0000-1000-8000-00805f9b34fb"
// Nordic UART Service (NUS)
#define SERVICE_UUID      "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_WRITE_UUID   "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // RX: phone → dongle
#define CHAR_NOTIFY_UUID  "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // TX: dongle → phone"

NimBLECharacteristic* pWriteChar = nullptr;
NimBLECharacteristic* pTxChar    = nullptr;

volatile uint32_t recvCount = 0;

// keep track of current "base" LED state
CRGB currentColor = CRGB::Black;

// ===== Security/link state =====
static volatile bool g_linkEncrypted = false;
static volatile bool g_linkAuthenticated = false;
static uint32_t g_bootPasskey = 0;   // random per boot

// ---- tiny helpers ----
static inline void setLED(const CRGB& c) {
  led[0] = c;
  FastLED.show();
}

// Blink helper: show red for 1s, then restore currentColor
void blinkLed() {
  led[0] = CRGB::Red;
  FastLED.show();
  delay(1000);
  led[0] = currentColor;
  FastLED.show();
}

// Send data back to phone via NUS TX notify (chunk to MTU-3)
static void sendTX(const std::string& s) {
  if (!pTxChar) return;
  uint16_t mtu = NimBLEDevice::getMTU();
  uint16_t maxPayload = (mtu >= 23) ? (mtu - 3) : (uint16_t)(20 - 3); // 3B ATT header
  if (maxPayload < 1) maxPayload = 1;
  size_t off = 0;
  while (off < s.size()) {
    size_t n = std::min((size_t)maxPayload, s.size() - off);
    pTxChar->setValue((const uint8_t*)&s[off], n);
    pTxChar->notify();
    off += n;
  }
}

// ---------- UI helpers ----------
void displayStatus(const String& msg, uint16_t color, bool big = true) 
{
  static bool hasBox = false;
  static int16_t bx, by; static uint16_t bw, bh;

  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(big ? 4 : 2);
  tft.setTextColor(color, TFT_BLACK);

  if (hasBox) tft.fillRect(bx - 2, by - 2, bw + 4, bh + 4, TFT_BLACK);

  bw = tft.textWidth(msg);
  bh = tft.fontHeight();
  bx = (tft.width()  - (int)bw) / 2;
  by = (tft.height() - (int)bh) / 2;

  tft.setTextDatum(TL_DATUM);
  tft.drawString(msg, bx, by);
  hasBox = true;
}

static inline void showPin(uint32_t pin) {
  char buf[32];
  snprintf(buf, sizeof(buf), "PIN: %06lu", (unsigned long)pin);
  displayStatus(buf, TFT_YELLOW, /*big*/ true);
}

// ---------- status helpers ----------
void drawReady() { displayStatus("READY", TFT_GREEN, true); }

void drawRecv() {
  char buf[32];
  snprintf(buf, sizeof(buf), "RECV: %lu", (unsigned long)recvCount);
  displayStatus(buf, TFT_WHITE, true);
}

// ---------- BLE write handler (secured by link state) ----------
void handleWrite(const std::string& val) 
{
  if (!g_linkEncrypted || !g_linkAuthenticated) 
  {
    displayStatus("LOCKED", TFT_RED, true);
    blinkLed();

    sendTX(std::string("OK:") + val);  // OPTIONAL echo so terminals auto-detect TX

    return;
  }
  if (val.empty() || val.size() > 512) return;

  // replace with mapping
  //Keyboard.print(val.c_str());
  // test
  //Keyboard.print("@ \" # ~ \\ | { } [ ] ? /\n");
  //sendUnicodeAware(Keyboard, "@ \" # ~ \\ | { } [ ] ? /\n");
  //Keyboard.print("£$€\n");
  //sendUnicodeAware(Keyboard, "TEST: £$€\n");

  sendUnicodeAware(Keyboard, val.c_str());
  
  recvCount++;
  drawRecv();
  blinkLed();
}

class WriteCallback : public NimBLECharacteristicCallbacks {
 public:
  void onWrite(NimBLECharacteristic* chr) { handleWrite(chr->getValue()); }
  void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo& info) override {
    (void)info; handleWrite(chr->getValue());
  }
};

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
    setLED(CRGB::Green);
    currentColor = CRGB::Green;

    // Reset link flags and start security; show pairing UI + PIN.
    g_linkEncrypted = g_linkAuthenticated = false;
    displayStatus("PAIRING...", TFT_YELLOW, true);
    showPin(g_bootPasskey);
    NimBLEDevice::startSecurity(info.getConnHandle());
  }

  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& info, int reason) override {
    setLED(CRGB::Black);
    //displayStatus("ADVERTISING", TFT_YELLOW, true);
    displayStatus("READY", TFT_GREEN, true);

    g_linkEncrypted = g_linkAuthenticated = false;
    NimBLEDevice::startAdvertising();
  }

  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    // Some NimBLE builds report auth status via ConnInfo
    g_linkEncrypted     = info.isEncrypted();
    g_linkAuthenticated = info.isAuthenticated();
    if (g_linkEncrypted && g_linkAuthenticated) {
      displayStatus("SECURED", TFT_GREEN, true);
      delay(1000);
      displayStatus("READY", TFT_GREEN, true);
      // just to let the sender know is ready
      sendTX("KPKB_SRV_READY\n");

    } else 
    {
      displayStatus("PAIR FAIL", TFT_RED, true);
    }
  }
};

void setup() {

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

  // Generate a random 6-digit PIN per boot and set it as the passkey
  g_bootPasskey = (esp_random()%900000UL)+100000UL;
  NimBLEDevice::setSecurityPasskey(g_bootPasskey);

  // Bonding + MITM + LE Secure Connections
  //NimBLEDevice::setSecurityAuth(/*bond*/true, /*MITM*/true, /*SC*/true);
  NimBLEDevice::setSecurityAuth(/*bond*/true, /*MITM*/true, /*SC*/false);

  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);

  // new add
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);

  // NOTE: Your NimBLE version lacks setSecurityCallbacks and per-attribute permissions.

  NimBLEServer* pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks()); 

  NimBLEService* pService = pServer->createService(SERVICE_UUID);

  // TX = Notify to device connecting 
  pTxChar = pService->createCharacteristic( CHAR_NOTIFY_UUID, NIMBLE_PROPERTY::NOTIFY);

  // read from device connecting
  pWriteChar = pService->createCharacteristic(
      CHAR_WRITE_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  pWriteChar->setCallbacks(new WriteCallback());
  pService->start();

  // Advertising
  NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
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
  pAdv->start();

//displayStatus("ADVERTISING", TFT_YELLOW, true);

}

void loop() {
  static uint32_t last = 0;
  if (millis() - last > 1000) {
    last = millis();
    static bool on = false;
    uint16_t c = on ? TFT_RED : TFT_BLACK;
    tft.fillRect(tft.width()-10, 2, 6, 6, c);
    on = !on;
  }
}
