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

#include <freezer_driver/synchronized_driver.hpp>

namespace freezer_driver
{
SynchronizedDriver::SynchronizedDriver(std::unique_ptr<Driver> driver) : driver_{ std::move(driver) }
{
}

bool SynchronizedDriver::connect()
{
  std::lock_guard lock{ mutex_ };
  return driver_->connect();
}

void SynchronizedDriver::disconnect()
{
  std::lock_guard lock{ mutex_ };
  driver_->disconnect();
}

InfoResponse SynchronizedDriver::get_info()
{
  std::lock_guard lock{ mutex_ };
  return driver_->get_info();
}

LoadSequenceResponse SynchronizedDriver::load_sequence(const Sequence& sequence)
{
  std::lock_guard lock{ mutex_ };
  return driver_->load_sequence(sequence);
}

ShootResponse SynchronizedDriver::shoot()
{
  std::lock_guard lock{ mutex_ };
  return driver_->shoot();
}

Response SynchronizedDriver::set_outputs(uint16_t outputs)
{
  std::lock_guard lock{ mutex_ };
  return driver_->set_outputs(outputs);
}

Response SynchronizedDriver::stop()
{
  std::lock_guard lock{ mutex_ };
  return driver_->stop();
}

StatusResponse SynchronizedDriver::get_status()
{
  std::lock_guard lock{ mutex_ };
  return driver_->get_status();
}
}  // namespace freezer_driver
