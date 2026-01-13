// File: TFT_eSPI/User_Setups/Setup_Waveshare_ESP32S3_147.h
//
// Waveshare ESP32-S3 1.47" LCD (ST7789, 172x320) + backlight on GPIO48
// SPI: SCLK=40, MOSI=45, CS=42, DC=41, RST=39
//
// Use with TFT_eSPI by enabling this setup in User_Setup_Select.h


// ST7789 172 x 320 display, ESP32-C6 Waveshare
#define USER_SETUP_ID 901

#define ST7789_DRIVER     // Configure all registers
//#define TFT_SDA_READ   // Display has a bidirectional SDA pin

#define TFT_WIDTH  172
#define TFT_HEIGHT 320

//#define CGRAM_OFFSET      // Library will add offsets required

//#define TFT_RGB_ORDER TFT_RGB  // Colour order Red-Green-Blue
#define TFT_RGB_ORDER TFT_BGR  // Colour order Blue-Green-Red

//#define TFT_INVERSION_ON
//#define TFT_INVERSION_OFF
#define TFT_BACKLIGHT_ON HIGH 

// -------------------- SPI pins --------------------
#define TFT_MOSI 45
#define TFT_SCLK 40
#define TFT_CS   42
#define TFT_DC   41
#define TFT_RST  39
// If your build complains about MISO, define it as -1 (not used)
#define TFT_MISO -1

// -------------------- Backlight --------------------
#define TFT_BL   48

#define LOAD_GLCD // Font 1. Original Adafruit 8 pixel font needs ~1820 bytes in FLASH
#define LOAD_FONT2 // Font 2. Small 16 pixel high font, needs ~3534 bytes in FLASH, 96 characters
#define LOAD_FONT4 // Font 4. Medium 26 pixel high font, needs ~5848 bytes in FLASH, 96 characters
#define LOAD_FONT6 // Font 6. Large 48 pixel font, needs ~2666 bytes in FLASH, only characters 1234567890:-.apm
#define LOAD_FONT7 // Font 7. 7 segment 48 pixel font, needs ~2438 bytes in FLASH, only characters 1234567890:-.
#define LOAD_FONT8 // Font 8. Large 75 pixel font needs ~3256 bytes in FLASH, only characters 1234567890:-.
//#define LOAD_FONT8N // Font 8. Alternative to Font 8 above, slightly narrower, so 3 digits fit a 160 pixel TFT
#define LOAD_GFXFF // FreeFonts. Include access to the 48 Adafruit_GFX free fonts FF1 to FF48 and custom fonts

#define SMOOTH_FONT

//#define SPI_FREQUENCY        27000000
//#define SPI_FREQUENCY        40000000
#define SPI_FREQUENCY          80000000

#define USE_HSPI_PORT // fix?
#define DISABLE_ALL_LIBRARY_WARNINGS //disable touch gpio warnings
