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
constexpr byte VERSION_MINOR = 0;
constexpr byte VERSION_PATCH = 0;

// Limits of a sequence, reported to the host at the handshake. The host is
// expected to reject a sequence beyond them before sending it.
constexpr byte MAX_STEPS = 16;
constexpr unsigned long MIN_HOLD_US = 20;
constexpr unsigned long MAX_DURATION_US = 10000000;

constexpr byte INFO_CMD = 0x76;  // Request controller info for connection handshaking.
constexpr byte ECHO_CMD = 0x79;  // Return the given bytes, for debugging.

constexpr byte SUCCESS_MSG = 0x11;
constexpr byte ERROR_MSG = 0x12;

// Reasons following an error message.
constexpr byte MALFORMED_ERR = 0x01;  // Unknown command id or wrong payload length.

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

  serialPort.init(9600);
  serialPort.setCallback(&processBuffer);
}

void loop()
{
  serialPort.update();  // Read/write serial port data.
}
