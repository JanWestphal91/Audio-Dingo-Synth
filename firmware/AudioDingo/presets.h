// =============================================================================
//  presets.h — Sound presets, waveforms and chord definitions
// =============================================================================
//
//  A preset is a complete "patch": it sets every parameter of the sound engine
//  at once. Selecting a preset (hold MODE + turn encoder 2) copies its values
//  into the live parameters, which can then be tweaked further.
//
//  To add your own sound, append a row to PRESETS[] and a 3-letter name to
//  PRESET_NAMES[] — NUM_PRESETS is derived from the table automatically.
// =============================================================================
#pragma once

#include <Arduino.h>

// -----------------------------------------------------------------------------
//  Waveforms
// -----------------------------------------------------------------------------
// Each voice consists of two oscillators ("a" and "b"). What "b" does depends
// on the waveform:
//   SAW / PULSE / TRI : b is a slightly detuned copy of a  -> thick, chorus-like
//   SINE              : b is unused                        -> pure tone
//   FM                : b modulates the phase of a         -> bells, metallic tones
//   NOISE             : neither oscillator is used         -> white noise
enum Waveform : uint8_t {
  WAVE_SAW,
  WAVE_PULSE,
  WAVE_TRI,
  WAVE_SINE,
  WAVE_FM,
  WAVE_NOISE,
  NUM_WAVEFORMS
};

// Short names for the 128x32 display (index = Waveform value).
const char* const WAVE_NAMES[NUM_WAVEFORMS] = {"Saw", "Pul", "Tri", "Sin", "FM", "Nse"};

// -----------------------------------------------------------------------------
//  LFO destinations
// -----------------------------------------------------------------------------
// The preset LFO (low-frequency oscillator) is a slow sine wave that can either
// wobble the pitch (vibrato) or the filter cutoff (wah-like movement).
enum LfoDestination : uint8_t {
  LFO_TO_PITCH,
  LFO_TO_FILTER
};

// -----------------------------------------------------------------------------
//  Preset structure
// -----------------------------------------------------------------------------
struct Preset {
  // Amplitude envelope (ADSR) — times in milliseconds, sustain as level 0..255
  uint16_t attack, decay;
  uint8_t  sustain;
  uint16_t release;

  // Low-pass filter — all values 0..255
  uint8_t  cutoff;          // base cutoff
  uint8_t  resonance;       // emphasis at the cutoff frequency
  uint8_t  filterEnvDepth;  // how far the filter envelope opens the filter

  int8_t   octave;          // octave offset added to the played note

  uint8_t  wave;            // one of Waveform

  // FM synthesis (only used with WAVE_FM)
  float    fmRatio;         // modulator frequency = carrier frequency * fmRatio
  uint8_t  fmDepth;         // modulation index; 0 = no FM

  uint8_t  noiseMix;        // amount of white noise mixed in, 0..255

  float    detune;          // frequency ratio of oscillator b (1.0 = in tune)

  // Pitch envelope: the note starts `pitchDrop` semitones higher and slides
  // down to the target pitch within `pitchTime` ms. Great for kicks and blips.
  int8_t   pitchDrop;
  uint16_t pitchTime;       // 0 = off

  uint8_t  crush;           // sample-rate reduction: 1 = off, higher = grittier

  // Preset LFO
  uint8_t  lfoRate;         // in 0.1 Hz steps (50 = 5 Hz); 0 = off
  uint8_t  lfoDepth;
  uint8_t  lfoDest;         // one of LfoDestination
};

// -----------------------------------------------------------------------------
//  Preset table
// -----------------------------------------------------------------------------
const Preset PRESETS[] = {
  // atk  dcy  sus  rel   cut  res  fEnv  oct  wave        fmR   fmD  nse  detune  pDrop pTime crush  lfoR lfoD  lfoDest
  {    5, 180, 170, 120,  230,  40, 120,   0,  WAVE_SAW,   1.0f,   0,   0, 1.004f,    0,    0,    1,    50,   8, LFO_TO_PITCH  }, // Lead: detuned saw + vibrato
  {    3, 160, 120,  80,  200,  70, 150,  -1,  WAVE_PULSE, 1.0f,   0,   0, 1.001f,    0,    0,    3,     0,   0, LFO_TO_PITCH  }, // Bass: square + bit crush
  {    2, 320,  40, 220,  255,  20,  60,   0,  WAVE_FM,    3.5f,  90,   0, 1.000f,    0,    0,    1,     6,  40, LFO_TO_FILTER }, // Bell: FM + slow filter wobble
  {    4, 150, 180, 100,  150,  30, 110,  -1,  WAVE_FM,    1.0f,  50,  40, 1.000f,    5,   40,    2,     0,   0, LFO_TO_PITCH  }, // Sub: soft FM + blip + grit
  {    1,  90,   0,  60,  220,  40, 120,   0,  WAVE_SINE,  1.0f,   0,  70, 1.000f,   36,   90,    1,     0,   0, LFO_TO_PITCH  }, // Drum: sine kick with pitch drop + noise click
};
const int NUM_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

// Display names (3 characters each, index = preset number).
const char* const PRESET_NAMES[NUM_PRESETS] = {"Ld ", "Bas", "Bel", "Sub", "Drm"};

// -----------------------------------------------------------------------------
//  Chords
// -----------------------------------------------------------------------------
// In chord mode every key plays three voices: the root note plus two more
// notes, given here as semitone offsets from the root.
const int NUM_CHORD_TYPES = 4;
const char* const CHORD_NAMES[NUM_CHORD_TYPES] = {"Dur", "Mol", "Su4", "Oct"};
const int8_t CHORD_INTERVALS[NUM_CHORD_TYPES][2] = {
  { 4,  7},   // major:  root + major third + fifth
  { 3,  7},   // minor:  root + minor third + fifth
  { 5,  7},   // sus4:   root + fourth + fifth
  {12, 24},   // octaves: root + 1 octave + 2 octaves
};
