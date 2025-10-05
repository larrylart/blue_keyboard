////////////////////////////////////////////////////////////////////
// simple command handling implementation
////////////////////////////////////////////////////////////////////
#ifndef COMMANDS_H
#define COMMANDS_H

#include <Arduino.h>
#include "settings.h"
#include "layout_kb_profiles.h"   // for KeyboardLayout, layoutName, m_nKeyboardLayout
#include <string.h>   // strlen
#include <stddef.h>   // size_t

// Core: raw bytes + length (implemented in .ino)
extern bool sendTX(const uint8_t* data, size_t len);

// Convenience wrappers
static inline bool sendTX(const char* s) 
{
  return sendTX(reinterpret_cast<const uint8_t*>(s), ::strlen(s));
}
extern bool sendTX_str(const String& s);

// Persist layout (already implemented in your .ino)
extern void saveLayoutToNVS(KeyboardLayout id);

// --- Simple key enum (expand later if needed) ---
enum CommandKey 
{
  KEY_UNKNOWN = 0,
  KEY_LAYOUT,
  KEY_PASS
};

// Map "LAYOUT"/"PASS" -> enum (case-insensitive)
static inline CommandKey parseKey(const String& sIn) 
{
  String s = sIn; s.trim(); s.toUpperCase();
  if (s == "LAYOUT") return KEY_LAYOUT;
  if (s == "PASS")   return KEY_PASS;
  return KEY_UNKNOWN;
}

// Accept both "UK_WINLIN" and "LAYOUT_UK_WINLIN"
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
  return true;
}

// Handle: "SET:LAYOUT=UK_WINLIN" / "GET:LAYOUT" / "SET:PASS=..."
// Returns via sendTX("R:...") and nothing else.
static inline void handleCommand(const String& rawCmd) 
{
  // Split verb vs rest (first colon)
  int firstColon = rawCmd.indexOf(':');
  if (firstColon < 0) { sendTX("R:ERR"); return; }

  String verb = rawCmd.substring(0, firstColon);
  String rest = rawCmd.substring(firstColon + 1);

  verb.trim(); rest.trim(); verb.toUpperCase();

  // Split key vs value (at '=')
  String keyPart, valuePart;
  int eq = rest.indexOf('=');
  if (eq >= 0) 
  {
    keyPart   = rest.substring(0, eq);
    valuePart = rest.substring(eq + 1);
	
  } else 
  {
    keyPart   = rest;
    valuePart = "";
  }
  keyPart.trim(); valuePart.trim();

  CommandKey key = parseKey(keyPart);

  // ---- SET ----
  if( verb == "SET" ) 
  {
    switch(key) 
	{
      case KEY_LAYOUT:
        if(setLayoutByName(valuePart)) 
		{
          sendTX("R:OK");
		  
        } else 
		{
          sendTX("R:ERR");
        }
        return;

      case KEY_PASS:
        // You can store/use valuePart here if needed.
        sendTX("R:OK");
        return;

      default:
        sendTX("R:ERR");
        return;
    }
  }

  // ---- GET ----
  if (verb == "GET") 
  {
    switch(key) 
	{
      case KEY_LAYOUT:
        // Must reply with just the layout name, prefixed by R:
        sendTX_str(String("R:") + layoutName(m_nKeyboardLayout));
        return;

      default:
        sendTX("R:ERR");
        return;
    }
  }

  // ---- Unknown verb ----
  sendTX("R:ERR");
}

#endif // COMMANDS_H
