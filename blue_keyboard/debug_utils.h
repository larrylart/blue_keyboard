#ifndef DEBUG_UTILS_H
#define DEBUG_UTILS_H

#include <Arduino.h>

// set to 1 to disable debug everywhere
#define DEBUG_GLOBAL_DISABLED 1

// Master debug switch
#ifndef DEBUG_ENABLED
#define DEBUG_ENABLED 1
#endif

#if (!DEBUG_GLOBAL_DISABLED && DEBUG_ENABLED)
  #define DPRINT(...)    Serial.printf(__VA_ARGS__)
  #define DPRINTLN(...)  Serial.println(__VA_ARGS__)
#else
  #define DPRINT(...)
  #define DPRINTLN(...)
#endif

#endif // DEBUG_UTILS_H
