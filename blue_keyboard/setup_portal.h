#pragma once
#include <Arduino.h>

// Launch the first-run setup portal (blocking until saved or timeout).
// Returns true if setup completed - false if aborted.
bool runSetupPortal();
