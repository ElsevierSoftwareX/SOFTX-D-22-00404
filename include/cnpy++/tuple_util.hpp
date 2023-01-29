// Copyright (C) 2022 Maximilian Reininghaus
// Released under MIT License
// license available in LICENSE file, or at
// http://www.opensource.org/licenses/mit-license.php

#pragma once

#include <array>
#include <cstddef>
#include <iterator>
#include <tuple>
#include <type_traits>
#include <utility>

#include <boost/iterator/iterator_facade.hpp>

#include <cnpy++/map_type.hpp>

namespace cnpypp {

namespace detail {
template <template <typename...> class Template, typename T>
struct is_specialization_of : std::false_type {};

template <template <typename...> class Template, typename... Args>
struct is_specialization_of<Template, Template<Args...>> : std::true_type {};

template <template <typename...> class Template, typename... Args>
bool constexpr is_specialization_of_v =
    is_specialization_of<Template, Args...>::value;

template <class T> struct is_std_array : std::false_type {};

template <class T, std::size_t N>
struct is_std_array<std::array<T, N>> : std::true_type {};

template <typename T> bool constexpr is_std_array_v = is_std_array<T>::value;

template <typename T>
bool constexpr is_tuple_like_v =
    is_std_array_v<T> || is_specialization_of_v<std::pair, T> ||
    is_specialization_of_v<std::tuple, T>;

template <typename T> struct is_bool : public std::is_same<T, bool> {};

template <typename T> bool constexpr is_bool_v = is_bool<T>::value;

template <typename T, size_t N, size_t i> bool constexpr has_bool_impl() {
  if constexpr (i < N)
    return is_bool_v<std::decay_t<typename std::tuple_element<i, T>::type>> ||
           has_bool_impl<T, N, i + 1>();
  else
    return false;
}

template <typename T> bool constexpr has_bool() {
  return has_bool_impl<T, std::tuple_size<T>::value, 0>();
}

} // namespace detail

template <typename Tup> struct tuple_info {
  static_assert(detail::is_specialization_of_v<std::tuple, Tup> ||
                    detail::is_specialization_of_v<std::pair, Tup> ||
                    detail::is_std_array_v<Tup>,
                "must provide std::tuple-like type");

  static auto constexpr size = std::tuple_size_v<Tup>;
  static bool constexpr has_bool_element = detail::has_bool<Tup>();

  // prevent any instantiation
  tuple_info() = delete;
  tuple_info(tuple_info<Tup> const&) = delete;
  tuple_info& operator=(tuple_info const&) = delete;

private:
  static std::array<char, size> constexpr getDataTypes() {
    std::array<char, size> types{};
    getDataTypes_impl<0>(types);
    return types;
  }

  static std::array<size_t, size> constexpr getElementSizes() {
    std::array<size_t, size> sizes{};
    getSizes_impl<0>(sizes);
    return sizes;
  }

public:
  static std::array<char, size> constexpr data_types = getDataTypes();
  static std::array<size_t, size> constexpr element_sizes = getElementSizes();

private:
  static size_t constexpr sum_size_impl() {
    size_t sum{};

    for (auto const& v : element_sizes) {
      sum += v;
    }

    return sum;
  }

public:
  static size_t constexpr sum_sizes = sum_size_impl();

private:
  static std::array<size_t, size> constexpr calc_offsets() {
    std::array<size_t, size> offsets{};
    offsets[0] = 0;
    calc_offsets_impl<1>(offsets);
    return offsets;
  }

public:
  static std::array<size_t, size> constexpr offsets = calc_offsets();

private:
  template <int k>
  static void constexpr getDataTypes_impl(std::array<char, size>& sizes) {
    if constexpr (k < size) {
      sizes[k] = map_type(std::tuple_element_t<k, Tup>{});
      getDataTypes_impl<k + 1>(sizes);
    }
  }

  template <int k>
  static void constexpr getSizes_impl(std::array<size_t, size>& sizes) {
    if constexpr (k < size) {
      sizes[k] = sizeof(std::tuple_element_t<k, Tup>);
      getSizes_impl<k + 1>(sizes);
    }
  }

  template <int k>
  static void constexpr calc_offsets_impl(std::array<size_t, size>& offsets) {
    if constexpr (k < size) {
      offsets[k] = offsets[k - 1] + element_sizes[k - 1];
      calc_offsets_impl<k + 1>(offsets);
    }
  }
};

template <typename T> struct add_const {};

template <typename... Types> struct add_const<std::tuple<Types...>> {
  using type = std::tuple<typename std::add_const<Types>::type...>;
};

template <typename Tup> using add_const_t = typename add_const<Tup>::type;

template <typename T> struct add_ptr {};

template <typename... Types> struct add_ptr<std::tuple<Types...>> {
  using type = std::tuple<typename std::add_pointer<Types>::type...>;
};

template <typename Tup> using add_ptr_t = typename add_ptr<Tup>::type;

template <typename T> struct add_ref {};

template <typename... Types> struct add_ref<std::tuple<Types...>> {
  using type = std::tuple<typename std::add_lvalue_reference<Types>::type...>;
};

template <typename Tup> using add_ref_t = typename add_ref<Tup>::type;

namespace detail {
template <typename T> auto dereference_impl(T* ptr) {
  return std::tuple<std::add_lvalue_reference_t<T>>{*ptr};
}

template <typename T, typename... Tother>
auto dereference_impl(T* ptr, Tother... others) {
  return std::tuple_cat(std::tuple<std::add_lvalue_reference_t<T>>(*ptr),
                        dereference_impl(others...));
}

} // namespace detail

template <typename Tup>
class tuple_iterator
    : public boost::iterator_facade<tuple_iterator<Tup>, Tup,
                                    std::random_access_iterator_tag,
                                    add_ref_t<Tup>, std::ptrdiff_t> {
public:
  using ref_tuple_t = add_ref_t<Tup>;
  using pointer_tuple_t = add_ptr_t<Tup>;
  using const_ref_tuple_t = add_ref_t<add_const_t<Tup>>;
  using const_pointer_tuple_t = add_ptr_t<add_const_t<Tup>>;

  tuple_iterator(std::byte* ptr) : ptr_{ptr} {}

private:
  friend class boost::iterator_core_access;

  void increment() { ptr_ += tuple_info<Tup>::sum_sizes; }

  void decrement() { ptr_ -= tuple_info<Tup>::sum_sizes; }

  void advance(size_t n) { ptr_ += n * tuple_info<Tup>::sum_sizes; }

  std::ptrdiff_t distance_to(tuple_iterator const& other) const {
    return (other.ptr_ - ptr_) / tuple_info<Tup>::sum_sizes;
  }

  template <int k> void constexpr unpack(pointer_tuple_t& ptrTup) const {
    if constexpr (k < tuple_info<Tup>::size) {
      auto& ref = std::get<k>(ptrTup);
      ref = reinterpret_cast<std::tuple_element_t<k, Tup>*>(
          ptr_ + tuple_info<Tup>::offsets[k]);
      unpack<k + 1>(ptrTup);
    }
  }

  bool equal(tuple_iterator<Tup> const& other) const {
    return ptr_ == other.ptr_;
  }

  ref_tuple_t dereference() const {
    pointer_tuple_t element_addresses{};

    unpack<0>(element_addresses);

    return std::apply(
        [](auto... args) { return detail::dereference_impl(args...); },
        element_addresses);
  }

public:
  std::byte* ptr_; //!< pointer to first byte of packed sequence
};

} // namespace cnpypp
