// Copyright (C) 2022 Maximilian Reininghaus
// Released under MIT License
// license available in LICENSE file, or at
// http://www.opensource.org/licenses/mit-license.php

#pragma once

#include <complex>
#include <type_traits>

namespace cnpypp {

template <typename F> char constexpr map_type(std::complex<F>) { return 'c'; }

template <typename T> char constexpr map_type(T) {
  static_assert(std::is_arithmetic_v<T>, "only arithmetic types supported");

  if constexpr (std::is_same_v<T, bool>) {
    return 'b';
  }

  if constexpr (std::is_integral_v<T>) {
    return std::is_signed_v<T> ? 'i' : 'u';
  }

  if constexpr (std::is_floating_point_v<T>) {
    return 'f';
  }
}

} // namespace cnpypp
