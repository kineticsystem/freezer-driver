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

#include <cstdint>

#include <freezer_driver/msgs/info_response.hpp>
#include <freezer_driver/msgs/load_sequence_response.hpp>
#include <freezer_driver/msgs/shoot_response.hpp>
#include <freezer_driver/msgs/status_response.hpp>
#include <freezer_driver/sequence.hpp>

namespace freezer_driver
{
/**
 * @brief This class sends the commands and queries of the node to a fake or
 * a real Freezer controller.
 *
 * A call sends one request and returns its one response. A shot runs on the
 * controller by itself: Shoot returns as soon as it has started, and the
 * caller polls get_status() to learn when it ends.
 */
class Driver
{
public:
  virtual ~Driver() = default;

  /**
   * @brief Open the connection and check, with a handshake, that a Freezer
   * controller speaking this protocol is on the other end.
   * @return True when the controller answered and is the right one.
   */
  virtual bool connect() = 0;

  /**
   * @brief Close the connection.
   */
  virtual void disconnect() = 0;

  /**
   * @brief Request the firmware version, the limits of a sequence and the
   * name of the controller.
   */
  virtual InfoResponse get_info() = 0;

  /**
   * @brief Store a sequence in the controller, to be fired by shoot().
   */
  virtual LoadSequenceResponse load_sequence(const Sequence& sequence) = 0;

  /**
   * @brief Fire the loaded sequence.
   * @param checksum The checksum of the sequence the caller means: the
   * controller refuses to fire another one.
   */
  virtual ShootResponse shoot(uint16_t checksum) = 0;

  /**
   * @brief Request the state of the controller and of the last shot.
   */
  virtual StatusResponse get_status() = 0;
};
}  // namespace freezer_driver
