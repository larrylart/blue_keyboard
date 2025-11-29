////////////////////////////////////////////////////////////////////
// setup_portal.cpp - Wi-Fi setup portal for BlueKeyboard dongle
// 
// This module runs a one-time Wi-Fi AP + captive portal that lets
// the user:
//   - Set the BLE name (what shows up in Bluetooth scan)
//   - Choose keyboard layout
//   - Set a one-time setup password
//
// The setup password is never stored in clear. A PBKDF2-HMAC-SHA256
// verifier and salt are stored instead and used later during the
// first MTLS key provisioning from the app.
////////////////////////////////////////////////////////////////////
#include <Arduino.h>
#include "TFT_eSPI.h" 
#include <WiFi.h>

#include <FS.h>
using fs::FS;

#include <WebServer.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <esp_system.h>  // esp_random()
#include <esp_mac.h>     // esp_read_mac(), ESP_MAC_WIFI_SOFTAP
#include <mbedtls/version.h> 
#include <DNSServer.h>

#include "settings.h"
#include "layout_kb_profiles.h"   // KeyboardLayout, layoutName(), m_nKeyboardLayout

// externals
extern void displayStatus(const String& msg, uint16_t color, bool);
//extern String computeDefaultBleName(); 

////////////////////////////////////////////////////////////////////
// Captive-portal DNS and global state
////////////////////////////////////////////////////////////////////

// DNS server for captive portal - trying to resolve all queries to our AP IP
static DNSServer dns;
static const byte DNS_PORT = 53;

// Fixed IP configuration for the AP, so we know where to redirect
static const IPAddress AP_IP(192,168,4,1);
static const IPAddress AP_NET(255,255,255,0);

// Flags used to coordinate reboot after settings are saved
static bool g_needReboot = false;
static unsigned long g_restartAt = 0;

// output to serial teminal - for debug purposes
static void displaySetupInfo(const char* ssid, const char* psk) 
{
	DPRINTLN("==== SETUP PORTAL ====");
	DPRINT("Connect to Wi-Fi AP: %s\n", ssid);
	DPRINT("Password: %s\n", psk);
	DPRINTLN("Browse http://192.168.4.1/");
}

////////////////////////////////////////////////////////////////////
// Show AP password on the TFT in big blue text so the user can
// type it into the phone / laptop Wi-Fi dialog.
////////////////////////////////////////////////////////////////////
static inline void showPassword(const char* _psk) 
{
	char buf[12];
	snprintf(buf, sizeof(buf), "%s", _psk);
	displayStatus(buf, TFT_BLUE, /*big*/ true);
}

////////////////////////////////////////////////////////////////////
// KDF: PBKDF2-HMAC-SHA256 (via mbedTLS)
// 
// This helper computes a 32-byte verifier from:
//   - password (String)
//   - random salt (16 bytes)
//   - iteration count
//
// The verifier and salt are stored in NVS; the clear password
// is never written to NVS.
////////////////////////////////////////////////////////////////////
static void pbkdf2_sha256(const String& password,
                          const uint8_t* salt, size_t saltLen,
                          uint32_t iters, uint8_t out32[32])
{
	const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

#if defined(MBEDTLS_VERSION_NUMBER) && (MBEDTLS_VERSION_NUMBER >= 0x03000000)
	// mbedTLS 3.x (ESP-IDF v5.x): use the _ext variant, which takes md_type
	mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
								(const unsigned char*)password.c_str(), password.length(),
								salt, saltLen, (unsigned int)iters,
								32, out32);
#else
	// Older bundles: use the classic variant, which takes md_info*
	mbedtls_md_context_t ctx; 
	mbedtls_md_init(&ctx);
	mbedtls_md_setup(&ctx, md, 1 ); // 1=HMAC

	mbedtls_pkcs5_pbkdf2_hmac(md,
							(const unsigned char*)password.c_str(), password.length(),
							salt, saltLen, (unsigned int)iters,
							32, out32);

	mbedtls_md_free(&ctx);
#endif
}


////////////////////////////////////////////////////////////////////
// HTML form (simple, no external assets)
// 
// This renders the full setup page with:
//   - BLE name field
//   - Keyboard layout dropdown
//   - Two password boxes + client-side matching check
//
// If a validation error occurred on the server side, the message
// is shown at the top in a red box.
////////////////////////////////////////////////////////////////////
static String renderForm(const String& err = "")
{
	String currName = loadBleName();
	//if( currName.length() < 3 ) currName = computeDefaultBleName(); - should be already done now
	KeyboardLayout currLayout = m_nKeyboardLayout;

	String html;
	
	html += F(
		"<!doctype html><html lang='en'><head><meta charset='utf-8'>"
		"<meta name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'>"
		"<title>BlueKeyboard • First-Run Setup</title>"
		"<style>"
		":root{--bg:#f7f8fa;--card:#ffffff;--muted:#667;--txt:#111;--accent:#0b5cff;--accent2:#19a87b;--err:#d33}"
		"body{margin:0;padding:24px;background:var(--bg);font:16px/1.45 -apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Inter,Arial,sans-serif;color:var(--txt);}"
		".wrap{max-width:720px;margin:0 auto}"
		".card{background:var(--card);border:1px solid #ddd;border-radius:14px;box-shadow:0 4px 12px rgba(0,0,0,.08);padding:20px 18px}"
		"h1{font-size:22px;margin:0 0 16px;text-align:center;color:#0b5cff;}"
		"p.lead{text-align:center;margin:.5rem 0 1rem;color:var(--muted)}"
		"label{display:block;margin:.75rem 0 .35rem;color:#333;font-size:13px;font-weight:600;letter-spacing:.2px;text-transform:uppercase;}"
		"input,select,button{display:block;width:100%;max-width:100%;box-sizing:border-box;padding:.7rem .8rem;font-size:16px;border-radius:10px;border:1px solid #ccc;background:#fff;color:var(--txt);outline:none;transition:border .15s,box-shadow .15s;}"
		"input:focus,select:focus{border-color:var(--accent);box-shadow:0 0 0 3px rgba(11,92,255,.2);}"
		".row{display:grid;gap:12px;}"
		"@media(min-width:560px){.row.two{grid-template-columns:1fr 1fr;}}"
		".err{background:#fff0f0;border:1px solid #e7b3b3;color:var(--err);padding:.7rem .8rem;border-radius:10px;margin:0 0 12px;}"
		".hint{font-size:12px;color:var(--muted);margin:.35rem 0 0;}"
		".inline{display:flex;gap:8px;align-items:center;}"
		".btn{margin-top:12px;background:var(--accent);border:0;color:#fff;font-weight:600;letter-spacing:.3px;border-radius:10px;cursor:pointer;transition:background .2s;padding:.7rem;}"
		".btn:hover{background:#0848c1;}"
		".btn[disabled]{opacity:.55;cursor:not-allowed;filter:grayscale(25%);}"
		".ok{color:var(--accent2);font-weight:600;font-size:12px;margin-top:6px;}"
		".warn{color:var(--err);font-weight:600;font-size:12px;margin-top:6px;}"
		"</style></head><body><div class='wrap'>"
		"<h1>BlueKeyboard • Setup</h1>"
		"<p class='lead'>Name your dongle, choose keyboard layout, and set a one-time password.</p>"
		"<div class='card'>");

	if (err.length() > 0) html += "<div class='err'>" + err + "</div>";

	html += F(
		"<form method='POST' action='/save' autocomplete='off' autocapitalize='off' spellcheck='false'>"
		  "<label for='ble'>BLE Name</label>"
		  "<input id='ble' name='ble' maxlength='24' required placeholder='BlueKeyboard_XXXX' value='");
	html += currName;
	html += F("'>"
		"<div class='hint'>Appears during Bluetooth discovery.</div>"
		"<label for='layout'>Keyboard Layout</label>"
		"<select id='layout' name='layout' required>");

	for( int i = 1; i <= 100; ++i ) 
	{
		KeyboardLayout id = static_cast<KeyboardLayout>(i);
		const char* name = layoutName(id);
		if (!name || !*name) continue;
		html += "<option value='"; html += String(i); html += "'";
		if (id == currLayout) html += " selected";
		html += ">"; html += name; html += "</option>";
	}

	html += F(
	  "</select>"
	  "<div class='row two'>"
		"<div>"
		  "<label for='pw1'>Setup Password</label>"
		  "<div class='inline'>"
			"<input id='pw1' name='pw1' type='password' minlength='6' required placeholder='min 6 chars'>"
			"<button type='button' id='toggle1' onclick=\"togglePw('pw1',this)\" class='btn' style='width:auto;padding:.45rem .6rem'>Show</button>"
		  "</div>"
		  "<div class='hint'>Used to secure initial exchange.</div>"
		"</div>"
		"<div>"
		  "<label for='pw2'>Repeat Password</label>"
		  "<div class='inline'>"
			"<input id='pw2' name='pw2' type='password' minlength='6' required placeholder='repeat password'>"
			"<button type='button' id='toggle2' onclick=\"togglePw('pw2',this)\" class='btn' style='width:auto;padding:.45rem .6rem'>Show</button>"
		  "</div>"
		  "<div id='pmatch' class='warn' style='display:none'>Passwords must match.</div>"
		  "<div id='pok' class='ok' style='display:none'>Looks good ✓</div>"
		"</div>"
	  "</div>"
	  "<button id='submitBtn' class='btn' type='submit' disabled>Save &amp; Restart</button>"
	"</form></div></div>"
	"<script>"
	  "function togglePw(id,btn){var i=document.getElementById(id);"
	  "var s=i.type==='password'?'text':'password';"
	  "i.type=s;btn.textContent=(s==='text')?'Hide':'Show';}"
	  "var pw1=document.getElementById('pw1'),pw2=document.getElementById('pw2'),btn=document.getElementById('submitBtn');"
	  "var pm=document.getElementById('pmatch'),ok=document.getElementById('pok');"
	  "function v(){var a=pw1.value,b=pw2.value,m=a.length>=6&&b.length>=6&&a===b;"
	  "btn.disabled=!m;pm.style.display=(a&&b&&!m)?'block':'none';ok.style.display=(m)?'block':'none';}"
	  "pw1.addEventListener('input',v);pw2.addEventListener('input',v);"
	"</script>"
	"</body></html>");

	return( html );
}

static WebServer server(80);

////////////////////////////////////////////////////////////////////
// Wi-Fi AP setup with random PSK
// 
// This brings up the ESP32 as a SoftAP with:
//   - SSID: BLUKBD-XXXX (last 2 MAC bytes)
//   - PSK:  8-character random alphanumeric string
//
// Steps:
//   1. Read MAC address and build SSID.
//   2. Generate random PSK and save to outPsk.
//   3. Configure Wi-Fi AP mode with fixed IP (AP_IP).
//   4. Start DNS captive server that resolves everything to AP_IP.
////////////////////////////////////////////////////////////////////
static void startApWithRandomPsk(String& outSsid, String& outPsk) 
{
	// SSID: BLUKBD-XXXX (last 2 bytes of MAC), PSK: 12 hex chars
	uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
	char ssid[20]; snprintf(ssid, sizeof(ssid), "BLUKBD-%02X%02X", mac[4], mac[5]);

	// Generate an 8-character alphanumeric PSK (A–Z, a–z, 0–9)
	char psk[9] = {0};
	static const char chars[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789";
		
	for( int i = 0; i < 8; ++i ) 
	{
		uint8_t r = (uint8_t)esp_random() % (sizeof(chars) - 1);
		psk[i] = chars[r];
	}

	outSsid = ssid;
	outPsk  = psk;

	WiFi.mode(WIFI_AP);
	// Important: set a known AP IP so we can DNS-redirect to it
	WiFi.softAPConfig(AP_IP, AP_IP, AP_NET);
	WiFi.softAP(outSsid.c_str(), outPsk.c_str());
	delay(100);

	// Start DNS “captive portal”: resolve every hostname to our AP_IP
	dns.start(DNS_PORT, "*", AP_IP);
	
	// When a client connects to our AP, display the portal IP
	WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) 
	{
	  if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) 
	  {
		displayStatus("192.168.4.1", TFT_BLUE, true);
		DPRINTLN("Client connected to AP - displaying IP 192.168.4.1");
	  }
	});	
}

////////////////////////////////////////////////////////////////////
// runSetupPortal() - main entry point
//
// This function runs the entire first-run flow:
//
//   Step 0: If setup is already done (NVS flag), return immediately.
//   Step 1: Start SoftAP + DNS captive portal with random PSK.
//   Step 2: Show PSK on TFT and (optionally) log AP info to serial.
//   Step 3: Register HTTP routes for:
//           - "/"      -> HTML form
//           - "/save"  -> handle POST, validate, store NVS, KDF, app key
//           - "/reboot"-> trigger restart
//           - captive portal probe URLs -> redirect to "/"
//   Step 4: Enter loop that services DNS + HTTP until save schedules reboot.
//   Step 5: Tear down Wi-Fi and restart into normal BLE mode.
//
// Notes:
//   - This is a blocking function meant to run at boot when the device
//     is not yet configured.
////////////////////////////////////////////////////////////////////
bool runSetupPortal() 
{
	// Bring up AP + portal only if not already done.
	if (isSetupDone()) return true;

	// Ensure NVS is initialized/open before any settings access
	//ensurePrefsOpenRW();

	String ssid, psk;
	startApWithRandomPsk(ssid, psk);
  
#if (!DEBUG_GLOBAL_DISABLED && DEBUG_ENABLED)
	// this displays to serial port - use on debug only
	displaySetupInfo(ssid.c_str(), psk.c_str());
#endif
	
	// display AP key
	showPassword( psk.c_str() );
	
	// Routes
	server.on("/", HTTP_GET, []() {
		server.send(200, "text/html", renderForm());
	});

	///////////////////////
	// :: SAVE MY DATA
	server.on("/save", HTTP_POST, []() {
		String ble = server.hasArg("ble") ? server.arg("ble") : "";
		String pw1 = server.hasArg("pw1") ? server.arg("pw1") : "";
		String pw2 = server.hasArg("pw2") ? server.arg("pw2") : "";
		String layoutStr = server.hasArg("layout") ? server.arg("layout") : "";

		// Basic validation
		if( ble.length() < 3 || ble.length() > 24 ) 
		{
		  server.send(400, "text/html", renderForm("BLE name must be 3–24 characters."));
		  return;
		}
		if( pw1 != pw2 || pw1.length() < 6 ) 
		{
		  server.send(400, "text/html", renderForm("Passwords must match and be at least 6 characters."));
		  return;
		}
		if( layoutStr.length() == 0 ) 
		{
		  server.send(400, "text/html", renderForm("Please choose a layout."));
		  return;
		}

		// Parse layout ID
		int layoutInt = layoutStr.toInt();
		KeyboardLayout chosen = static_cast<KeyboardLayout>(layoutInt);

		// Persist BLE name + layout
		saveBleName(ble);
		saveLayoutToNVS(chosen);

		// Prepare password KDF: salt + PBKDF2(password, salt, iters) - verifier
		uint8_t salt[16];
		for( int i=0; i<16; ++i ) salt[i] = (uint8_t)esp_random();
		
		//  was 100000 but too slow for the dongle - 10k should be good enough give that is only birefly used 
		uint32_t iters = 10000; 
		uint8_t verif[32];
		pbkdf2_sha256(pw1, salt, sizeof(salt), iters, verif);
		savePwKdf(salt, verif, iters);

		// Ensure APPKEY exists
		loadOrGenAppKeyForMTLS();

		// Mark setup complete
		setSetupDone(true);

		// mark to reboot in ble mode
		g_needReboot = true;
		g_restartAt  = millis() + 2000;

		// Response page
		String ok;
		ok += F("<!doctype html><html><meta charset='utf-8'>"
				"<meta name='viewport' content='width=device-width,initial-scale=1'>"
				"<title>Saved</title><body>"
				"<p class='ok'>Saved. You can disconnect from Wi-Fi. The device will reboot/switch to BLE mode.</p>"
				"<script>setTimeout(function(){fetch('/reboot');},500);</script>"
				"</body></html>");
		server.send(200, "text/html", ok);
	});

	server.on("/reboot", HTTP_GET, []() 
	{
		server.send(200, "text/plain", "OK");
		delay(100);
		ESP.restart();
	});


	// Add captive endpoints + onNotFound redirect
	auto redirectToRoot = []() {
	  // Some OSes honor 302, others just display the content — do both.
	  String html = F("<!doctype html><html><head><meta http-equiv='refresh' content='0; url=/'/>"
					  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
					  "<title>Redirecting…</title></head>"
					  "<body><a href='/'>Continue to setup</a></body></html>");
	  server.sendHeader("Location", String("http://") + AP_IP.toString() + "/");
	  server.send(302, "text/html", html);
	};

	// Android captive portal checks
	server.on("/generate_204", HTTP_ANY, redirectToRoot);
	server.on("/gen_204",      HTTP_ANY, redirectToRoot);

	// Apple iOS/macOS captive portal
	server.on("/hotspot-detect.html", HTTP_ANY, redirectToRoot);

	// Windows captive portal checks
	server.on("/ncsi.txt",           HTTP_ANY, redirectToRoot); // legacy
	server.on("/connecttest.txt",    HTTP_ANY, redirectToRoot); // Win10+

	// ChromeOS / others sometimes hit google endpoints via DNS - we’ll catch via DNS anyway.

	// Fallback: anything else not matched - redirect to root
	server.onNotFound(redirectToRoot);


	// start server
	server.begin();

	// Simple loop (blocking) until setup_done becomes true
	unsigned long lastBlink = 0;
	for(;;) 
	{		  
		dns.processNextRequest(); 
		 
		server.handleClient();
		// optional: small LED/TFT “AP mode” heartbeat
		if (millis() - lastBlink > 500) { lastBlink = millis(); /* blink */ }
		// If a parallel task sets setup_done, break out:
		//if( isSetupDone() || g_needReboot ) break;
		if (g_needReboot && millis() >= g_restartAt) break;
		
		delay(2);
	}

	// Teardown AP & server
	dns.stop();
	server.stop();
	WiFi.softAPdisconnect(true);
	WiFi.mode(WIFI_OFF);
	delay(200);
  
	// reboot cleanly if flagged
	if (g_needReboot) 
	{
		delay(300);
		ESP.restart();
	}  
  
	return true;
}
