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
#include <string>

namespace freezer_driver
{
/**
 * The first byte of every answer of the controller and, for an error, the
 * byte that follows it: why the command was refused.
 */
class Response
{
public:
  enum class Status : uint8_t
  {
    Success = 0x11,
    Failure = 0x12,
  };

  enum class Reason : uint8_t
  {
    None = 0x00,
    Malformed = 0x01,     // Unknown command id or wrong payload length.
    InvalidTable = 0x02,  // The sequence breaks a rule of the controller.
    Busy = 0x03,          // A shot is running.
    NoTable = 0x04,       // Shoot before any sequence is loaded.
  };

  explicit Response(Status status = Status::Success, Reason reason = Reason::None)
    : status_{ status }, reason_{ reason }
  {
  }
  virtual ~Response() = default;

  Status status() const
  {
    return status_;
  }

  Reason reason() const
  {
    return reason_;
  }

  bool success() const
  {
    return status_ == Status::Success;
  }

private:
  Status status_;
  Reason reason_;
};

/**
 * A short description of an error reason, for logs and action results.
 */
inline std::string to_string(Response::Reason reason)
{
  switch (reason)
  {
    case Response::Reason::None:
      return "no error";
    case Response::Reason::Malformed:
      return "malformed command";
    case Response::Reason::InvalidTable:
      return "invalid sequence";
    case Response::Reason::Busy:
      return "a shot is running";
    case Response::Reason::NoTable:
      return "no sequence loaded";
  }
  return "unknown error " + std::to_string(static_cast<int>(reason));
}
}  // namespace freezer_driver
