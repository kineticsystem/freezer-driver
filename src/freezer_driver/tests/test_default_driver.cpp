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

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

#include <mock/mock_framed_serial.hpp>
#include <freezer_driver/default_driver.hpp>

namespace freezer_driver::test
{
using ::testing::_;
using ::testing::Return;
using ::testing::Throw;

/**
 * The answer of the firmware to Info, as read from the Nano: version 1.0.0,
 * 16 steps, 40 µs, 10 s and the name.
 */
std::vector<uint8_t> info_response(const std::string& name = "FREEZER", uint8_t version_major = 1)
{
  std::vector<uint8_t> out{
    0x11,                             // status success
    version_major, 0,    0,           // version
    0x10,                             // max steps
    0x00,          0x00, 0x00, 0x28,  // min hold, 40 µs
    0x00,          0x98, 0x96, 0x80,  // max duration, 10 s
  };
  out.insert(out.end(), name.begin(), name.end());
  return out;
}

struct TestDefaultDriver : public ::testing::Test
{
  void SetUp() override
  {
    auto mock = std::make_unique<MockFramedSerial>();
    serial = mock.get();
    driver = std::make_unique<DefaultDriver>(std::move(mock), std::chrono::seconds{ 0 });
  }

  MockFramedSerial* serial = nullptr;
  std::unique_ptr<DefaultDriver> driver;
};

TEST_F(TestDefaultDriver, get_info)
{
  EXPECT_CALL(*serial, write(std::vector<uint8_t>{ 0x76 }));
  EXPECT_CALL(*serial, read()).WillOnce(Return(info_response()));

  const InfoResponse response = driver->get_info();
  EXPECT_TRUE(response.success());
  EXPECT_EQ(response.version.to_string(), "1.0.0");
  EXPECT_EQ(response.limits.max_steps, 16);
  EXPECT_EQ(response.limits.min_hold_us, 40u);
  EXPECT_EQ(response.limits.max_duration_us, 10'000'000u);
  EXPECT_EQ(response.name, "FREEZER");
}

TEST_F(TestDefaultDriver, connect)
{
  EXPECT_CALL(*serial, open());
  EXPECT_CALL(*serial, write(_));
  EXPECT_CALL(*serial, read()).WillOnce(Return(info_response()));
  EXPECT_TRUE(driver->connect());
}

/** A frame lost while the Nano boots is retried. */
TEST_F(TestDefaultDriver, connect_retries_until_the_controller_answers)
{
  EXPECT_CALL(*serial, open());
  EXPECT_CALL(*serial, write(_)).Times(3);
  EXPECT_CALL(*serial, read())
      .WillOnce(Throw(std::runtime_error("timeout")))
      .WillOnce(Throw(std::runtime_error("timeout")))
      .WillOnce(Return(info_response()));
  EXPECT_TRUE(driver->connect());
}

/** Another device answered: asking again will not change it. */
TEST_F(TestDefaultDriver, connect_refuses_another_device)
{
  EXPECT_CALL(*serial, open());
  EXPECT_CALL(*serial, write(_)).Times(1);
  EXPECT_CALL(*serial, read()).WillOnce(Return(info_response("STEPIT")));
  EXPECT_FALSE(driver->connect());
}

TEST_F(TestDefaultDriver, connect_refuses_another_protocol_version)
{
  EXPECT_CALL(*serial, open());
  EXPECT_CALL(*serial, write(_)).Times(1);
  EXPECT_CALL(*serial, read()).WillOnce(Return(info_response("FREEZER", 2)));
  EXPECT_FALSE(driver->connect());
}

TEST_F(TestDefaultDriver, connect_gives_up)
{
  EXPECT_CALL(*serial, open());
  EXPECT_CALL(*serial, write(_)).Times(5);
  EXPECT_CALL(*serial, read()).Times(5).WillRepeatedly(Throw(std::runtime_error("timeout")));
  EXPECT_FALSE(driver->connect());
}

TEST_F(TestDefaultDriver, load_sequence)
{
  const Sequence sequence{ { Step{ Step::Type::SetAndHold, 0x00FF, 100'000 },
                             Step{ Step::Type::SetAndHold, 0x0000, 200'000 } } };
  const std::vector<uint8_t> expected_request{
    0x7B,                    // command id
    0x02,                    // step count
    0x00,                    // type
    0x00, 0xFF,              // outputs
    0x00, 0x01, 0x86, 0xA0,  // hold, 100 ms
    0x00,                    // type
    0x00, 0x00,              // outputs
    0x00, 0x03, 0x0D, 0x40,  // hold, 200 ms
  };
  EXPECT_CALL(*serial, write(expected_request));
  EXPECT_CALL(*serial, read())
      .WillOnce(Return(std::vector<uint8_t>{
          0x11,                    // status success
          0xAB, 0xCD,              // checksum
          0x00, 0x04, 0x93, 0xE0,  // duration, 300 ms
      }));

  const LoadSequenceResponse response = driver->load_sequence(sequence);
  EXPECT_TRUE(response.success());
  EXPECT_EQ(response.checksum, 0xABCD);
  EXPECT_EQ(response.duration_us, 300'000u);
}

TEST_F(TestDefaultDriver, shoot)
{
  EXPECT_CALL(*serial, write(std::vector<uint8_t>{ 0x7C, 0xAB, 0xCD }));
  EXPECT_CALL(*serial, read())
      .WillOnce(Return(std::vector<uint8_t>{
          0x11,                    // status success
          0x00, 0x07,              // shot id
          0x00, 0x04, 0x93, 0xE0,  // duration, 300 ms
      }));

  const ShootResponse response = driver->shoot(0xABCD);
  EXPECT_TRUE(response.success());
  EXPECT_EQ(response.shot_id, 7);
  EXPECT_EQ(response.duration_us, 300'000u);
}

TEST_F(TestDefaultDriver, shoot_refused)
{
  EXPECT_CALL(*serial, write(_));
  EXPECT_CALL(*serial, read()).WillOnce(Return(std::vector<uint8_t>{ 0x12, 0x05 }));

  const ShootResponse response = driver->shoot(0xABCD);
  EXPECT_FALSE(response.success());
  EXPECT_EQ(response.reason(), Response::Reason::WrongTable);
}

TEST_F(TestDefaultDriver, get_status)
{
  EXPECT_CALL(*serial, write(std::vector<uint8_t>{ 0x75 }));
  EXPECT_CALL(*serial, read())
      .WillOnce(Return(std::vector<uint8_t>{
          0x11,                    // status success
          0x01,                    // state running
          0xAB, 0xCD,              // checksum
          0x00, 0x07,              // last shot id
          0x02,                    // step
          0x00, 0x03, 0x0D, 0x40,  // elapsed, 200 ms
          0x00, 0x00, 0x00, 0x08,  // worst lateness, 8 µs
      }));

  const StatusResponse response = driver->get_status();
  EXPECT_TRUE(response.success());
  EXPECT_EQ(response.state, StatusResponse::State::Running);
  EXPECT_EQ(response.checksum, 0xABCD);
  EXPECT_EQ(response.last_shot_id, 7);
  EXPECT_EQ(response.step, 2);
  EXPECT_EQ(response.elapsed_us, 200'000u);
  EXPECT_EQ(response.worst_lateness_us, 8u);
}

/** Firmware 1.0.0 answers Status with a malformed error: it does not know it yet. */
TEST_F(TestDefaultDriver, error_reason)
{
  EXPECT_CALL(*serial, write(_));
  EXPECT_CALL(*serial, read()).WillOnce(Return(std::vector<uint8_t>{ 0x12, 0x01 }));
  EXPECT_EQ(driver->get_status().reason(), Response::Reason::Malformed);
}

TEST_F(TestDefaultDriver, short_response_throws)
{
  EXPECT_CALL(*serial, write(_));
  EXPECT_CALL(*serial, read()).WillOnce(Return(std::vector<uint8_t>{ 0x11, 0x01 }));
  EXPECT_THROW(driver->get_status(), std::runtime_error);
}

TEST_F(TestDefaultDriver, unknown_status_byte_throws)
{
  EXPECT_CALL(*serial, write(_));
  EXPECT_CALL(*serial, read()).WillOnce(Return(std::vector<uint8_t>{ 0x42 }));
  EXPECT_THROW(driver->get_info(), std::runtime_error);
}
}  // namespace freezer_driver::test
