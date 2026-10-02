#ifndef SEQUENCER_H
#define SEQUENCER_H

#include <Arduino.h>

// The sequencer runs a shot: a table of steps, each a pattern of the 16
// outputs and how long to hold it.
//
// Timer1 keeps the time, at 0.5 µs a tick, and its compare-match interrupt
// latches each pattern at its boundary. The pattern of the next step is
// shifted into the 74HC595 during the current step, so that the interrupt only
// pulses ST_CP and the 16 outputs change together at the boundary. Each
// boundary is an absolute time, the start of the shot plus the holds before
// it, so that the lateness of one step does not move the next one.
//
// loop() stays free during a shot: it is only delayed by the interrupt, and
// can answer the serial port.

namespace sequencer
{
constexpr byte MAX_STEPS = 16;
// Handling a boundary, the latch and the shift of the next pattern, takes
// about 27.5 µs: shorter steps fall behind, one after the other. 40 µs leaves
// room for the serial interrupts of a status poll.
constexpr unsigned long MIN_HOLD_US = 40;
constexpr unsigned long MAX_DURATION_US = 10000000;

struct Step
{
  uint16_t outputs;
  uint32_t holdUs;
};

struct Status
{
  bool running;
  bool loaded;
  uint16_t checksum;         // Of the loaded table, 0 when none is loaded.
  uint16_t lastShotId;       // The shot running, or the last one; 0 when none since power on.
  byte step;                 // The step running, when running.
  uint32_t elapsedUs;        // Time since the start of the shot, when running.
  uint32_t worstLatenessUs;  // The latest a boundary of the last shot came after its time.
};

// Configure Timer1. Call once, from setup(), after the outputs are cleared.
void init();

// Store a table, which must be valid and is copied. Only while not running.
void load(const Step* steps, byte count, uint16_t checksum, uint32_t durationUs);

// Fire the loaded table: latch its first pattern now and return the shot id.
// Only while not running and with a table loaded.
uint16_t start();

// The duration of the loaded table, in µs.
uint32_t duration();

// A consistent copy of the state, read with interrupts disabled for a few µs.
Status status();

// Latch the patterns whose time has come. Used by the Timer1 interrupt.
void runDueBoundaries();
}  // namespace sequencer

#endif  // SEQUENCER_H
