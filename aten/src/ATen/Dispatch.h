#pragma once

#include <ATen/Type.h>
#include <c10/util/Half.h>
#include <c10/util/Exception.h>

#define AT_PRIVATE_CASE_TYPE(enum_type, type, ...) \
  case enum_type: {                                \
    using scalar_t = type;                         \
    return __VA_ARGS__();                          \
  }

namespace detail {

inline at::ScalarType scalar_type(at::ScalarType s) {
  return s;
}

C10_DEPRECATED_MESSAGE("passing at::Type to an AT_DISPATCH macro is deprecated, " \
                       "pass an at::ScalarType instead")
inline at::ScalarType scalar_type(const at::Type &t) {
  return t.scalarType();
}

C10_DEPRECATED_MESSAGE("AT_DISPATCH_ALL_TYPES_AND_HALF is deprecated, " \
                       "use AT_DISPATCH_ALL_TYPES_AND(at::ScalarType::Half, ...) instead")
inline void deprecated_AT_DISPATCH_ALL_TYPES_AND_HALF() {}

C10_DEPRECATED_MESSAGE("AT_DISPATCH_ALL_TYPES_AND_HALF_AND_COMPLEX is deprecated, "            \
                       "use AT_DISPATCH_ALL_TYPES_AND_COMPLEX_AND(at::ScalarType::Half, ...) " \
                       "instead")
inline void deprecated_AT_DISPATCH_ALL_TYPES_AND_HALF_AND_COMPLEX() {}

}

#define AT_DISPATCH_FLOATING_TYPES(TYPE, NAME, ...)                          \
  [&] {                                                                      \
    const auto& the_type = TYPE;                                             \
    at::ScalarType _st = ::detail::scalar_type(TYPE);                        \
    switch (_st) {                                                           \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Double, double, __VA_ARGS__)      \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Float, float, __VA_ARGS__)        \
      default:                                                               \
        AT_ERROR(#NAME, " not implemented for '", toString(_st), "'");       \
    }                                                                        \
  }()

#define AT_DISPATCH_FLOATING_TYPES_AND_HALF(TYPE, NAME, ...)                 \
  [&] {                                                                      \
    const auto& the_type = TYPE;                                             \
    at::ScalarType _st = ::detail::scalar_type(TYPE);                        \
    switch (_st) {                                                           \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Double, double, __VA_ARGS__)      \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Float, float, __VA_ARGS__)        \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Half, at::Half, __VA_ARGS__)      \
      default:                                                               \
        AT_ERROR(#NAME, " not implemented for '", toString(_st), "'");       \
    }                                                                        \
  }()

#define AT_DISPATCH_FLOATING_AND_COMPLEX_TYPES(TYPE, NAME, ...)              \
  [&] {                                                                      \
    const auto& the_type = TYPE;                                             \
    at::ScalarType _st = ::detail::scalar_type(TYPE);                        \
    switch (_st) {                                                           \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Double, double, __VA_ARGS__)      \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Float, float, __VA_ARGS__)        \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Half, at::Half, __VA_ARGS__)      \
      AT_PRIVATE_CASE_TYPE(                                                  \
          at::ScalarType::ComplexDouble, std::complex<double>, __VA_ARGS__)  \
      AT_PRIVATE_CASE_TYPE(                                                  \
          at::ScalarType::ComplexFloat, std::complex<float>, __VA_ARGS__)    \
      AT_PRIVATE_CASE_TYPE(                                                  \
          at::ScalarType::ComplexHalf, std::complex<at::Half>, __VA_ARGS__)  \
      default:                                                               \
        AT_ERROR(#NAME, " not implemented for '", toString(_st), "'");       \
    }                                                                        \
  }()

#define AT_DISPATCH_INTEGRAL_TYPES(TYPE, NAME, ...)                          \
  [&] {                                                                      \
    const auto& the_type = TYPE;                                             \
    at::ScalarType _st = ::detail::scalar_type(TYPE);                        \
    switch (_st) {                                                           \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Byte, uint8_t, __VA_ARGS__)       \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Char, int8_t, __VA_ARGS__)        \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Int, int32_t, __VA_ARGS__)        \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Long, int64_t, __VA_ARGS__)       \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Short, int16_t, __VA_ARGS__)      \
      default:                                                               \
        AT_ERROR(#NAME, " not implemented for '", toString(_st), "'");       \
    }                                                                        \
  }()

#define AT_DISPATCH_ALL_TYPES(TYPE, NAME, ...)                               \
  [&] {                                                                      \
    const auto& the_type = TYPE;                                             \
    at::ScalarType _st = ::detail::scalar_type(TYPE);                        \
    switch (_st) {                                                           \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Byte, uint8_t, __VA_ARGS__)       \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Char, int8_t, __VA_ARGS__)        \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Double, double, __VA_ARGS__)      \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Float, float, __VA_ARGS__)        \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Int, int32_t, __VA_ARGS__)        \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Long, int64_t, __VA_ARGS__)       \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Short, int16_t, __VA_ARGS__)      \
      default:                                                               \
        AT_ERROR(#NAME, " not implemented for '", toString(_st), "'");       \
    }                                                                        \
  }()

#define AT_DISPATCH_ALL_TYPES_AND_HALF(TYPE, NAME, ...)                      \
  [&] {                                                                      \
    detail::deprecated_AT_DISPATCH_ALL_TYPES_AND_HALF();                     \
    const auto& the_type = TYPE;                                             \
    at::ScalarType _st = ::detail::scalar_type(TYPE);                        \
    switch (_st) {                                                           \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Byte, uint8_t, __VA_ARGS__)       \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Char, int8_t, __VA_ARGS__)        \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Double, double, __VA_ARGS__)      \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Float, float, __VA_ARGS__)        \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Int, int32_t, __VA_ARGS__)        \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Long, int64_t, __VA_ARGS__)       \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Short, int16_t, __VA_ARGS__)      \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Half, at::Half, __VA_ARGS__)      \
      default:                                                               \
        AT_ERROR(#NAME, " not implemented for '", toString(_st), "'");       \
    }                                                                        \
  }()

#define AT_DISPATCH_COMPLEX_TYPES(TYPE, NAME, ...)                           \
  [&] {                                                                      \
    const auto& the_type = TYPE;                                             \
    at::ScalarType _st = ::detail::scalar_type(TYPE);                        \
    switch (_st) {                                                           \
      AT_PRIVATE_CASE_TYPE(                                                  \
          at::ScalarType::ComplexFloat, std::complex<float>, __VA_ARGS__)    \
      AT_PRIVATE_CASE_TYPE(                                                  \
          at::ScalarType::ComplexDouble, std::complex<double>, __VA_ARGS__)  \
      default:                                                               \
        AT_ERROR(#NAME, " not implemented for '", toString(_st), "'");       \
    }                                                                        \
  }()

#define AT_DISPATCH_ALL_TYPES_AND_COMPLEX(TYPE, NAME, ...)                   \
  [&] {                                                                      \
    const auto& the_type = TYPE;                                             \
    at::ScalarType _st = ::detail::scalar_type(TYPE);                        \
    switch (_st) {                                                           \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Byte, uint8_t, __VA_ARGS__)       \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Char, int8_t, __VA_ARGS__)        \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Double, double, __VA_ARGS__)      \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Float, float, __VA_ARGS__)        \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Int, int32_t, __VA_ARGS__)        \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Long, int64_t, __VA_ARGS__)       \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Short, int16_t, __VA_ARGS__)      \
      AT_PRIVATE_CASE_TYPE(                                                  \
          at::ScalarType::ComplexFloat, std::complex<float>, __VA_ARGS__)    \
      AT_PRIVATE_CASE_TYPE(                                                  \
          at::ScalarType::ComplexDouble, std::complex<double>, __VA_ARGS__)  \
      default:                                                               \
        AT_ERROR(#NAME, " not implemented for '", toString(_st), "'"); \
    }                                                                        \
  }()

#define AT_DISPATCH_ALL_TYPES_AND_HALF_AND_COMPLEX(TYPE, NAME, ...)          \
  [&] {                                                                      \
    detail::deprecated_AT_DISPATCH_ALL_TYPES_AND_HALF_AND_COMPLEX()          \
    const auto& the_type = TYPE;                                             \
    at::ScalarType _st = ::detail::scalar_type(TYPE);                        \
    switch (_st) {                                                           \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Byte, uint8_t, __VA_ARGS__)       \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Char, int8_t, __VA_ARGS__)        \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Double, double, __VA_ARGS__)      \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Float, float, __VA_ARGS__)        \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Int, int32_t, __VA_ARGS__)        \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Long, int64_t, __VA_ARGS__)       \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Short, int16_t, __VA_ARGS__)      \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Half, at::Half, __VA_ARGS__)      \
      AT_PRIVATE_CASE_TYPE(                                                  \
          at::ScalarType::ComplexFloat, std::complex<float>, __VA_ARGS__)    \
      AT_PRIVATE_CASE_TYPE(                                                  \
          at::ScalarType::ComplexDouble, std::complex<double>, __VA_ARGS__)  \
      default:                                                               \
        AT_ERROR(#NAME, " not implemented for '", toString(_st), "'");       \
    }                                                                        \
  }()


template <at::ScalarType N>
struct MyTemplate;

template<>
struct MyTemplate<at::ScalarType::Half> {
  using type = at::Half;
};

template<>
struct MyTemplate<at::ScalarType::Bool> {
  using type = bool;
};

#define AT_DISPATCH_ALL_TYPES_AND(SCALARTYPE, TYPE, NAME, ...)                    \
  [&] {                                                                           \
    switch (TYPE) {                                                               \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Byte, uint8_t, __VA_ARGS__)            \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Char, int8_t, __VA_ARGS__)             \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Double, double, __VA_ARGS__)           \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Float, float, __VA_ARGS__)             \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Int, int32_t, __VA_ARGS__)             \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Long, int64_t, __VA_ARGS__)            \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Short, int16_t, __VA_ARGS__)           \
      AT_PRIVATE_CASE_TYPE(SCALARTYPE, MyTemplate<SCALARTYPE>::type, __VA_ARGS__) \
      default:                                                                    \
        AT_ERROR(#NAME, " not implemented for '", toString(TYPE), "'");           \
    }                                                                             \
  }()

#define AT_DISPATCH_ALL_TYPES_AND_COMPLEX_AND(SCALARTYPE1, SCALARTYPE2, TYPE, NAME, ...)        \
  [&] {                                                                                         \
    switch (TYPE) {                                                                             \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Byte, uint8_t, __VA_ARGS__)                          \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Char, int8_t, __VA_ARGS__)                           \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Double, double, __VA_ARGS__)                         \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Float, float, __VA_ARGS__)                           \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Int, int32_t, __VA_ARGS__)                           \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Long, int64_t, __VA_ARGS__)                          \
      AT_PRIVATE_CASE_TYPE(at::ScalarType::Short, int16_t, __VA_ARGS__)                         \
      AT_PRIVATE_CASE_TYPE(SCALARTYPE1, MyTemplate<SCALARTYPE1>::type, __VA_ARGS__)             \
      AT_PRIVATE_CASE_TYPE(SCALARTYPE2, MyTemplate<SCALARTYPE2>::type, __VA_ARGS__)             \
      AT_PRIVATE_CASE_TYPE(                                                                     \
          at::ScalarType::ComplexFloat, std::complex<float>, __VA_ARGS__)                       \
      AT_PRIVATE_CASE_TYPE(                                                                     \
          at::ScalarType::ComplexDouble, std::complex<double>, __VA_ARGS__)                     \
      default:                                                                                  \
        AT_ERROR(#NAME, " not implemented for '", TYPE, "'");                                   \
    }                                                                                           \
  }()
