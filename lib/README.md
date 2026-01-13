
To get the display to work, edit `/Arduino/libraries/TFT_eSPI/User_Setup_Select.h` and make sure you have **one** of the following includes:

- **LilyGO ESP32-S3 T-Dongle**
```cpp
#include <User_Setups/Setup47_ST7735.h>
```
- **Waveshare ESP32-S3 1.47\" Display**
```cpp
#include <User_Setups/Setup_Waveshare_ESP32S3_147.h>
```

Copy the relevant file (`Setup47_ST7735.h` or `Setup_Waveshare_ESP32S3_147.h`) from this folder to `/Arduino/libraries/TFT_eSPI/User_Setups/`.
