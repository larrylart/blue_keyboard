#pragma once

#include "settings.h"

// esp32-s3-zero - no diplay
// rgb pin 21

// Default (safe): current T-Dongle-S3 mapping
#if (BLUKEY_BOARD == BLUKEY_BOARD_LILYGO_TDONGLE_S3) || !defined(BLUKEY_BOARD)

#define BTN_PIN     0

// lilygo t-dongle display version
#define LED_DI_PIN     40
#define LED_CI_PIN     39

#define TFT_CS_PIN     4
#define TFT_SDA_PIN    3
#define TFT_SCL_PIN    5
#define TFT_DC_PIN     2
#define TFT_RES_PIN    1
#define TFT_LEDA_PIN   38

#define SD_MMC_D0_PIN  14
#define SD_MMC_D1_PIN  17
#define SD_MMC_D2_PIN  21
#define SD_MMC_D3_PIN  18
#define SD_MMC_CLK_PIN 12
#define SD_MMC_CMD_PIN 16

// Waveshare ESP32-S3 1.47" Display
#elif (BLUKEY_BOARD == BLUKEY_BOARD_WAVESHARE_ESP32S3_DISPLAY147)

#define BTN_PIN     	0
#define LED_DI_PIN     38 // WS2812B-0807

#define TFT_CS_PIN     42   // LCD_CS
#define TFT_SDA_PIN    45   // LCD_DIN (MOSI)
#define TFT_SCL_PIN    40   // LCD_CLK (SCK)
#define TFT_DC_PIN     41   // LCD_DC
#define TFT_RES_PIN    39   // LCD_RST
#define TFT_LEDA_PIN   48   // LCD_BL (backlight)

// Headless board mapping (no TFT) waveshare s3 zero
#elif (BLUKEY_BOARD == BLUKEY_BOARD_ESP32S3_ZERO)
	
#define BTN_PIN     	0
#define LED_DI_PIN     21

// Unknown model: fall back to current/default mapping (lilygo t-dongle)
// probably need to tidy up this
#else

  #define BTN_PIN        0

  #define LED_DI_PIN     40
  #define LED_CI_PIN     39

  #define TFT_CS_PIN     4
  #define TFT_SDA_PIN    3
  #define TFT_SCL_PIN    5
  #define TFT_DC_PIN     2
  #define TFT_RES_PIN    1
  #define TFT_LEDA_PIN   38

  #define SD_MMC_D0_PIN  14
  #define SD_MMC_D1_PIN  17
  #define SD_MMC_D2_PIN  21
  #define SD_MMC_D3_PIN  18
  #define SD_MMC_CLK_PIN 12
  #define SD_MMC_CMD_PIN 16

#endif
