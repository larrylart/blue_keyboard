////////////////////////////////////////////////////////////////////
// Purpose: just a fancy to customize the usb stick appereance to os
// Larry Lart
////////////////////////////////////////////////////////////////////
#include "tusb.h"
#include <stdint.h>

// usb_desc_override.c 
#include <stdint.h>

static tusb_desc_device_t const desc_device = {
	.bLength            = sizeof(tusb_desc_device_t),
	.bDescriptorType    = TUSB_DESC_DEVICE,
	.bcdUSB             = 0x0200,
	.bDeviceClass       = 0x00,
	.bDeviceSubClass    = 0x00,
	.bDeviceProtocol    = 0x00,
	.bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
	.idVendor           = 0x303A,    // keep current VID
	.idProduct          = 0x1001,    // keep current PID so Windows treats it as same device
	.bcdDevice          = 0x0100,
	.iManufacturer      = 0x01,      // MUST be non-zero to use strings
	.iProduct           = 0x02,
	.iSerialNumber      = 0x03,
	.bNumConfigurations = 0x01
};

uint8_t const * tud_descriptor_device_cb(void) 
{
	return( (uint8_t const*) &desc_device );
}

// Works with ESP32 Arduino 3.x + TinyUSB
// Return UTF-16LE string descriptors for the indexes the core requests.
// Commonly: 0 = language, 1 = Manufacturer, 2 = Product, 3 = Serial
uint16_t const * tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
	static uint16_t desc[32];
	(void)langid;

	if( index == 0 ) 
	{
		// 0x0304 = (type=STRING=0x03, length=4 bytes)
		desc[0] = 0x0304;
		desc[1] = 0x0409; // US English
		
		return( desc );
	}

	const char* str = "";
	switch( index ) 
	{
		case 1: str = "LilyGo/Larry Lart"; break;  // Manufacturer
		case 2: str = "BlueKeyboard";     break;  // Product
		case 3: str = "BK-0016";           break;  // Bump to refresh cache
		default: return( NULL ); // TinyUSB will handle gracefully
	}

	// Convert ASCII -> UTF-16LE
	uint8_t len = 0;
	while( str[len] && len < 31 ) { desc[1 + len] = (uint8_t)str[len]; len++; }
	desc[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * (1 + len)));
	
	return( desc );
}

