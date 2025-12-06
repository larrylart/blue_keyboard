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
  // No serial, but keep a tiny delay to preserve timing - chasing a ephemeral bug caused by timing - works with debug enabled
//  #define DPRINT(...)    do { delayMicroseconds(50); } while (0)
//  #define DPRINTLN(...)  do { delayMicroseconds(50); } while (0)
#endif

#endif // DEBUG_UTILS_H
