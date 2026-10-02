// =============================================================================
//  AudioDingo — a pocket synthesizer for the Arduino UNO R4 Minima
// =============================================================================
//
//  Copyright (c) 2026 Jan Westphal — MIT License (see LICENSE)
//  Project write-up: https://janwestphal.dev/blog/hello-blog/
//
//  A small, battery-powered, 3-voice synthesizer built on the Mozzi audio
//  library. Each preset defines its own waveform, FM, noise, pitch envelope,
//  bit crush and LFO, so the few controls can produce very different sounds —
//  from leads and basses to bells and drum hits.
//
//  ---------------------------------------------------------------------------
//  Controls
//  ---------------------------------------------------------------------------
//   7 note keys          play a note (or a 3-note chord in chord mode)
//
//   Encoder 1            change the selected envelope value (A, D, S or R)
//     + hold MODE        select which envelope value to edit
//
//   Encoder 2            master volume (1..15)
//     + hold MODE        select preset
//     + hold CHORD       octave shift (only in chord mode)
//
//   MODE   (tap)         chord mode on/off
//   CHORD  (tap)         chord mode: next chord type / otherwise: random patch
//   WAVE   (tap)         next waveform
//   WAVE   (hold 0.4 s)  toggle the animated "visualizer" screen
//
//   Joystick up          vibrato
//   Joystick down        close the filter (wah)
//   Joystick right       bit crush
//   Joystick left        glide / portamento between notes
//
//  ---------------------------------------------------------------------------
//  How Mozzi structures the program
//  ---------------------------------------------------------------------------
//  Mozzi splits the work into two callbacks with different speeds:
//
//   updateControl()  runs MOZZI_CONTROL_RATE times per second (here 128 Hz).
//                    Reads buttons, encoders, joystick; updates envelopes,
//                    pitch, filter and the display. Can be relatively slow.
//
//   updateAudio()    runs MOZZI_AUDIO_RATE times per second (32768 Hz on the
//                    R4). Computes ONE audio sample. Must be very fast — no
//                    analogRead(), no display, no floating-point heavy work.
//
//  loop() only calls audioHook(), which keeps Mozzi's audio buffer filled and
//  calls the two functions above when needed. Never use delay() in a Mozzi sketch.
// =============================================================================

// Must be defined BEFORE including Mozzi.h.
#define MOZZI_CONTROL_RATE 128

#include <Mozzi.h>
#include <Phasor.h>
#include <ADSR.h>
#include <LowPassFilter.h>
#include <mozzi_midi.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "config.h"
#include "presets.h"
#include "encoder.h"

// =============================================================================
//  Global objects
// =============================================================================

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET_PIN);

// Three voices with two oscillators each.
// A Phasor is a simple counter that wraps around once per period. Its 32-bit
// output is the "phase" (position within one waveform cycle), which the wave
// shaper functions below turn into an actual waveform.
//   osc?a = main oscillator (carrier)
//   osc?b = detuned partner OR FM modulator, depending on the waveform
// Voice 1 always plays; voices 2 and 3 are only used in chord mode.
Phasor<MOZZI_AUDIO_RATE> osc1a, osc1b;
Phasor<MOZZI_AUDIO_RATE> osc2a, osc2b;
Phasor<MOZZI_AUDIO_RATE> osc3a, osc3b;

// Two envelopes: one shapes the volume of each note, the other opens and
// closes the filter over the course of a note.
ADSR<MOZZI_CONTROL_RATE, MOZZI_AUDIO_RATE> ampEnvelope;
ADSR<MOZZI_CONTROL_RATE, MOZZI_AUDIO_RATE> filterEnvelope;

// Resonant low-pass filter applied to the mixed voices.
LowPassFilter lowPass;

// Rotary encoders. The pins are passed in swapped order (B, A) on purpose:
// this sets which direction counts as "clockwise" for this particular wiring.
RotaryEncoder encoder1 = {ENC1_PIN_B, ENC1_PIN_A, ENC_REST, 0, 0};
RotaryEncoder encoder2 = {ENC2_PIN_B, ENC2_PIN_A, ENC_REST, 0, 0};

// 256-entry sine lookup table (-127..127), filled in setup().
// Looking up a value is much faster than calling sin() at audio rate.
int8_t sineTable[256];

// =============================================================================
//  Sound parameters (set by presets, partly editable live)
// =============================================================================

// Amplitude envelope: {attack ms, decay ms, sustain level 0..255, release ms}
uint16_t       adsrValues[4] = {5, 150, 180, 80};
const uint16_t ADSR_MIN[4]   = {   1,   20,   0,   20};
const uint16_t ADSR_MAX[4]   = {2000, 2000, 255, 2000};
const int      ADSR_STEP[4]  = {   2,   10,   4,   10};  // change per encoder click
int            adsrEditIndex = 0;                         // which value encoder 1 edits

// Filter
uint8_t filterCutoff    = 220;
uint8_t filterResonance = 30;
uint8_t filterEnvDepth  = 180;

// Oscillators (see presets.h for a description of each parameter)
uint8_t  waveform     = WAVE_SAW;
float    detune       = 1.003f;
float    fmRatio      = 1.0f;
uint8_t  fmDepth      = 0;
uint8_t  noiseMix     = 0;
int8_t   pitchDrop    = 0;
uint16_t pitchDropMs  = 0;
uint8_t  crushAmount  = 1;
uint8_t  lfoRate      = 0;
uint8_t  lfoDepth     = 0;
uint8_t  lfoDest      = LFO_TO_PITCH;
int8_t   presetOctave = 0;

// =============================================================================
//  Performance state
// =============================================================================

int  currentPreset = 0;
int  volume        = 8;      // 1..15, index into VOLUME_CURVE
int  octaveShift   = 0;      // -3..+3, set with CHORD + encoder 2
bool chordMode     = false;
int  chordType     = 0;      // index into CHORD_INTERVALS

// Our ears perceive loudness logarithmically, so equal encoder steps should
// multiply the level, not add to it. Each 3 steps roughly double the level
// (about +6 dB); 256 = full scale.
const uint16_t VOLUME_CURVE[16] = {8, 10, 13, 16, 20, 25, 32, 40, 51, 64, 81, 102, 128, 161, 203, 256};

// Currently sounding note
int           activeNote    = -1;      // index into NOTE_MIDI_NUMBERS, -1 = silent
bool          activeIsChord = false;
unsigned long noteStartMs   = 0;       // for the pitch envelope
int           lastNoteKey   = -1;      // key held during the previous control tick

// Joystick-controlled modulation (updated in readJoystick())
int   joyFilterOffset = 0;      // down:  negative filter offset (wah)
float joyVibratoDepth = 0.0f;   // up:    vibrato depth in semitones
int   joyCrush        = 0;      // right: extra bit crush
float glideSpeed      = 1.0f;   // left:  portamento (1.0 = instant, smaller = slower)
float glidePitch      = 60.0f;  // current (gliding) pitch as a fractional MIDI note

// LFOs — phases run from 0 to 256 (one sine-table cycle), values are -127..127
float vibratoPhase = 0.0f;      // fixed 5.5 Hz LFO used by the joystick vibrato
int   vibratoValue = 0;
float lfoPhase     = 0.0f;      // preset LFO
int   lfoValue     = 0;

// Button edge detection (buttons read LOW while pressed)
bool lastModeState  = HIGH;
bool lastChordState = HIGH;
bool lastWaveState  = HIGH;
bool modeUsedAsShift = false;   // MODE was held while turning an encoder -> don't toggle chord mode
bool waveLongPressed = false;
unsigned long wavePressStartMs = 0;

// Display
bool          animationMode       = false;  // true = show visualizer instead of status
unsigned long randomPatchBannerEndMs = 0;   // show "Happy Accident" message until this time
int           displayTickCounter  = 0;

// =============================================================================
//  Fast random numbers for the noise generator
// =============================================================================
// xorshift32: a tiny pseudo-random generator — just three shifts and XORs —
// fast enough to run once or twice per audio sample.
uint32_t rngState = 0x1234abcdUL;

static inline uint8_t fastRandom8() {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return (uint8_t)rngState;
}

// =============================================================================
//  Sound engine: applying parameters
// =============================================================================

// Push the current ADSR values into both envelopes.
// Sustain time is set to 60 s, i.e. "as long as the key is held".
void applyEnvelopes() {
  ampEnvelope.setADLevels(255, adsrValues[2]);
  ampEnvelope.setTimes(adsrValues[0], adsrValues[1], 60000, adsrValues[3]);

  // The filter envelope uses the same timing but a fixed sustain level.
  filterEnvelope.setADLevels(255, 80);
  filterEnvelope.setTimes(adsrValues[0], adsrValues[1], 60000, adsrValues[3]);
}

// Copy a preset from the table into the live parameters.
void loadPreset(int index) {
  const Preset &p = PRESETS[index];

  adsrValues[0] = p.attack;
  adsrValues[1] = p.decay;
  adsrValues[2] = p.sustain;
  adsrValues[3] = p.release;

  filterCutoff    = p.cutoff;
  filterResonance = p.resonance;
  filterEnvDepth  = p.filterEnvDepth;

  presetOctave = p.octave;
  waveform     = p.wave;
  fmRatio      = p.fmRatio;
  fmDepth      = p.fmDepth;
  noiseMix     = p.noiseMix;
  detune       = p.detune;
  pitchDrop    = p.pitchDrop;
  pitchDropMs  = p.pitchTime;
  crushAmount  = p.crush;
  lfoRate      = p.lfoRate;
  lfoDepth     = p.lfoDepth;
  lfoDest      = p.lfoDest;

  applyEnvelopes();
}

// "Happy accident" button: roll random values for (almost) every parameter.
// The ranges are chosen so the result is usually playable.
void randomizePatch() {
  adsrValues[0] = random(2, 300);
  adsrValues[1] = random(40, 400);
  adsrValues[2] = random(40, 255);
  adsrValues[3] = random(20, 500);

  filterCutoff    = random(80, 255);
  filterResonance = random(5, 120);
  filterEnvDepth  = random(40, 220);

  detune       = 1.000f + random(0, 15) * 0.001f;
  presetOctave = random(-1, 2);
  waveform     = random(0, NUM_WAVEFORMS);

  // Integer and simple fractional ratios give harmonic FM tones,
  // odd ones like 3.5 or 7 give bell-like, inharmonic tones.
  const float FM_RATIOS[] = {1.0f, 1.5f, 2.0f, 3.0f, 3.5f, 5.0f, 7.0f};
  fmRatio = FM_RATIOS[random(0, 7)];
  fmDepth = random(20, 120);

  // Noise and pitch drop only in about one out of three patches
  noiseMix    = (random(0, 3) == 0) ? random(20, 120) : 0;
  pitchDrop   = (random(0, 3) == 0) ? random(6, 40) : 0;
  pitchDropMs = random(50, 200);

  crushAmount = random(1, 5);

  // LFO on in about half of the patches
  lfoRate  = (random(0, 2) == 0) ? random(3, 80) : 0;
  lfoDepth = random(8, 50);
  lfoDest  = random(0, 2);

  applyEnvelopes();
  randomPatchBannerEndMs = millis() + 900;
}

// =============================================================================
//  Sound engine: pitch
// =============================================================================

// Recalculate the frequency of every active oscillator.
// Called on every control tick, because glide, pitch envelope and vibrato all
// change the pitch continuously while a note is held.
void updatePitch() {
  if (activeNote < 0) return;

  // Glide: move a fraction of the remaining distance towards the target note on
  // every tick. glideSpeed 1.0 jumps immediately; small values slide slowly.
  float targetPitch = NOTE_MIDI_NUMBERS[activeNote] + (octaveShift + presetOctave) * 12;
  glidePitch += glideSpeed * (targetPitch - glidePitch);

  // Pitch envelope: start `pitchDrop` semitones higher, fall linearly to 0.
  float drop = 0.0f;
  if (pitchDrop != 0 && pitchDropMs > 0) {
    unsigned long elapsed = millis() - noteStartMs;
    if (elapsed < pitchDropMs) drop = pitchDrop * (1.0f - (float)elapsed / pitchDropMs);
  }

  // Preset LFO as vibrato (in semitones)
  float lfoVibrato = (lfoDest == LFO_TO_PITCH) ? (lfoValue / 127.0f) * lfoDepth * 0.05f : 0.0f;

  // Joystick vibrato (in semitones)
  float joyVibrato = (vibratoValue / 127.0f) * joyVibratoDepth;

  // Oscillator b runs at a multiple of oscillator a:
  //   FM:     the modulator ratio (e.g. 3.5 for bells)
  //   others: the detune factor (e.g. 1.004 -> slightly sharp -> beating/chorus)
  float partnerRatio = (waveform == WAVE_FM) ? fmRatio : detune;

  // Everything above is in (fractional) MIDI notes; mtof() converts to Hz.
  float pitch = glidePitch + drop + lfoVibrato + joyVibrato;

  float freq1 = mtof(pitch);
  osc1a.setFreq(freq1);
  osc1b.setFreq(freq1 * partnerRatio);

  if (activeIsChord) {
    float freq2 = mtof(pitch + CHORD_INTERVALS[chordType][0]);
    float freq3 = mtof(pitch + CHORD_INTERVALS[chordType][1]);
    osc2a.setFreq(freq2);
    osc2b.setFreq(freq2 * partnerRatio);
    osc3a.setFreq(freq3);
    osc3b.setFreq(freq3 * partnerRatio);
  }
}

// =============================================================================
//  Sound engine: note on / note off
// =============================================================================

void noteOn(int noteIndex, bool asChord) {
  activeNote    = noteIndex;
  activeIsChord = asChord;
  noteStartMs   = millis();
  ampEnvelope.noteOn(true);      // true = restart the envelope from zero
  filterEnvelope.noteOn(true);
  digitalWrite(AMP_ENABLE_PIN, HIGH);
  updatePitch();
}

// Starts the release phase; the note fades out according to the envelope.
void noteOff() {
  ampEnvelope.noteOff();
  filterEnvelope.noteOff();
  activeNote    = -1;
  activeIsChord = false;
}

// Monophonic key handling: only (re)trigger when the pressed key changes.
// Retriggering on every tick would restart the envelope constantly.
void handleNoteKeys(int pressedKey) {
  if (pressedKey == lastNoteKey) return;
  if (pressedKey >= 0) noteOn(pressedKey, chordMode);
  else                 noteOff();
  lastNoteKey = pressedKey;
}

// Returns the index of the lowest pressed note key, or -1 if none is pressed.
int readNoteKeys() {
  for (int i = 0; i < NUM_NOTE_BUTTONS; i++) {
    if (digitalRead(NOTE_BUTTON_PINS[i]) == LOW) return i;
  }
  return -1;
}

// =============================================================================
//  LFOs
// =============================================================================

// Advance both LFOs by one control tick.
// Phase increment per tick = frequency / tick rate * table length.
void updateLfos() {
  // Fixed 5.5 Hz LFO for the joystick vibrato (always running)
  vibratoPhase += 5.5f / MOZZI_CONTROL_RATE * 256.0f;
  while (vibratoPhase >= 256.0f) vibratoPhase -= 256.0f;
  vibratoValue = sineTable[(uint8_t)vibratoPhase];

  // Preset LFO (lfoRate is in 0.1 Hz steps)
  if (lfoRate == 0) {
    lfoValue = 0;
    return;
  }
  lfoPhase += (float)lfoRate * 0.1f / MOZZI_CONTROL_RATE * 256.0f;
  while (lfoPhase >= 256.0f) lfoPhase -= 256.0f;
  lfoValue = sineTable[(uint8_t)lfoPhase];
}

// =============================================================================
//  Inputs
// =============================================================================

// Encoder 1: edit the envelope. With MODE held, choose which value to edit.
void handleEncoder1() {
  int steps = encoderTakeSteps(encoder1);
  if (steps == 0) return;
  int dir = (steps > 0) ? 1 : -1;
  int count = abs(steps);

  if (digitalRead(MODE_BUTTON_PIN) == LOW) {
    // Wrap around 0..3 (the "+ 4) % 4" keeps the result positive)
    adsrEditIndex   = ((adsrEditIndex + dir * count) % 4 + 4) % 4;
    modeUsedAsShift = true;
  } else {
    adsrValues[adsrEditIndex] = (uint16_t)constrain(
      (int)adsrValues[adsrEditIndex] + dir * count * ADSR_STEP[adsrEditIndex],
      ADSR_MIN[adsrEditIndex], ADSR_MAX[adsrEditIndex]);
    applyEnvelopes();
  }
  animationMode = false;
}

// Encoder 2: volume. With MODE held: preset. With CHORD held in chord mode: octave.
void handleEncoder2() {
  int steps = encoderTakeSteps(encoder2);
  if (steps == 0) return;
  int dir = (steps > 0) ? 1 : -1;
  int count = abs(steps);

  if (digitalRead(CHORD_BUTTON_PIN) == LOW && chordMode) {
    octaveShift = constrain(octaveShift + dir * count, -3, 3);
  } else if (digitalRead(MODE_BUTTON_PIN) == LOW) {
    currentPreset   = ((currentPreset + dir * count) % NUM_PRESETS + NUM_PRESETS) % NUM_PRESETS;
    loadPreset(currentPreset);
    modeUsedAsShift = true;
  } else {
    volume = constrain(volume + dir * count, 1, 15);
  }
  animationMode = false;
}

// CHORD button, acts on press:
//   animation screen visible -> leave it
//   chord mode               -> next chord type
//   otherwise                -> random patch
void handleChordButton() {
  bool state = digitalRead(CHORD_BUTTON_PIN);

  if (lastChordState == HIGH && state == LOW) {
    if      (animationMode) animationMode = false;
    else if (chordMode)     chordType = (chordType + 1) % NUM_CHORD_TYPES;
    else                    randomizePatch();
  }
  lastChordState = state;
}

// MODE button, acts on release, so it can double as a "shift" key:
// if an encoder was turned while it was held, releasing it does nothing.
void handleModeButton() {
  bool state = digitalRead(MODE_BUTTON_PIN);

  if (lastModeState == HIGH && state == LOW) {        // pressed
    modeUsedAsShift = false;
  }
  if (lastModeState == LOW && state == HIGH) {        // released
    if (!modeUsedAsShift) {
      if (animationMode) animationMode = false;
      else               chordMode = !chordMode;
    }
    modeUsedAsShift = false;
  }
  lastModeState = state;
}

// WAVE button: short press = next waveform, long press (400 ms) = animation.
// The long press fires while still holding; the short press fires on release.
void handleWaveButton() {
  bool state = digitalRead(WAVE_BUTTON_PIN);

  if (lastWaveState == HIGH && state == LOW) {        // pressed
    wavePressStartMs = millis();
    waveLongPressed  = false;
  }

  if (state == LOW && !waveLongPressed && millis() - wavePressStartMs >= 400) {
    waveLongPressed = true;
    animationMode   = !animationMode;
  }

  if (lastWaveState == LOW && state == HIGH) {        // released
    if (!waveLongPressed) {
      if (animationMode) {
        animationMode = false;
      } else {
        waveform = (waveform + 1) % NUM_WAVEFORMS;
        // Retrigger a held note so the new waveform is heard right away
        if (activeNote >= 0) noteOn(activeNote, activeIsChord);
      }
    }
    waveLongPressed = false;
  }
  lastWaveState = state;
}

// Joystick: each of the four directions controls a separate effect.
void readJoystick() {
  int x = analogRead(JOY_X_PIN) - JOY_CENTER_X;
  int y = analogRead(JOY_Y_PIN) - JOY_CENTER_Y;
  if (abs(x) < JOY_DEAD_ZONE) x = 0;
  if (abs(y) < JOY_DEAD_ZONE) y = 0;

  // Deflection per direction, normalized to 0..1.
  // (If your joystick is mounted differently, swap the signs here.)
  float down  = (y > 0) ? constrain( y / (float)JOY_FULL_DEFLECTION, 0.0f, 1.0f) : 0.0f;
  float up    = (y < 0) ? constrain(-y / (float)JOY_FULL_DEFLECTION, 0.0f, 1.0f) : 0.0f;
  float left  = (x < 0) ? constrain(-x / (float)JOY_FULL_DEFLECTION, 0.0f, 1.0f) : 0.0f;
  float right = (x > 0) ? constrain( x / (float)JOY_FULL_DEFLECTION, 0.0f, 1.0f) : 0.0f;

  joyFilterOffset = -(int)(down * 200.0f);   // down:  close filter by up to 200
  joyVibratoDepth = up * 0.6f;               // up:    vibrato up to +/-0.6 semitones
  joyCrush        = (int)(right * 6.0f);     // right: add 0..6 crush steps
  glideSpeed      = 1.0f - left * 0.97f;     // left:  glide from instant (1.0) to slow (0.03)
}

// =============================================================================
//  Display
// =============================================================================

// "Plasma" visualizer: a 32x8 grid of dots whose size follows the sum of four
// moving sine waves. Purely decorative.
void drawAnimation() {
  display.clearDisplay();
  float t = millis() / 700.0f;

  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 32; col++) {
      float x = col / 8.0f;
      float y = row / 4.0f;
      float v = sin(x + t)
              + sin(y + t * 0.7f)
              + sin((x + y) * 0.5f + t * 1.3f)
              + sin(sqrt(x * x + y * y + 0.001f) + t * 0.9f);
      float level = (v + 4.0f) / 8.0f;       // sum of 4 sines (-4..4) -> 0..1

      int px = col * 4, py = row * 4;
      if      (level > 0.66f) display.fillRect(px, py, 3, 3, SSD1306_WHITE);
      else if (level > 0.33f) display.fillRect(px, py, 2, 2, SSD1306_WHITE);
      else if (level > 0.15f) display.drawPixel(px, py, SSD1306_WHITE);
    }
  }
  display.display();
}

// Status screen:
//   line 1: preset, waveform, volume, octave
//   line 2: envelope values, the one being edited in [brackets]
//   line 3: detune, crush indicator, chord type ("RND" = CHORD button randomizes)
void drawStatus() {
  // Sending a frame over I2C takes a few milliseconds, during which the audio
  // buffer is not refilled — audible as clicks on held notes. So we only redraw
  // when something visible has changed. To detect that cheaply, all displayed
  // values are combined into a single hash ("signature") and compared to the
  // last one drawn.
  bool showBanner = (millis() < randomPatchBannerEndMs);
  uint32_t signature = 0xFFFFFFFFUL;        // constant while the banner is shown -> drawn once
  if (!showBanner) {
    signature = currentPreset;
    signature = signature * 31 + waveform;
    signature = signature * 31 + (uint32_t)volume;
    signature = signature * 31 + (uint32_t)(octaveShift + 8);
    signature = signature * 31 + adsrEditIndex;
    for (int i = 0; i < 4; i++) signature = signature * 131 + adsrValues[i];
    signature = signature * 31 + (uint32_t)(detune * 1000.0f);
    signature = signature * 31 + crushAmount;
    signature = signature * 31 + (chordMode ? (10 + chordType) : 0);
  }
  static uint32_t lastSignature = 0xABCDEF01UL;
  if (signature == lastSignature) return;
  lastSignature = signature;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  if (showBanner) {
    display.setCursor(0, 0);  display.print("Happy Accident");
    display.setCursor(0, 12); display.print("Wave+FM+Noise");
    display.setCursor(0, 22); display.print("Rnd patch set");
    display.display();
    return;
  }

  // Line 1
  display.setCursor(0, 0);
  display.print(PRESET_NAMES[currentPreset]);
  display.print(" ");  display.print(WAVE_NAMES[waveform]);
  display.print(" V"); display.print(volume);
  display.print(" O"); display.print(octaveShift);

  // Line 2
  display.setCursor(0, 11);
  const char* labels = "ADSR";
  for (int i = 0; i < 4; i++) {
    if (i) display.print(" ");
    if (adsrEditIndex == i) display.print("[");
    display.print(labels[i]);
    display.print(adsrValues[i]);
    if (adsrEditIndex == i) display.print("]");
  }

  // Line 3
  display.setCursor(0, 21);
  display.print("Dt "); display.print(detune, 3);
  display.print(crushAmount > 1 ? " Crsh " : " ");
  display.print(chordMode ? CHORD_NAMES[chordType] : "RND");
  display.display();
}

// Called every control tick; throttles how often the display is updated.
void updateDisplay() {
  if (++displayTickCounter < 8) return;      // every 8th tick = 16 times per second
  displayTickCounter = 0;

  if (animationMode) {
    drawAnimation();
  } else {
    static int statusDivider = 0;
    if (++statusDivider >= 4) {              // status: every 32nd tick = 4 times per second
      statusDivider = 0;
      drawStatus();
    }
  }
}

// =============================================================================
//  Setup
// =============================================================================

void setup() {
  // Inputs (all buttons pull to GND when pressed)
  for (int i = 0; i < NUM_NOTE_BUTTONS; i++) pinMode(NOTE_BUTTON_PINS[i], INPUT_PULLUP);
  pinMode(ENC1_PIN_A, INPUT_PULLUP);
  pinMode(ENC1_PIN_B, INPUT_PULLUP);
  pinMode(ENC2_PIN_A, INPUT_PULLUP);
  pinMode(ENC2_PIN_B, INPUT_PULLUP);
  pinMode(MODE_BUTTON_PIN,  INPUT_PULLUP);
  pinMode(WAVE_BUTTON_PIN,  INPUT_PULLUP);
  pinMode(CHORD_BUTTON_PIN, INPUT_PULLUP);

  pinMode(AMP_ENABLE_PIN, OUTPUT);
  digitalWrite(AMP_ENABLE_PIN, LOW);

  // Pre-compute one sine cycle into the lookup table
  for (int i = 0; i < 256; i++) {
    sineTable[i] = (int8_t)(sinf(i * TWO_PI / 256.0f) * 127.0f);
  }

  // Seed both random generators from analog noise on the joystick pin
  randomSeed(analogRead(JOY_Y_PIN));
  rngState = ((uint32_t)analogRead(JOY_Y_PIN) << 16) ^ micros() ^ 0x9e3779b9UL;
  if (rngState == 0) rngState = 1;           // xorshift gets stuck at 0

  loadPreset(currentPreset);
  lowPass.setCutoffFreqAndResonance(filterCutoff, filterResonance);

  // Display. If it is not found, stop here (check wiring and I2C address).
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    for (;;) {}
  }
  Wire.setClock(400000);                     // 400 kHz I2C: shorter display transfers
  drawStatus();

  startMozzi();
}

// =============================================================================
//  Mozzi callback: control rate (128 Hz)
// =============================================================================

void updateControl() {
  // 1. Read controls
  handleEncoder1();
  handleEncoder2();
  handleChordButton();
  handleModeButton();
  handleWaveButton();
  int pressedKey = readNoteKeys();
  readJoystick();

  // 2. Modulation sources
  updateLfos();

  // 3. Notes and pitch (keys are ignored while the animation screen is shown)
  if (!animationMode) handleNoteKeys(pressedKey);
  updatePitch();

  // 4. Envelopes and filter
  ampEnvelope.update();
  filterEnvelope.update();

  // Final cutoff = base + envelope + joystick + LFO, clamped to the valid range.
  // (env * depth) >> 8 scales the envelope (0..255) by depth/256.
  int lfoFilter = (lfoDest == LFO_TO_FILTER) ? (lfoValue * lfoDepth) / 127 : 0;
  int cutoff = filterCutoff
             + ((filterEnvelope.next() * (int)filterEnvDepth) >> 8)
             + joyFilterOffset
             + lfoFilter;
  lowPass.setCutoffFreqAndResonance((uint8_t)constrain(cutoff, 20, 255), filterResonance);

  // 5. Screen
  updateDisplay();
}

// =============================================================================
//  Wave shapers: 32-bit phase -> 8-bit sample (-128..127)
// =============================================================================
// Only the top 8 bits of the phase are used: 0..255 = one full cycle.

// Sawtooth: the phase itself is a rising ramp.
static inline int8_t waveSaw(uint32_t phase)   { return (int8_t)(phase >> 24); }

// Pulse/square: high for the first half of the cycle, low for the second.
static inline int8_t wavePulse(uint32_t phase) { return ((phase >> 24) < 128) ? 127 : -128; }

// Triangle: ramp up during the first half, down during the second.
static inline int8_t waveTri(uint32_t phase) {
  uint8_t t = phase >> 24;
  return t < 128 ? (int8_t)(-128 + t * 2) : (int8_t)(127 - (t - 128) * 2);
}

// Sine: lookup table.
static inline int8_t waveSine(uint32_t phase)  { return sineTable[phase >> 24]; }

// Compute one sample for one voice (oscillator a + partner b).
static inline int16_t voiceSample(Phasor<MOZZI_AUDIO_RATE> &a, Phasor<MOZZI_AUDIO_RATE> &b) {
  uint32_t phaseA = a.next();

  switch (waveform) {
    // Difference of two slightly detuned copies of the same wave. As the two
    // drift in and out of phase, the shape of the result keeps changing.
    // For saws, the difference is a pulse wave whose width sweeps slowly —
    // the classic "PWM" sound. The detune amount sets the sweep speed.
    case WAVE_SAW:   return (int16_t)waveSaw(phaseA)   - waveSaw(b.next());
    case WAVE_PULSE: return (int16_t)wavePulse(phaseA) - wavePulse(b.next());
    case WAVE_TRI:   return (int16_t)waveTri(phaseA)   - waveTri(b.next());

    case WAVE_SINE:  return waveSine(phaseA);

    // FM (strictly speaking phase modulation): the modulator's output is
    // added to the carrier's phase, bending the sine wave back and forth.
    // More depth = more harmonics = brighter, more metallic sound.
    case WAVE_FM: {
      int8_t modulator = waveSine(b.next());
      uint32_t modulatedPhase = phaseA + (uint32_t)(((int32_t)modulator * (int)fmDepth) << 16);
      return waveSine(modulatedPhase);
    }

    default:         return (int8_t)fastRandom8();   // WAVE_NOISE
  }
}

// =============================================================================
//  Mozzi callback: audio rate (32768 Hz) — keep this fast!
// =============================================================================

AudioOutput updateAudio() {
  uint8_t gain = ampEnvelope.next();

  // Envelope finished and no key held: output silence and disable the amp.
  if (gain == 0 && activeNote < 0) {
    digitalWrite(AMP_ENABLE_PIN, LOW);
    return MonoOutput::from8Bit(0);
  }

  // 1. Oscillators: one voice, or three averaged voices in chord mode
  int16_t sample = voiceSample(osc1a, osc1b);
  if (activeIsChord) {
    sample += voiceSample(osc2a, osc2b);
    sample += voiceSample(osc3a, osc3b);
    sample /= 3;
  }

  // 2. Optional noise layer for grit / attack transients
  if (noiseMix) {
    int8_t noise = (int8_t)fastRandom8();
    sample += ((int16_t)noise * noiseMix) >> 8;
  }

  // 3. Low-pass filter
  int16_t filtered = lowPass.next(sample);

  // 4. Envelope (gain / 256) and master volume (VOLUME_CURVE / 512)
  int32_t out = (((int32_t)filtered * gain) >> 8) * VOLUME_CURVE[volume] >> 9;
  int8_t outSample = (int8_t)constrain(out, -128, 127);

  // 5. Bit crush (sample-rate reduction): hold each sample for `crush` ticks.
  //    This lowers the effective sample rate and adds a harsh, retro aliasing.
  int crush = crushAmount + joyCrush;
  if (crush > 1) {
    static uint8_t holdCounter = 0;
    static int8_t  heldSample  = 0;
    if (++holdCounter >= crush) {
      holdCounter = 0;
      heldSample  = outSample;
    }
    outSample = heldSample;
  }

  // Scale the 8-bit sample to the R4's 12-bit DAC
  return MonoOutput::from8Bit(outSample);
}

// =============================================================================
//  Main loop
// =============================================================================

void loop() {
  audioHook();        // let Mozzi fill the audio buffer (calls updateControl/updateAudio)

  // Encoders are polled here, as often as possible, rather than only at the
  // 128 Hz control rate — otherwise fast turns would skip steps.
  encoderPoll(encoder1);
  encoderPoll(encoder2);
}
