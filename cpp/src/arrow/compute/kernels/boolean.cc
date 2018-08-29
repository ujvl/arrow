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

#include "arrow/compute/kernels/boolean.h"

#include "arrow/compute/context.h"
#include "arrow/compute/kernels/util-internal.h"
#include "arrow/util/logging.h"

#include <vector>

namespace arrow {
namespace compute {

class InvertKernel : public UnaryKernel {
  Status Call(FunctionContext* ctx, const Datum& input, Datum* out) override {
    DCHECK_EQ(Datum::ARRAY, input.kind());

    const ArrayData& in_data = *input.array();
    ArrayData* result;

    out->value = ArrayData::Make(boolean(), in_data.length);
    result = out->array().get();

    // Allocate or copy bitmap
    result->null_count = in_data.null_count;
    std::shared_ptr<Buffer> validity_bitmap = in_data.buffers[0];
    if (in_data.offset != 0) {
      RETURN_NOT_OK(CopyBitmap(ctx->memory_pool(), validity_bitmap->data(),
                               in_data.offset, in_data.length, &validity_bitmap));
    }
    result->buffers.push_back(validity_bitmap);

    // Allocate output data buffer
    std::shared_ptr<Buffer> data_buffer;
    RETURN_NOT_OK(InvertBitmap(ctx->memory_pool(), in_data.buffers[1]->data(),
                               in_data.offset, in_data.length, &data_buffer));
    result->buffers.push_back(data_buffer);

    return Status::OK();
  }
};

Status Invert(FunctionContext* ctx, const Datum& value, Datum* out) {
  InvertKernel kernel;

  std::vector<Datum> result;
  RETURN_NOT_OK(detail::InvokeUnaryArrayKernel(ctx, &kernel, value, &result));

  *out = detail::WrapDatumsLike(value, result);
  return Status::OK();
}

class BinaryBooleanKernel : public BinaryKernel {
  virtual Status Compute(FunctionContext* ctx, const ArrayData& left,
                         const ArrayData& right, ArrayData* out) = 0;

  Status Call(FunctionContext* ctx, const Datum& left, const Datum& right,
              Datum* out) override {
    DCHECK_EQ(Datum::ARRAY, right.kind());
    DCHECK_EQ(Datum::ARRAY, left.kind());

    const ArrayData& left_data = *left.array();
    const ArrayData& right_data = *right.array();
    ArrayData* result;
    out->value = ArrayData::Make(boolean(), right_data.length);

    result = out->array().get();

    // If one of the arrays has a null value, the result will have a null.
    std::shared_ptr<Buffer> validity_bitmap;
    RETURN_NOT_OK(BitmapAnd(ctx->memory_pool(), left_data.buffers[0]->data(),
                            left_data.offset, right_data.buffers[0]->data(),
                            right_data.offset, right_data.length, 0, &validity_bitmap));
    result->buffers.push_back(validity_bitmap);

    result->null_count =
        result->length - CountSetBits(validity_bitmap->data(), 0, result->length);

    return Compute(ctx, left_data, right_data, result);
  }
};

class AndKernel : public BinaryBooleanKernel {
  Status Compute(FunctionContext* ctx, const ArrayData& left, const ArrayData& right,
                 ArrayData* out) override {
    std::shared_ptr<Buffer> data_bitmap;
    RETURN_NOT_OK(BitmapAnd(ctx->memory_pool(), left.buffers[1]->data(), left.offset,
                            right.buffers[1]->data(), right.offset, right.length, 0,
                            &data_bitmap));
    out->buffers.push_back(data_bitmap);
    return Status::OK();
  }
};

Status And(FunctionContext* ctx, const Datum& left, const Datum& right, Datum* out) {
  AndKernel kernel;
  return detail::InvokeBinaryArrayKernel(ctx, &kernel, left, right, out);
}

class OrKernel : public BinaryBooleanKernel {
  Status Compute(FunctionContext* ctx, const ArrayData& left, const ArrayData& right,
                 ArrayData* out) override {
    std::shared_ptr<Buffer> data_bitmap;
    RETURN_NOT_OK(BitmapOr(ctx->memory_pool(), left.buffers[1]->data(), left.offset,
                           right.buffers[1]->data(), right.offset, right.length, 0,
                           &data_bitmap));
    out->buffers.push_back(data_bitmap);
    return Status::OK();
  }
};

Status Or(FunctionContext* ctx, const Datum& left, const Datum& right, Datum* out) {
  OrKernel kernel;
  return detail::InvokeBinaryArrayKernel(ctx, &kernel, left, right, out);
}

class XorKernel : public BinaryBooleanKernel {
  Status Compute(FunctionContext* ctx, const ArrayData& left, const ArrayData& right,
                 ArrayData* out) override {
    std::shared_ptr<Buffer> data_bitmap;
    RETURN_NOT_OK(BitmapXor(ctx->memory_pool(), left.buffers[1]->data(), left.offset,
                            right.buffers[1]->data(), right.offset, right.length, 0,
                            &data_bitmap));
    out->buffers.push_back(data_bitmap);
    return Status::OK();
  }
};

Status Xor(FunctionContext* ctx, const Datum& left, const Datum& right, Datum* out) {
  XorKernel kernel;
  return detail::InvokeBinaryArrayKernel(ctx, &kernel, left, right, out);
}

}  // namespace compute
}  // namespace arrow
