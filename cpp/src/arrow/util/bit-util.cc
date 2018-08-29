// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

// Alias MSVC popcount to GCC name
#ifdef _MSC_VER
#include <intrin.h>
#define __builtin_popcount __popcnt
#include <nmmintrin.h>
#define __builtin_popcountll _mm_popcnt_u64
#endif

#include <algorithm>
#include <cstring>
#include <functional>
#include <vector>

#include "arrow/buffer.h"
#include "arrow/memory_pool.h"
#include "arrow/status.h"
#include "arrow/util/bit-util.h"
#include "arrow/util/logging.h"

namespace arrow {

namespace BitUtil {

namespace {

void FillBitsFromBytes(const std::vector<uint8_t>& bytes, uint8_t* bits) {
  for (size_t i = 0; i < bytes.size(); ++i) {
    if (bytes[i] > 0) {
      SetBit(bits, i);
    }
  }
}

}  // namespace

Status BytesToBits(const std::vector<uint8_t>& bytes, MemoryPool* pool,
                   std::shared_ptr<Buffer>* out) {
  int64_t bit_length = BytesForBits(bytes.size());

  std::shared_ptr<Buffer> buffer;
  RETURN_NOT_OK(AllocateBuffer(pool, bit_length, &buffer));
  uint8_t* out_buf = buffer->mutable_data();
  memset(out_buf, 0, static_cast<size_t>(buffer->capacity()));
  FillBitsFromBytes(bytes, out_buf);

  *out = buffer;
  return Status::OK();
}

}  // namespace BitUtil

int64_t CountSetBits(const uint8_t* data, int64_t bit_offset, int64_t length) {
  constexpr int64_t pop_len = sizeof(uint64_t) * 8;

  int64_t count = 0;

  // The first bit offset where we can use a 64-bit wide hardware popcount
  const int64_t fast_count_start = BitUtil::RoundUp(bit_offset, pop_len);

  // The number of bits until fast_count_start
  const int64_t initial_bits = std::min(length, fast_count_start - bit_offset);
  for (int64_t i = bit_offset; i < bit_offset + initial_bits; ++i) {
    if (BitUtil::GetBit(data, i)) {
      ++count;
    }
  }

  const int64_t fast_counts = (length - initial_bits) / pop_len;

  // Advance until the first aligned 8-byte word after the initial bits
  const uint64_t* u64_data =
      reinterpret_cast<const uint64_t*>(data) + fast_count_start / pop_len;

  const uint64_t* end = u64_data + fast_counts;

  // popcount as much as possible with the widest possible count
  for (auto iter = u64_data; iter < end; ++iter) {
    count += __builtin_popcountll(*iter);
  }

  // Account for left over bit (in theory we could fall back to smaller
  // versions of popcount but the code complexity is likely not worth it)
  const int64_t tail_index = bit_offset + initial_bits + fast_counts * pop_len;
  for (int64_t i = tail_index; i < bit_offset + length; ++i) {
    if (BitUtil::GetBit(data, i)) {
      ++count;
    }
  }

  return count;
}

template <bool invert_bits>
Status TransferBitmap(MemoryPool* pool, const uint8_t* data, int64_t offset,
                      int64_t length, std::shared_ptr<Buffer>* out) {
  std::shared_ptr<Buffer> buffer;
  RETURN_NOT_OK(AllocateEmptyBitmap(pool, length, &buffer));
  uint8_t* dest = buffer->mutable_data();

  int64_t byte_offset = offset / 8;
  int64_t bit_offset = offset % 8;
  int64_t num_bytes = BitUtil::BytesForBits(length);
  int64_t bits_to_zero = num_bytes * 8 - length;

  if (bit_offset > 0) {
    uint32_t carry_mask = BitUtil::kBitmask[bit_offset] - 1U;
    uint32_t carry_shift = 8U - static_cast<uint32_t>(bit_offset);

    uint32_t carry = 0U;
    if (BitUtil::BytesForBits(length + bit_offset) > num_bytes) {
      carry = (data[byte_offset + num_bytes] & carry_mask) << carry_shift;
    }

    int64_t i = num_bytes - 1;
    while (i + 1 > 0) {
      uint8_t cur_byte = data[byte_offset + i];
      if (invert_bits) {
        dest[i] = static_cast<uint8_t>(~((cur_byte >> bit_offset) | carry));
      } else {
        dest[i] = static_cast<uint8_t>((cur_byte >> bit_offset) | carry);
      }
      carry = (cur_byte & carry_mask) << carry_shift;
      --i;
    }
  } else {
    if (invert_bits) {
      for (int64_t i = 0; i < num_bytes; i++) {
        dest[i] = static_cast<uint8_t>(~(data[byte_offset + i]));
      }
    } else {
      std::memcpy(dest, data + byte_offset, static_cast<size_t>(num_bytes));
    }
  }

  for (int64_t i = length; i < length + bits_to_zero; ++i) {
    // Both branches may copy extra bits - unsetting to match specification.
    BitUtil::ClearBit(dest, i);
  }

  *out = buffer;
  return Status::OK();
}

Status CopyBitmap(MemoryPool* pool, const uint8_t* data, int64_t offset, int64_t length,
                  std::shared_ptr<Buffer>* out) {
  return TransferBitmap<false>(pool, data, offset, length, out);
}

Status InvertBitmap(MemoryPool* pool, const uint8_t* data, int64_t offset, int64_t length,
                    std::shared_ptr<Buffer>* out) {
  return TransferBitmap<true>(pool, data, offset, length, out);
}

bool BitmapEquals(const uint8_t* left, int64_t left_offset, const uint8_t* right,
                  int64_t right_offset, int64_t bit_length) {
  if (left_offset % 8 == 0 && right_offset % 8 == 0) {
    // byte aligned, can use memcmp
    bool bytes_equal = std::memcmp(left + left_offset / 8, right + right_offset / 8,
                                   bit_length / 8) == 0;
    if (!bytes_equal) {
      return false;
    }
    for (int64_t i = (bit_length / 8) * 8; i < bit_length; ++i) {
      if (BitUtil::GetBit(left, left_offset + i) !=
          BitUtil::GetBit(right, right_offset + i)) {
        return false;
      }
    }
    return true;
  }

  // Unaligned slow case
  for (int64_t i = 0; i < bit_length; ++i) {
    if (BitUtil::GetBit(left, left_offset + i) !=
        BitUtil::GetBit(right, right_offset + i)) {
      return false;
    }
  }
  return true;
}

namespace {

template <typename Op>
void AlignedBitmapOp(const uint8_t* left, int64_t left_offset, const uint8_t* right,
                     int64_t right_offset, uint8_t* out, int64_t out_offset,
                     int64_t length) {
  Op op;
  DCHECK_EQ(left_offset % 8, right_offset % 8);
  DCHECK_EQ(left_offset % 8, out_offset % 8);

  const int64_t nbytes = BitUtil::BytesForBits(length + left_offset);
  left += left_offset / 8;
  right += right_offset / 8;
  out += out_offset / 8;
  for (int64_t i = 0; i < nbytes; ++i) {
    out[i] = op(left[i], right[i]);
  }
}

template <typename Op>
void UnalignedBitmapOp(const uint8_t* left, int64_t left_offset, const uint8_t* right,
                       int64_t right_offset, uint8_t* out, int64_t out_offset,
                       int64_t length) {
  Op op;
  auto left_reader = internal::BitmapReader(left, left_offset, length);
  auto right_reader = internal::BitmapReader(right, right_offset, length);
  auto writer = internal::BitmapWriter(out, out_offset, length);
  for (int64_t i = 0; i < length; ++i) {
    if (op(left_reader.IsSet(), right_reader.IsSet())) {
      writer.Set();
    }
    left_reader.Next();
    right_reader.Next();
    writer.Next();
  }
  writer.Finish();
}

template <typename BitOp, typename LogicalOp>
Status BitmapOp(MemoryPool* pool, const uint8_t* left, int64_t left_offset,
                const uint8_t* right, int64_t right_offset, int64_t length,
                int64_t out_offset, std::shared_ptr<Buffer>* out_buffer) {
  if ((out_offset % 8 == left_offset % 8) && (out_offset % 8 == right_offset % 8)) {
    // Fast case: can use bytewise AND
    const int64_t phys_bits = length + out_offset;
    RETURN_NOT_OK(AllocateEmptyBitmap(pool, phys_bits, out_buffer));
    AlignedBitmapOp<BitOp>(left, left_offset, right, right_offset,
                           (*out_buffer)->mutable_data(), out_offset, length);
  } else {
    // Unaligned
    RETURN_NOT_OK(AllocateEmptyBitmap(pool, length + out_offset, out_buffer));
    UnalignedBitmapOp<LogicalOp>(left, left_offset, right, right_offset,
                                 (*out_buffer)->mutable_data(), out_offset, length);
  }
  return Status::OK();
}

}  // namespace

Status BitmapAnd(MemoryPool* pool, const uint8_t* left, int64_t left_offset,
                 const uint8_t* right, int64_t right_offset, int64_t length,
                 int64_t out_offset, std::shared_ptr<Buffer>* out_buffer) {
  return BitmapOp<std::bit_and<uint8_t>, std::logical_and<bool>>(
      pool, left, left_offset, right, right_offset, length, out_offset, out_buffer);
}

Status BitmapOr(MemoryPool* pool, const uint8_t* left, int64_t left_offset,
                const uint8_t* right, int64_t right_offset, int64_t length,
                int64_t out_offset, std::shared_ptr<Buffer>* out_buffer) {
  return BitmapOp<std::bit_or<uint8_t>, std::logical_or<bool>>(
      pool, left, left_offset, right, right_offset, length, out_offset, out_buffer);
}

Status BitmapXor(MemoryPool* pool, const uint8_t* left, int64_t left_offset,
                 const uint8_t* right, int64_t right_offset, int64_t length,
                 int64_t out_offset, std::shared_ptr<Buffer>* out_buffer) {
  return BitmapOp<std::bit_xor<uint8_t>, std::bit_xor<bool>>(
      pool, left, left_offset, right, right_offset, length, out_offset, out_buffer);
}

}  // namespace arrow
