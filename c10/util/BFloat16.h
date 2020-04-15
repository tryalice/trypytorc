#pragma once

// Defines the bloat16 type (brain floating-point). This representation uses
// 1 bit for the sign, 8 bits for the exponent and 7 bits for the mantissa.

#include <c10/macros/Macros.h>
#include <cmath>
#include <cstring>

namespace c10 {

namespace detail {
  inline bool isSmallEndian() {
    int num = 1;
    return *(char *)&num == 1;
  }

  inline float f32_from_bits(uint16_t src) {
    float res = 0;
    uint32_t tmp = src;
    if (isSmallEndian()) {
      tmp <<= 16;
    }

    memcpy(&res, &tmp, sizeof(tmp));
    return res;
  }

  inline uint16_t bits_from_f32(float src) {
    uint32_t res;
    memcpy(&res, &src, sizeof(res));
    if (isSmallEndian()) {
        return res >>= 16;
    } else {
      return res;
    }
  }
} // namespace detail

struct alignas(2) BFloat16 {
  uint16_t val_;

  struct from_bits_t {};
  static constexpr from_bits_t from_bits() {
    return from_bits_t();
  }

  // HIP wants __host__ __device__ tag, CUDA does not
#ifdef __HIP_PLATFORM_HCC__
  C10_HOST_DEVICE BFloat16() = default;
#else
  BFloat16() = default;
#endif

  constexpr C10_HOST_DEVICE BFloat16(uint16_t bits, from_bits_t) : val_(bits){};
  inline C10_HOST_DEVICE BFloat16(float value);
  inline C10_HOST_DEVICE operator float() const;
};

} // namespace c10


#include <c10/util/BFloat16-inl.h>
