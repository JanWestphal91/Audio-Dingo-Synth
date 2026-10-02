// =============================================================================
//  encoder.h — Debounced rotary encoder decoding
// =============================================================================
//
//  How a rotary encoder works
//  --------------------------
//  A mechanical encoder has two switches (A and B) that open and close slightly
//  out of phase while you turn the knob. Reading both pins as a 2-bit number
//  (A = high bit, B = low bit) gives a repeating Gray-code sequence, in which
//  only ONE bit changes per step:
//
//      clockwise:          11 -> 01 -> 00 -> 10 -> 11
//      counter-clockwise:  11 -> 10 -> 00 -> 01 -> 11
//
//  With pull-up resistors, "11" (both contacts open) is the resting position
//  in each detent. One click of the knob = four transitions.
//
//  How this decoder works
//  ----------------------
//  1. Every time the pins change, the pair (previous reading, new reading)
//     is looked up in a 16-entry table that says: was this a step forward
//     (+1), a step backward (-1), or invalid / no change (0)?
//     "Invalid" means both bits changed at once — physically impossible for a
//     Gray code, so it must be noise and is ignored.
//
//  2. The +1/-1 values are added up in an accumulator.
//
//  3. Only when the encoder is back in its resting position ("11") do we look
//     at the accumulator: a clear positive or negative sum counts as one click
//     in that direction. Then the accumulator is reset.
//
//  Why this debounces: contact bounce makes the reading flicker between two
//  neighbouring states, e.g. 11 -> 01 -> 11. That adds +1 and then -1, so the
//  sum returns to 0 and no click is reported. Only real movement through the
//  whole sequence adds up to a full click.
// =============================================================================
#pragma once

#include <Arduino.h>

// Resting position of a detented encoder with pull-ups (A = 1, B = 1).
const uint8_t ENC_REST = 0b11;

// Direction of each transition, indexed by (previous << 2) | current.
//   +1 = one step clockwise, -1 = one step counter-clockwise, 0 = none/invalid
const int8_t ENC_TRANSITION[16] = {
  // current:  00  01  10  11
               0, -1, +1,  0,   // previous 00
              +1,  0,  0, -1,   // previous 01
              -1,  0,  0, +1,   // previous 10
               0, +1, -1,  0,   // previous 11
};

// State of one encoder.
struct RotaryEncoder {
  uint8_t pinA;
  uint8_t pinB;
  uint8_t lastPins;  // previous 2-bit reading (A << 1 | B); start with ENC_REST
  int8_t  accum;     // sum of transitions since the last resting position
  int     steps;     // completed clicks since last read: >0 clockwise, <0 counter-clockwise
};

// Read the pins and update the decoder.
// Call this as often as possible — a missed transition can mean a missed click.
inline void encoderPoll(RotaryEncoder &enc) {
  uint8_t pins = (digitalRead(enc.pinA) << 1) | digitalRead(enc.pinB);
  if (pins == enc.lastPins) return;               // nothing changed

  enc.accum += ENC_TRANSITION[(enc.lastPins << 2) | pins];
  enc.lastPins = pins;

  if (pins == ENC_REST) {
    // A full click is +4 / -4. Accepting +-2 or more tolerates one missed
    // transition when the knob is turned quickly.
    if      (enc.accum >=  2) enc.steps++;
    else if (enc.accum <= -2) enc.steps--;
    enc.accum = 0;
  }
}

// Return the accumulated clicks and reset the counter.
inline int encoderTakeSteps(RotaryEncoder &enc) {
  int s = enc.steps;
  enc.steps = 0;
  return s;
}
