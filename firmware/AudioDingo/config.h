// =============================================================================
//  config.h — Hardware configuration for AudioDingo
// =============================================================================
//
//  Everything that depends on how *your* board is wired lives here.
//  If you build your own version with a different pinout, this should be
//  the only file you need to touch.
//
//  All buttons are wired between the pin and GND and use the internal
//  pull-up resistor, so a pressed button reads LOW.
// =============================================================================
#pragma once

#include <Arduino.h>

// -----------------------------------------------------------------------------
//  Note keys
// -----------------------------------------------------------------------------
// Seven keys, one per note of a C major scale (see NOTE_MIDI_NUMBERS below).
// Order matters: index 0 is the lowest note.
const uint8_t NOTE_BUTTON_PINS[] = {12, 11, 10, 9, 8, 7, 6};
const int     NUM_NOTE_BUTTONS   = 7;

// MIDI note numbers played by the seven keys: C4 D4 E4 F4 G4 A4 B4.
// Change these to play a different scale.
const uint8_t NOTE_MIDI_NUMBERS[] = {60, 62, 64, 65, 67, 69, 71};

// -----------------------------------------------------------------------------
//  Function buttons
// -----------------------------------------------------------------------------
const uint8_t MODE_BUTTON_PIN  = 3;   // chord mode on/off, "shift" for both encoders
const uint8_t WAVE_BUTTON_PIN  = A4;  // cycle waveform / long press: animation screen
const uint8_t CHORD_BUTTON_PIN = A5;  // next chord type / random patch, "shift" for encoder 2

// NOTE: On the UNO R4 Minima, A4 and A5 are also the I2C lines (SDA/SCL) used
// by the OLED display. In this build the two buttons share those lines with
// the display. If you build your own, consider moving them to free pins
// (e.g. D13, or D0/D1 if you don't need the serial pins).

// -----------------------------------------------------------------------------
//  Rotary encoders (quadrature, two signal pins each)
// -----------------------------------------------------------------------------
const uint8_t ENC1_PIN_A = 4;
const uint8_t ENC1_PIN_B = 5;
const uint8_t ENC2_PIN_A = 2;
const uint8_t ENC2_PIN_B = A3;

// -----------------------------------------------------------------------------
//  Analog joystick
// -----------------------------------------------------------------------------
const uint8_t JOY_X_PIN = A1;
const uint8_t JOY_Y_PIN = A2;

const int JOY_CENTER_X        = 512;  // ADC reading at rest (10-bit ADC: 0..1023)
const int JOY_CENTER_Y        = 512;
const int JOY_DEAD_ZONE       = 40;   // ignore small movements around the center
const int JOY_FULL_DEFLECTION = 250;  // distance from center that counts as "100 %"

// -----------------------------------------------------------------------------
//  Audio amplifier
// -----------------------------------------------------------------------------
// Pin driven HIGH when a note starts and LOW when the voice is silent,
// intended for the shutdown (SD) input of a small class-D amplifier.
//
// NOTE: On the UNO R4, Mozzi sends its audio through the on-board DAC,
// which is hard-wired to pin A0. The amplifier's audio input therefore
// connects to A0. See the "Audio output" section in the README.
const uint8_t AMP_ENABLE_PIN = A0;

// -----------------------------------------------------------------------------
//  OLED display (SSD1306, I2C)
// -----------------------------------------------------------------------------
const int     SCREEN_WIDTH   = 128;
const int     SCREEN_HEIGHT  = 32;
const int8_t  OLED_RESET_PIN = -1;    // -1 = display shares the Arduino's reset line
const uint8_t OLED_I2C_ADDR  = 0x3C;
