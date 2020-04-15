#pragma once

#include <ATen/core/Tensor.h>
#include <c10/util/Array.h>
#include <ATen/core/blob.h>
#include <ATen/core/dispatch/OpSchema.h>

namespace c10 {
namespace core {
namespace opschema {

// TODO This op schema should probably not live in c10 since it's not a method
// on Tensor. It's only here as a proof-of-concept op and for LATTE team
// to be able to call caffe2 layer norm from PyTorch.
struct LayerNorm final {
  static constexpr const char* name = "LayerNorm";

  using Signature = std::tuple<at::Tensor, int, float, at::Tensor, at::Tensor, at::Tensor> (
      const at::Tensor& input,
      int axis,
      float epsilon,
      const at::Tensor& output,
      const at::Tensor& output_mean,
      const at::Tensor& output_stdev);

  static constexpr size_t num_dispatch_args() {return 1;}

  static constexpr size_t num_outputs() {return 3;}

  static constexpr c10::guts::array<const char*, 6> parameter_names = {
      {"input", "axis", "epsilon", "output", "output_mean", "output_stdev"}};
};

} // namespace opschema
} // namespace core
} // namespace c10
