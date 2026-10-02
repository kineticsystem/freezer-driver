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

#include "SerialPort.h"
#include "DataBuffer.h"
#include "CrcUtils.h"
#include "Sequencer.h"

// Shift register connections, as routed on the Freezer board. The pin labels
// of motherboard/CircuitSchema.png in the Freezer repository are wrong; these
// are the ones of the PCB.
constexpr byte OVERRIDING_CLEAR_PIN = 2;  // MR of the 74HC595, low active.
constexpr byte CLOCK_PIN = 3;             // SH_CP of the 74HC595.
constexpr byte DATA_PIN = 4;              // DS of the 74HC595.
constexpr byte OUTPUT_ENABLED_PIN = 5;    // OE of the 74HC595, low active.
constexpr byte LATCH_PIN = 6;             // ST_CP of the 74HC595.

constexpr char NAME[] = "FREEZER";

constexpr byte VERSION_MAJOR = 1;
constexpr byte VERSION_MINOR = 1;
constexpr byte VERSION_PATCH = 0;

// Limits of a sequence, reported to the host at the handshake. The host is
// expected to reject a sequence beyond them before sending it.
using sequencer::MAX_DURATION_US;
using sequencer::MAX_STEPS;
using sequencer::MIN_HOLD_US;

constexpr byte STATUS_CMD = 0x75;         // Request the state of the controller and of the last shot.
constexpr byte INFO_CMD = 0x76;           // Request controller info for connection handshaking.
constexpr byte ECHO_CMD = 0x79;           // Return the given bytes, for debugging.
constexpr byte LOAD_SEQUENCE_CMD = 0x7B;  // Store a table of steps, to be fired by SHOOT_CMD.
constexpr byte SHOOT_CMD = 0x7C;          // Fire the loaded table.

constexpr byte STEP_SIZE = 7;  // Type, outputs and hold, in bytes.
constexpr byte SET_AND_HOLD_STEP = 0x00;

constexpr byte SUCCESS_MSG = 0x11;
constexpr byte ERROR_MSG = 0x12;

// Reasons following an error message.
constexpr byte MALFORMED_ERR = 0x01;      // Unknown command id or wrong payload length.
constexpr byte INVALID_TABLE_ERR = 0x02;  // The table breaks a rule of the controller.
constexpr byte BUSY_ERR = 0x03;           // A shot is running.
constexpr byte NO_TABLE_ERR = 0x04;       // Shoot before any table is loaded.
constexpr byte WRONG_TABLE_ERR = 0x05;    // The checksum of Shoot is not the one of the loaded table.

SerialPort serialPort{ 200, 200 };

DataBuffer responseBuffer{ 200 };

/**
 * Switch every optocoupler off: no camera focuses or shoots and no flash
 * fires. The outputs are disabled while the registers are cleared, so that
 * whatever the 74HC595 held at power on never reaches a camera.
 */
void clearOutputs()
{
  digitalWrite(OUTPUT_ENABLED_PIN, HIGH);
  digitalWrite(LATCH_PIN, LOW);
  digitalWrite(OVERRIDING_CLEAR_PIN, LOW);  // Clear the shift stage.
  digitalWrite(LATCH_PIN, HIGH);            // Copy the zeros to the outputs.
  digitalWrite(LATCH_PIN, LOW);
  digitalWrite(OVERRIDING_CLEAR_PIN, HIGH);
  digitalWrite(OUTPUT_ENABLED_PIN, LOW);
}

/** Send an error response with its reason. */
void returnCommandError(byte reason)
{
  responseBuffer.addByte(ERROR_MSG, BufferPosition::Tail);
  responseBuffer.addByte(reason, BufferPosition::Tail);
  serialPort.write(&responseBuffer);
}

/**
 * Send information about the software installed on Arduino to help the client
 * identify the correct port where Arduino is connected, together with the
 * limits of a sequence.
 *
 * The response carries the limits before the name so that the name is the
 * remaining bytes of the packet, as in StepIt:
 *
 *   status           - 1 byte
 *   version          - 3 bytes: major, minor and patch
 *   max steps        - 1 byte
 *   min hold         - 4 bytes, in µs
 *   max duration     - 4 bytes, in µs
 *   name             - the remaining bytes, in ASCII
 */
void returnControllerInfo()
{
  responseBuffer.addByte(SUCCESS_MSG, BufferPosition::Tail);
  responseBuffer.addByte(VERSION_MAJOR, BufferPosition::Tail);
  responseBuffer.addByte(VERSION_MINOR, BufferPosition::Tail);
  responseBuffer.addByte(VERSION_PATCH, BufferPosition::Tail);
  responseBuffer.addByte(MAX_STEPS, BufferPosition::Tail);
  responseBuffer.addLong(MIN_HOLD_US, BufferPosition::Tail);
  responseBuffer.addLong(MAX_DURATION_US, BufferPosition::Tail);
  for (int i = 0; NAME[i] != '\0'; i++)
  {
    responseBuffer.addByte(NAME[i], BufferPosition::Tail);
  }
  serialPort.write(&responseBuffer);
}

/**
 * Echo back the given bytes to test the serial communication.
 * @param cmd The bytes to echo.
 */
void echoCommand(DataBuffer* cmd)
{
  responseBuffer.addByte(SUCCESS_MSG, BufferPosition::Tail);
  while (cmd->getSize() > 0)
  {
    responseBuffer.addByte(cmd->removeByte(BufferPosition::Head), BufferPosition::Tail);
  }
  serialPort.write(&responseBuffer);
}

/**
 * Store a table of steps, to be fired by SHOOT_CMD. The payload is the step
 * count, then for each step its type, its outputs and its hold in µs, most
 * significant byte first. The answer is the checksum of the payload, the
 * CRC-16 the host computes too, and the duration of the table:
 *
 *   status           - 1 byte
 *   checksum         - 2 bytes
 *   duration         - 4 bytes, in µs
 *
 * The table is checked whole before it replaces the loaded one, so that a
 * rejected table leaves the loaded one as it was. The rules are the host's:
 * the last step must switch every output off, so that a camera is never left
 * with its shutter held.
 * @param cmd The command data.
 */
void loadSequenceCommand(DataBuffer* cmd)
{
  if (cmd->getSize() < 1)
  {
    returnCommandError(MALFORMED_ERR);
    return;
  }
  if (sequencer::status().running)
  {
    returnCommandError(BUSY_ERR);
    return;
  }

  const byte count = cmd->removeByte(BufferPosition::Head);
  if (cmd->getSize() != count * STEP_SIZE)
  {
    returnCommandError(MALFORMED_ERR);
    return;
  }
  if (count == 0 || count > MAX_STEPS)
  {
    returnCommandError(INVALID_TABLE_ERR);
    return;
  }

  sequencer::Step steps[MAX_STEPS];
  uint16_t checksum = crc_utils::crc_ccitt_byte(0, count);
  uint32_t duration = 0;
  bool valid = true;
  for (byte i = 0; i < count; i++)
  {
    byte bytes[STEP_SIZE];
    for (byte j = 0; j < STEP_SIZE; j++)
    {
      bytes[j] = cmd->removeByte(BufferPosition::Head);
      checksum = crc_utils::crc_ccitt_byte(checksum, bytes[j]);
    }
    steps[i].outputs = (static_cast<uint16_t>(bytes[1]) << 8) | bytes[2];
    steps[i].holdUs = (static_cast<uint32_t>(bytes[3]) << 24) | (static_cast<uint32_t>(bytes[4]) << 16) |
                      (static_cast<uint32_t>(bytes[5]) << 8) | bytes[6];

    // Compare each hold before adding it, so that the sum cannot overflow.
    if (bytes[0] != SET_AND_HOLD_STEP || steps[i].holdUs < MIN_HOLD_US || steps[i].holdUs > MAX_DURATION_US)
    {
      valid = false;
    }
    else
    {
      duration += steps[i].holdUs;
    }
  }
  if (!valid || duration > MAX_DURATION_US || steps[count - 1].outputs != 0)
  {
    returnCommandError(INVALID_TABLE_ERR);
    return;
  }

  sequencer::load(steps, count, checksum, duration);
  responseBuffer.addByte(SUCCESS_MSG, BufferPosition::Tail);
  responseBuffer.addInt(checksum, BufferPosition::Tail);
  responseBuffer.addLong(duration, BufferPosition::Tail);
  serialPort.write(&responseBuffer);
}

/**
 * Fire the loaded table. The payload is the checksum of the table the host
 * means: the controller refuses to fire another one. The answer is sent once
 * the first pattern is latched:
 *
 *   status           - 1 byte
 *   shot id          - 2 bytes
 *   duration         - 4 bytes, in µs
 *
 * @param cmd The command data.
 */
void shootCommand(DataBuffer* cmd)
{
  if (cmd->getSize() != 2)
  {
    returnCommandError(MALFORMED_ERR);
    return;
  }
  const uint16_t checksum = cmd->removeInt(BufferPosition::Head);
  const sequencer::Status status = sequencer::status();
  if (status.running)
  {
    returnCommandError(BUSY_ERR);
    return;
  }
  if (!status.loaded)
  {
    returnCommandError(NO_TABLE_ERR);
    return;
  }
  if (checksum != status.checksum)
  {
    returnCommandError(WRONG_TABLE_ERR);
    return;
  }

  const uint16_t shotId = sequencer::start();
  responseBuffer.addByte(SUCCESS_MSG, BufferPosition::Tail);
  responseBuffer.addInt(shotId, BufferPosition::Tail);
  responseBuffer.addLong(sequencer::duration(), BufferPosition::Tail);
  serialPort.write(&responseBuffer);
}

/**
 * Send the state of the controller and of the last shot. A shot is over for
 * the host when the controller is idle and the last shot id is the one Shoot
 * returned.
 *
 *   status           - 1 byte
 *   state            - 1 byte: 0 idle, 1 running
 *   checksum         - 2 bytes, of the loaded table, 0 when none is loaded
 *   last shot id     - 2 bytes, 0 when none since power on
 *   step             - 1 byte, the step running
 *   elapsed          - 4 bytes, in µs since the start of the shot
 *   worst lateness   - 4 bytes, in µs, of the last shot
 *
 * @param cmd The command data.
 */
void statusCommand(DataBuffer* cmd)
{
  if (cmd->getSize() != 0)
  {
    returnCommandError(MALFORMED_ERR);
    return;
  }
  const sequencer::Status status = sequencer::status();
  responseBuffer.addByte(SUCCESS_MSG, BufferPosition::Tail);
  responseBuffer.addByte(status.running ? 1 : 0, BufferPosition::Tail);
  responseBuffer.addInt(status.checksum, BufferPosition::Tail);
  responseBuffer.addInt(status.lastShotId, BufferPosition::Tail);
  responseBuffer.addByte(status.step, BufferPosition::Tail);
  responseBuffer.addLong(status.elapsedUs, BufferPosition::Tail);
  responseBuffer.addLong(status.worstLatenessUs, BufferPosition::Tail);
  serialPort.write(&responseBuffer);
}

/**
 * Decode the input command and execute the requested action.
 * @param cmd The the command data.
 */
void processBuffer(DataBuffer* cmd)
{
  byte cmdId = cmd->removeByte(BufferPosition::Head);

  if (cmdId == INFO_CMD && cmd->getSize() == 0)
  {
    returnControllerInfo();
  }
  else if (cmdId == ECHO_CMD)
  {
    echoCommand(cmd);
  }
  else if (cmdId == STATUS_CMD)
  {
    statusCommand(cmd);
  }
  else if (cmdId == LOAD_SEQUENCE_CMD)
  {
    loadSequenceCommand(cmd);
  }
  else if (cmdId == SHOOT_CMD)
  {
    shootCommand(cmd);
  }
  else
  {
    returnCommandError(MALFORMED_ERR);
  }
  cmd->clear();
}

void setup()
{
  pinMode(OUTPUT_ENABLED_PIN, OUTPUT);
  digitalWrite(OUTPUT_ENABLED_PIN, HIGH);
  pinMode(OVERRIDING_CLEAR_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(DATA_PIN, OUTPUT);
  pinMode(LATCH_PIN, OUTPUT);
  digitalWrite(CLOCK_PIN, LOW);
  digitalWrite(DATA_PIN, LOW);
  clearOutputs();
  sequencer::init();

  serialPort.init(9600);
  serialPort.setCallback(&processBuffer);
}

void loop()
{
  serialPort.update();  // Read/write serial port data.
}
