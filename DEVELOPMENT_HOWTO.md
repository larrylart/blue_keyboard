## Development Setup (Arduino IDE)

### Requirements

- **ESP32 Board Library**: `3.3.1+`  
- Additional libraries:
  - `Adafruit_SPIFlash`
  - `FastLED`
  - `NimBLE-Arduino`
  - `TFT_eSPI` (customized)

![Arduino Settings](doc/arduino_studio_settings.jpg)

### TFT Screen

- The stock `TFT_eSPI` in Arduino IDE did not work out of the box  
- You need to get the one provided by LilyGO:  
  - [T-Dongle-S3 libs](https://github.com/Xinyuan-LilyGO/T-Dongle-S3/tree/main/lib)  
  - Copy from LilyGO TFT_eSPI/User_Setup.h, TFT_eSPI/User_Setup_Select.h, User_Setups directory your Arduino `libraries/TFT_eSPI` folder.
  - also copy LilyGO `lv_conf.h` manually into your Arduino `libraries/` folder.
  - edit `libraries/TFT_eSPI\User_Setups\Setup47_ST7735.h` and add these lines:
  ```
  #define USE_HSPI_PORT // fix for t-dongle-s3 with newer board versions
  #define DISABLE_ALL_LIBRARY_WARNINGS //disable touch gpio warnings.
  ```

To get the display to work, edit `/Arduino/libraries/TFT_eSPI/User_Setup_Select.h` and make sure you have **one** of the following includes:

- **LilyGO ESP32-S3 T-Dongle**
```cpp
#include <User_Setups/Setup47_ST7735.h>
```
- **Waveshare ESP32-S3 1.47\" Display**
```cpp
#include <User_Setups/Setup_Waveshare_ESP32S3_147.h>
```

- **LilyGO ESP32-S3 T-QT**
```cpp
#include <User_Setups/Setup211_LilyGo_T_QT_Pro_S3.h>
```

Copy the relevant file (`Setup47_ST7735.h`, `Setup_Waveshare_ESP32S3_147.h`, `Setup211_LilyGo_T_QT_Pro_S3.h`, etc.) from `blue_keyboard/lib/` folder to `/Arduino/libraries/TFT_eSPI/User_Setups/`.

---
For qrcode library, you will need to rename qrcode.c and qrcode.h (in /Arduino/libraries/QRCode/src/) to my_qrcode.c respectively my_qrcode.h because it conflict with internal ESP qrcode. 
