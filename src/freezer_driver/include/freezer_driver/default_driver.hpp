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

#pragma once

#include <chrono>
#include <memory>

#include <framed_serial/framed_serial.hpp>
#include <freezer_driver/driver.hpp>

namespace freezer_driver
{
/**
 * @brief The driver of a real Freezer controller, an Arduino Nano on a
 * serial port.
 */
class DefaultDriver : public Driver
{
public:
  /**
   * @param framed_serial The framed serial connection to the controller.
   * @param connect_delay How long to wait after opening the port before the
   * handshake: the Nano resets when the port opens, and its bootloader keeps
   * the firmware from answering for a while.
   */
  explicit DefaultDriver(std::unique_ptr<framed_serial::FramedSerial> framed_serial,
                         std::chrono::duration<double> connect_delay = std::chrono::seconds{ 1 });

  bool connect() override;
  void disconnect() override;
  InfoResponse get_info() override;
  LoadSequenceResponse load_sequence(const Sequence& sequence) override;
  ShootResponse shoot() override;
  Response set_outputs(uint16_t outputs) override;
  Response stop() override;
  StatusResponse get_status() override;

private:
  std::unique_ptr<framed_serial::FramedSerial> framed_serial_;
  std::chrono::duration<double> connect_delay_;
};
}  // namespace freezer_driver
