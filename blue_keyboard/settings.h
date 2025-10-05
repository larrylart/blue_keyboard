#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "layout_kb_profiles.h"  // for KeyboardLayout + extern m_nKeyboardLayout

// -----------------------------------------------------------------------------
// SETTINGS MODULE  (fully self-contained, no C++17 needed)
// -----------------------------------------------------------------------------

// Global Preferences object (one instance for the whole firmware)
static Preferences gPrefs;

// -----------------------------------------------------------------------------
// Ensure NVS namespace is open for RW access
// -----------------------------------------------------------------------------
static void ensurePrefsOpenRW()
{
    static bool opened = false;
    if (!opened)
    {
        // Change "app" to any other namespace if you prefer
        if (!gPrefs.begin("app", /*readOnly=*/false))
        {
            Serial.println("[Settings] Failed to open NVS namespace 'app'");
        }
        else
        {
            opened = true;
            // Serial.println("[Settings] NVS namespace 'app' opened");
        }
    }
}

// -----------------------------------------------------------------------------
// Internal NVS keys
// -----------------------------------------------------------------------------
static const char* const NVS_KEY_LAYOUT    = "kb_layout";
static const char* const NVS_KEY_ALLOWPAIR = "allowPair";

// -----------------------------------------------------------------------------
// Pairing flag - true means pairing window open
// -----------------------------------------------------------------------------
static bool g_allowPairing = true;   // cached in RAM

// -----------------------------------------------------------------------------
// Initialize all settings (call once in setup())
// -----------------------------------------------------------------------------
static void initSettings()
{
    ensurePrefsOpenRW();

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
}

// -----------------------------------------------------------------------------
// Keyboard layout helpers (same interface as your originals)
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// Pairing-lock flag helpers
// -----------------------------------------------------------------------------
static bool getAllowPairing()
{
    return g_allowPairing;
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
