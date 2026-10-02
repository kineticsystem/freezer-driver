// Copyright 2026 Giovanni Remigi
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the Giovanni Remigi nor the names of its
//      contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "Sequencer.h"

#include <util/atomic.h>

// The shift registers are on port D, written directly: digitalWrite takes
// about 4 µs a call, and the interrupt must shift 16 bits between two steps
// as short as MIN_HOLD_US. The bits are those of the pins in main.cpp.
constexpr uint8_t CLOCK_BIT = _BV(PD3);  // D3, SH_CP of the 74HC595.
constexpr uint8_t DATA_BIT = _BV(PD4);   // D4, DS of the 74HC595.
constexpr uint8_t LATCH_BIT = _BV(PD6);  // D6, ST_CP of the 74HC595.

namespace sequencer
{
namespace
{
// The loaded table. Written only while not running, read by the interrupt.
uint16_t outputs[MAX_STEPS];
uint32_t holdTicks[MAX_STEPS];
byte count = 0;
uint16_t checksum = 0;
uint32_t durationUs = 0;

// The state of the shot, shared with the interrupt.
volatile bool running = false;
volatile byte step = 0;
volatile uint32_t startTicks = 0;
volatile uint32_t targetTicks = 0;  // The end of the current step.
volatile uint32_t worstLatenessTicks = 0;
volatile uint16_t overflows = 0;  // The high word of the time, in ticks.
uint16_t lastShotId = 0;

/**
 * Shift a pattern into the 74HC595, least significant bit first, without
 * latching it: the outputs keep the previous pattern. Bit 0 ends in the last
 * stage of the chain, which drives the shutter of OUT1.
 */
inline void shiftPattern(uint16_t value)
{
  for (byte i = 0; i < 16; i++)
  {
    if (value & 1)
    {
      PORTD |= DATA_BIT;
    }
    else
    {
      PORTD &= ~DATA_BIT;
    }
    PORTD |= CLOCK_BIT;
    PORTD &= ~CLOCK_BIT;
    value >>= 1;
  }
  PORTD &= ~DATA_BIT;
}

/** Copy the shifted pattern to the 16 outputs, all at once. */
inline void latchPattern()
{
  PORTD |= LATCH_BIT;
  PORTD &= ~LATCH_BIT;
}

/**
 * The time in ticks of 0.5 µs, on 32 bits: it wraps after about 35 minutes.
 * Call with interrupts disabled. An overflow that has happened but whose
 * interrupt has not run yet is counted, from its pending flag.
 */
inline uint32_t ticksNow()
{
  const uint16_t low = TCNT1;
  uint16_t high = overflows;
  if ((TIFR1 & _BV(TOV1)) && low < 0x8000)
  {
    high++;
  }
  return (static_cast<uint32_t>(high) << 16) | low;
}

/** Arm the compare-match interrupt for the end of the current step. */
inline void schedule()
{
  // The match fires once per wrap of the counter; runDueBoundaries() ignores
  // the matches that come before the target.
  OCR1A = static_cast<uint16_t>(targetTicks);
}
}  // namespace

/**
 * Handle every boundary that is due: latch the next pattern, or end the shot
 * after the last step. Called by the compare-match interrupt, and by start()
 * after arming it. Call with interrupts disabled.
 *
 * It loops, and checks the time again after arming the next match, because a
 * boundary may be due before the match is armed: a step held for less than
 * the time it takes to shift the next pattern in would otherwise wait for the
 * counter to wrap, 32.8 ms later.
 */
void runDueBoundaries()
{
  while (running)
  {
    const uint32_t now = ticksNow();
    const int32_t lateness = static_cast<int32_t>(now - targetTicks);
    if (lateness < 0)
    {
      return;  // Not due yet, e.g. a match of an earlier wrap of the counter.
    }
    if (static_cast<uint32_t>(lateness) > worstLatenessTicks)
    {
      worstLatenessTicks = lateness;
    }
    if (step + 1 >= count)
    {
      // The end of the last step. Its pattern switched every output off.
      running = false;
      TIMSK1 &= ~_BV(OCIE1A);
      return;
    }
    latchPattern();
    step = step + 1;
    targetTicks = targetTicks + holdTicks[step];
    if (step + 1 < count)
    {
      shiftPattern(outputs[step + 1]);
    }
    schedule();
  }
}

void init()
{
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
  {
    TCCR1A = 0;          // Normal mode, OC1A and OC1B disconnected.
    TCCR1B = _BV(CS11);  // Prescaler 8: 0.5 µs a tick at 16 MHz.
    TCNT1 = 0;
    TIFR1 = _BV(TOV1) | _BV(OCF1A);
    TIMSK1 = _BV(TOIE1);  // The compare match is enabled by start().
  }
}

void load(const Step* steps, byte stepCount, uint16_t tableChecksum, uint32_t tableDurationUs)
{
  for (byte i = 0; i < stepCount; i++)
  {
    outputs[i] = steps[i].outputs;
    holdTicks[i] = steps[i].holdUs * 2;
  }
  count = stepCount;
  checksum = tableChecksum;
  durationUs = tableDurationUs;
}

uint32_t duration()
{
  return durationUs;
}

uint16_t start()
{
  lastShotId = lastShotId == 0xFFFF ? 1 : lastShotId + 1;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
  {
    shiftPattern(outputs[0]);
    const uint32_t now = ticksNow();
    latchPattern();
    startTicks = now;
    step = 0;
    targetTicks = now + holdTicks[0];
    worstLatenessTicks = 0;
    running = true;
    if (count > 1)
    {
      shiftPattern(outputs[1]);
    }
    TIFR1 = _BV(OCF1A);  // Drop a match that happened before this shot.
    schedule();
    TIMSK1 |= _BV(OCIE1A);
    runDueBoundaries();  // The end of step 0 may already be due.
  }
  return lastShotId;
}

Status status()
{
  Status result;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
  {
    result.running = running;
    result.step = running ? step : 0;
    result.elapsedUs = running ? (ticksNow() - startTicks) / 2 : 0;
    result.worstLatenessUs = (worstLatenessTicks + 1) / 2;
  }
  result.loaded = count > 0;
  result.checksum = checksum;
  result.lastShotId = lastShotId;
  return result;
}
}  // namespace sequencer

using namespace sequencer;

ISR(TIMER1_OVF_vect)
{
  overflows++;
}

ISR(TIMER1_COMPA_vect)
{
  runDueBoundaries();
}
