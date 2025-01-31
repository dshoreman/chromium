// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_FIDO_U2F_APDU_RESPONSE_H_
#define DEVICE_FIDO_U2F_APDU_RESPONSE_H_

#include <memory>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/memory/ref_counted.h"

namespace device {

// APDU responses are defined as part of ISO 7816-4. Serialized responses
// consist of a data field of varying length, up to a maximum 65536, and a
// two byte status field.
class U2fApduResponse {
 public:
  // Status bytes are specified in ISO 7816-4
  enum class Status : uint16_t {
    SW_NO_ERROR = 0x9000,
    SW_CONDITIONS_NOT_SATISFIED = 0x6985,
    SW_WRONG_DATA = 0x6A80,
    SW_WRONG_LENGTH = 0x6700,
  };

  U2fApduResponse(std::vector<uint8_t> message, Status response_status);
  ~U2fApduResponse();

  // Create a APDU response from the serialized message
  static std::unique_ptr<U2fApduResponse> CreateFromMessage(
      const std::vector<uint8_t>& data);
  std::vector<uint8_t> GetEncodedResponse() const;
  const std::vector<uint8_t> data() const { return data_; };
  Status status() const { return response_status_; };

 private:
  FRIEND_TEST_ALL_PREFIXES(U2fApduTest, TestDeserializeResponse);

  Status response_status_;
  std::vector<uint8_t> data_;
};

}  // namespace device

#endif  // DEVICE_FIDO_U2F_APDU_RESPONSE_H_
