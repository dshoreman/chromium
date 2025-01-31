// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines the raster command buffer commands.

#ifndef GPU_COMMAND_BUFFER_COMMON_RASTER_CMD_FORMAT_H_
#define GPU_COMMAND_BUFFER_COMMON_RASTER_CMD_FORMAT_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "base/atomicops.h"
#include "base/logging.h"
#include "base/macros.h"
#include "components/viz/common/resources/resource_format.h"
#include "gpu/command_buffer/common/bitfield_helpers.h"
#include "gpu/command_buffer/common/cmd_buffer_common.h"
#include "gpu/command_buffer/common/constants.h"
#include "gpu/command_buffer/common/gl2_types.h"
#include "gpu/command_buffer/common/gles2_cmd_utils.h"
#include "gpu/command_buffer/common/raster_cmd_ids.h"
#include "ui/gfx/buffer_types.h"

namespace gpu {
namespace raster {

// Command buffer is GPU_COMMAND_BUFFER_ENTRY_ALIGNMENT byte aligned.
#pragma pack(push, 4)
static_assert(GPU_COMMAND_BUFFER_ENTRY_ALIGNMENT == 4,
              "pragma pack alignment must be equal to "
              "GPU_COMMAND_BUFFER_ENTRY_ALIGNMENT");

namespace id_namespaces {

enum class IdNamespaces { kQueries, kTextures };

}  // namespace id_namespaces

// Used for some glGetXXX commands that return a result through a pointer. We
// need to know if the command succeeded or not and the size of the result. If
// the command failed its result size will 0.
template <typename T>
struct SizedResult {
  typedef T Type;

  T* GetData() { return static_cast<T*>(static_cast<void*>(&data)); }

  // Returns the total size in bytes of the SizedResult for a given number of
  // results including the size field.
  static size_t ComputeSize(size_t num_results) {
    return sizeof(T) * num_results + sizeof(uint32_t);  // NOLINT
  }

  // Returns the maximum number of results for a given buffer size.
  static uint32_t ComputeMaxResults(size_t size_of_buffer) {
    return (size_of_buffer >= sizeof(uint32_t))
               ? ((size_of_buffer - sizeof(uint32_t)) / sizeof(T))
               : 0;  // NOLINT
  }

  // Set the size for a given number of results.
  void SetNumResults(size_t num_results) {
    size = sizeof(T) * num_results;  // NOLINT
  }

  // Get the number of elements in the result
  int32_t GetNumResults() const {
    return size / sizeof(T);  // NOLINT
  }

  // Copy the result.
  void CopyResult(void* dst) const { memcpy(dst, &data, size); }

  uint32_t size;  // in bytes.
  int32_t data;   // this is just here to get an offset.
};

static_assert(sizeof(SizedResult<int8_t>) == 8,
              "size of SizedResult<int8_t> should be 8");
static_assert(offsetof(SizedResult<int8_t>, size) == 0,
              "offset of SizedResult<int8_t>.size should be 0");
static_assert(offsetof(SizedResult<int8_t>, data) == 4,
              "offset of SizedResult<int8_t>.data should be 4");

namespace cmds {

#include "gpu/command_buffer/common/raster_cmd_format_autogen.h"

#pragma pack(pop)

}  // namespace cmd
}  // namespace raster
}  // namespace gpu

#endif  // GPU_COMMAND_BUFFER_COMMON_RASTER_CMD_FORMAT_H_
