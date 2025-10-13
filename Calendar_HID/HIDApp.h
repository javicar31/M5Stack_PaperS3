#ifndef HIDAPP_H
#define HIDAPP_H

#include <M5Unified.h>

// Public API for the USB HID Keyboard + Touchpad app
// Call hid_begin() once from setup() (after M5.begin).
// In loop(): if (hid_tick()) return;  // HID handled this frame
// To enter:  hid_setActive(true);
// To know if it just exited (so you can redraw calendar once): if (hid_justExited()) { /* redraw */ }

void hid_begin();                 // init USB once (idempotent)
void hid_setActive(bool on);      // enter/exit HID app
bool hid_isActive();              // current active state
bool hid_justExited();            // true once after exiting back to calendar
bool hid_tick();                  // draw + input; returns true if HID handled the frame

#endif // HIDAPP_H
