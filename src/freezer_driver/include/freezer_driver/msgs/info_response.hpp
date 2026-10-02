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

#include <string>

#include <freezer_driver/msgs/response.hpp>
#include <freezer_driver/sequence.hpp>

namespace freezer_driver
{
/**
 * The firmware version reported by the controller.
 *
 * The major number is the wire compatibility of the protocol: a driver that
 * does not know a controller's major version cannot trust the bytes it reads
 * from it. The minor and the patch number identify the build.
 */
class Version
{
public:
  explicit Version(uint8_t major = 0, uint8_t minor = 0, uint8_t patch = 0)
    : major_{ major }, minor_{ minor }, patch_{ patch }
  {
  }

  uint8_t major() const
  {
    return major_;
  }

  uint8_t minor() const
  {
    return minor_;
  }

  uint8_t patch() const
  {
    return patch_;
  }

  std::string to_string() const
  {
    return std::to_string(major_) + "." + std::to_string(minor_) + "." + std::to_string(patch_);
  }

private:
  uint8_t major_;
  uint8_t minor_;
  uint8_t patch_;
};

/**
 * The answer to the handshake: the firmware version, the limits of a
 * sequence and the name of the controller.
 */
struct InfoResponse : public Response
{
  using Response::Response;

  Version version;
  SequenceLimits limits;
  std::string name;
};
}  // namespace freezer_driver
