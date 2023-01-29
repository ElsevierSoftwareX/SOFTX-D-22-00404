// Copyright (C) 2022 Maximilian Reininghaus
// Released under MIT License
// license available in LICENSE file, or at
// http://www.opensource.org/licenses/mit-license.php

#pragma once

#include <iterator>
#include <type_traits>

#include <boost/iterator/iterator_facade.hpp>
#include <boost/stl_interfaces/iterator_interface.hpp>
#include <boost/stl_interfaces/view_interface.hpp>

namespace cnpypp {
template <typename TValueType>
class stride_iterator : public boost::stl_interfaces::iterator_interface<
                            stride_iterator<TValueType>,
                            std::random_access_iterator_tag, TValueType> {
public:
  using value_type = TValueType;

  stride_iterator(std::byte* ptr, std::ptrdiff_t stride)
      : ptr_{ptr}, stride_{stride} {}
  stride_iterator() : ptr_{nullptr}, stride_{} {}

  using reference_type = std::add_lvalue_reference_t<TValueType>;

  stride_iterator& operator+=(size_t n) {
    ptr_ += n * stride_;
    return *this;
  }

  reference_type operator*() const {
    return *reinterpret_cast<value_type*>(ptr_);
  }

  bool operator==(stride_iterator const& other) const {
    return ptr_ == other.ptr_;
  }

private:
  std::byte* ptr_;
  std::ptrdiff_t const stride_;
};

template <typename Iterator, typename Sentinel = Iterator>
struct subrange
    : boost::stl_interfaces::view_interface<subrange<Iterator, Sentinel>> {
  subrange() = default;
  constexpr subrange(Iterator it, Sentinel s) : first_{it}, last_{s} {}

  constexpr auto begin() const { return first_; }
  constexpr auto end() const { return last_; }

private:
  Iterator first_;
  Sentinel last_;
};
} // namespace cnpypp
