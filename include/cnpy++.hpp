// Copyright (C) 2011  Carl Rogers, 2020-2022 Maximilian Reininghaus
// Released under MIT License
// license available in LICENSE file, or at
// http://www.opensource.org/licenses/mit-license.php

#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <map>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <stdint.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/endian/buffers.hpp>

#ifndef NO_LIBZIP
#include <zip.h>
#endif

#if defined(MSGSL_SPAN)
#include <gsl/span>
#elif defined(GSL_LITE_SPAN)
#include <gsl-lite/gsl-lite.hpp>
#elif defined(BOOST_SPAN)
#include <boost/core/span.hpp>
#else
#include <span>
#endif

#include <cnpy++.h>
#include <cnpy++/buffer.hpp>
#include <cnpy++/map_type.hpp>
#include <cnpy++/stride_iterator.hpp>
#include <cnpy++/tuple_util.hpp>

namespace cnpypp {
template <typename T>
#if defined(MSGSL_SPAN)
using span = gsl::span<T>;
#elif defined(GSL_LITE_SPAN)
using span = gsl_lite::span<T>;
#elif defined(BOOST_SPAN)
using span = boost::span<T>;
#else
using span = std::span<T>;
#endif

namespace detail {
struct additional_parameters {
  additional_parameters(
      std::vector<char>&& _npyheader, size_t size,
      std::function<size_t(cnpypp::span<char>, additional_parameters*)> _func)
      : npyheader{std::move(_npyheader)},
        header_bytes_remaining{npyheader.size()}, buffer_capacity{size},
        buffer{std::make_unique<char[]>(size)}, func{_func} {}

  std::vector<char> const npyheader;
  size_t const buffer_capacity;

  size_t header_bytes_remaining, bytes_buffer_written = 0, buffer_size = 0;
  std::unique_ptr<char[]> const buffer;
  std::function<size_t(cnpypp::span<char>, additional_parameters*)> const func;
};

#ifndef NO_LIBZIP
zip_int64_t npzwrite_source_callback(void*, void*, zip_uint64_t,
                                     zip_source_cmd_t);
#endif
} // namespace detail

enum class MemoryOrder {
  Fortran = cnpypp_memory_order_fortran,
  C = cnpypp_memory_order_c,
  ColumnMajor = Fortran,
  RowMajor = C
};

struct NpyArray {
  NpyArray(NpyArray&& other)
      : shape{std::move(other.shape)}, word_sizes{std::move(other.word_sizes)},
        memory_order{other.memory_order}, num_vals{other.num_vals},
        total_value_size{other.total_value_size}, buffer{std::move(
                                                      other.buffer)} {}

  NpyArray(std::vector<size_t> _shape, std::vector<size_t> _word_sizes,
           std::vector<std::string> _labels, MemoryOrder _memory_order,
           std::unique_ptr<Buffer> _buffer)
      : shape{std::move(_shape)},
        word_sizes{std::move(_word_sizes)}, labels{std::move(_labels)},
        memory_order{_memory_order}, num_vals{std::accumulate(
                                         shape.begin(), shape.end(), size_t{1},
                                         std::multiplies<size_t>())},
        total_value_size{std::accumulate(word_sizes.begin(), word_sizes.end(),
                                         size_t{0}, std::plus<size_t>())},
        buffer{std::move(_buffer)} {}
  //~ : std::make_unique<InMemoryBuffer>(total_value_size *
  //~ num_vals)} {}

  NpyArray(NpyArray const&) = delete;

  template <typename T> T* data() {
    return reinterpret_cast<T*>(buffer->data());
  }

  template <typename T> const T* data() const {
    return reinterpret_cast<T const*>(buffer->data());
  }

  size_t num_bytes() const { return num_vals * total_value_size; }

  bool compare_metadata(NpyArray const& other) const {
    return shape == other.shape && word_sizes == other.word_sizes &&
           labels == other.labels && memory_order == other.memory_order;
  }

  bool operator==(NpyArray const& other) const {
    return compare_metadata(other) &&
           !std::memcmp(this->data<void>(), other.data<void>(), num_bytes());
  }

  template <typename T> T* begin() { return data<T>(); }
  template <typename T> T const* begin() const { return data<T>(); }

  template <typename T> T const* cbegin() const { return data<T>(); }

  template <typename T> T* end() { return data<T>() + num_vals; }
  template <typename T> T const* end() const { return data<T>() + num_vals; }

  template <typename T> T const* cend() const { return data<T>() + num_vals; }

  template <typename T> subrange<T*, T*> make_range() {
    return subrange{begin<T>(), end<T>()};
  }

  template <typename T> subrange<T const*, T const*> make_range() const {
    return subrange{cbegin<T>(), cend<T>()};
  }

  template <typename... TArgs>
  subrange<tuple_iterator<std::tuple<TArgs...>>>
  tuple_range(bool force_check = false) {
    // TODO: refactor this to remove code duplication
    // see Scott Meyers "Avoid Duplication in const and Non-const Member
    // Function"

    if (force_check && !compare_word_sizes<TArgs...>()) {
      throw std::runtime_error(
          "tuple_range: word sizes do not match requested types");
    } else {
      return subrange{tuple_iterator<std::tuple<TArgs...>>{buffer->data()},
                      tuple_iterator<std::tuple<TArgs...>>{
                          buffer->data() + num_vals * total_value_size}};
    }
  }

  template <typename... TArgs>
  subrange<tuple_iterator<add_const_t<std::tuple<TArgs...>>>>
  tuple_range(bool force_check = false) const {
    if (force_check && !compare_word_sizes<TArgs...>()) {
      throw std::runtime_error(
          "tuple_range: word sizes do not match requested types");
    } else {
      return subrange{
          tuple_iterator<add_const_t<std::tuple<TArgs...>>>{buffer.get()},
          tuple_iterator<add_const_t<std::tuple<TArgs...>>>{
              buffer->data() + num_vals * total_value_size}};
    }
  }

  template <typename TValueType>
  subrange<stride_iterator<TValueType>> column_range(std::string_view name) {
    // TODO: refactor this to remove code duplication
    // see Scott Meyers "Avoid Duplication in const and Non-const Member
    // Function"

    if (auto it = std::find(labels.cbegin(), labels.cend(), name);
        it == labels.cend()) {
      std::stringstream ss;
      ss << "column_range: " << std::quoted(name) << " not found in labels";
      throw std::runtime_error{ss.str().c_str()};
    } else {
      std::ptrdiff_t const d = std::distance(labels.cbegin(), it);

      if (word_sizes.at(d) != sizeof(TValueType)) {
        throw std::runtime_error{
            "column_range: word sizes of requested type and data do not match"};
      }

      ptrdiff_t const offset =
          std::accumulate(word_sizes.cbegin(),
                          std::next(word_sizes.cbegin(), d), std::ptrdiff_t{0});

      auto beg =
          stride_iterator<TValueType>{buffer.get() + offset, total_value_size};
      auto end = stride_iterator<TValueType>{buffer.get() + offset +
                                                 total_value_size * num_vals,
                                             total_value_size};
      return subrange{beg, end};
    }
  }

  template <typename TValueType>
  subrange<stride_iterator<TValueType const>>
  column_range(std::string_view name) const {
    if (auto it = std::find(labels.cbegin(), labels.cend(), name);
        it == labels.cend()) {
      std::stringstream ss;
      ss << "column_range: " << std::quoted(name) << " not found in labels";
      throw std::runtime_error{ss.str().c_str()};
    } else {
      std::ptrdiff_t const d = std::distance(labels.cbegin(), it);

      if (word_sizes.at(d) != sizeof(TValueType)) {
        throw std::runtime_error{
            "column_range: word sizes of requested type and data do not match"};
      }

      ptrdiff_t const offset =
          std::accumulate(word_sizes.cbegin(),
                          std::next(word_sizes.cbegin(), d), std::ptrdiff_t{0});

      auto beg = stride_iterator<TValueType const>{buffer.get() + offset,
                                                   total_value_size};
      auto end = stride_iterator<TValueType const>{
          buffer.get() + offset + total_value_size * num_vals,
          total_value_size};
      return subrange{beg, end};
    }
  }

  std::vector<size_t> const shape;
  std::vector<size_t> const word_sizes;
  std::vector<std::string> const labels;
  MemoryOrder const memory_order;
  size_t const num_vals;
  size_t const total_value_size;

private:
  std::unique_ptr<Buffer> buffer;

  template <typename... TArgs> bool compare_word_sizes() const {
    auto const& requested_type_sizes =
        tuple_info<std::tuple<TArgs...>>::element_sizes;
    return std::equal(requested_type_sizes.cbegin(),
                      requested_type_sizes.cend(), word_sizes.cbegin(),
                      word_sizes.cend());
  }
};

using npz_t = std::map<std::string, NpyArray>;

char BigEndianTest();

bool _exists(std::string const&); // calls boost::filesystem::exists()

std::vector<char> create_npy_header(cnpypp::span<size_t const> shape,
                                    char dtype, int size,
                                    MemoryOrder = MemoryOrder::C);

std::vector<char> create_npy_header(cnpypp::span<size_t const> shape,
                                    cnpypp::span<std::string_view const> labels,
                                    cnpypp::span<char const> dtypes,
                                    cnpypp::span<size_t const> sizes,
                                    MemoryOrder memory_order);

void parse_npy_header(std::istream& fs, std::vector<size_t>& word_sizes,
                      std::vector<char>& data_types,
                      std::vector<std::string>& labels,
                      std::vector<size_t>& shape,
                      cnpypp::MemoryOrder& memory_order);

void parse_npy_header(std::istream::char_type const* buffer,
                      std::vector<size_t>& word_sizes,
                      std::vector<char>& data_types,
                      std::vector<std::string>& labels,
                      std::vector<size_t>& shape, MemoryOrder& memory_order);

void parse_npy_dict(cnpypp::span<std::istream::char_type const> buffer,
                    std::vector<size_t>& word_sizes,
                    std::vector<char>& data_types,
                    std::vector<std::string>& labels,
                    std::vector<size_t>& shape,
                    cnpypp::MemoryOrder& memory_order);

npz_t npz_load(std::string const& fname);

NpyArray npz_load(std::string const& fname, std::string const& varname);

NpyArray npy_load(std::string const& fname, bool memory_mapped = false);

template <typename TConstInputIterator>
bool constexpr is_contiguous_v =
#if __cpp_lib_concepts >= 202002L
    std::contiguous_iterator<TConstInputIterator>;
#else
#warning "no concept support available - fallback to std::is_pointer_v"
    std::is_pointer_v<TConstInputIterator>; // unfortunately, is_pointer is less
                                            // sharp (e.g., doesn't bite on
                                            // std::vector<>::iterator)
#endif

// if it comes from contiguous memory, dump directly in file
template <typename TConstInputIterator,
          std::enable_if_t<is_contiguous_v<TConstInputIterator>, int> = 0>
void write_data(TConstInputIterator start, size_t nels, std::ostream& fs) {
  using value_type =
      typename std::iterator_traits<TConstInputIterator>::value_type;

  auto constexpr size_elem = sizeof(value_type);

  fs.write(reinterpret_cast<std::ostream::char_type const*>(&*start),
           nels * size_elem / sizeof(std::ostream::char_type));
}

// otherwise do it in chunks with a buffer
template <typename TConstInputIterator,
          std::enable_if_t<!is_contiguous_v<TConstInputIterator>, int> = 0>
void write_data(TConstInputIterator start, size_t nels, std::ostream& fs) {
  using value_type =
      typename std::iterator_traits<TConstInputIterator>::value_type;

  size_t const buffer_size = std::min(nels, 0x10000ul);

  auto buffer = std::make_unique<value_type[]>(buffer_size);

  size_t elements_written = 0;
  auto it = start;

  while (elements_written < nels) {
    size_t count = 0;
    while (count < buffer_size && elements_written < nels) {
      buffer[count] = *it;
      ++it;
      ++count;
      ++elements_written;
    }
    write_data(buffer.get(), count, fs);
  }
}

template <typename T, int k = 0> void fill(T const& tup, char* buffer) {
  auto constexpr offsets = tuple_info<T>::offsets;

  if constexpr (k < tuple_info<T>::size) {
    auto const& elem = std::get<k>(tup);
    auto constexpr elem_size = sizeof(elem);
    static_assert(tuple_info<T>::element_sizes[k] == elem_size); // sanity check

    char const* const beg = reinterpret_cast<char const*>(&elem);

    std::copy(beg, beg + elem_size, buffer + offsets[k]);
    fill<T, k + 1>(tup, buffer);
  }
}

template <typename TTupleIterator>
void write_data_tuple(TTupleIterator start, size_t nels, std::ostream& fs) {
  using value_type = typename std::iterator_traits<TTupleIterator>::value_type;
  static auto constexpr sizes = tuple_info<value_type>::element_sizes;
  static auto constexpr sum = tuple_info<value_type>::sum_sizes;
  static auto constexpr offsets = tuple_info<value_type>::offsets;

  size_t const buffer_size = std::min(nels, 0x10000ul); // number of tuples

  auto buffer = std::make_unique<char[]>(buffer_size * sum);

  size_t elements_written = 0;
  auto it = start;

  while (elements_written < nels) {
    size_t count = 0;
    while (count < buffer_size && elements_written < nels) {
      auto const& tup = *it;

      fill<value_type>(tup, buffer.get() + count * sum);

      ++it;
      ++count;
      ++elements_written;
    }
    write_data(buffer.get(), count * sum, fs);
  }
}

template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
std::vector<char>& operator+=(std::vector<char>& lhs, const T rhs) {
  // write in little endian
  boost::endian::endian_buffer<boost::endian::order::little, T,
                               sizeof(T) * CHAR_BIT> const buffer{rhs};

  for (auto const* ptr = buffer.data(); ptr < buffer.data() + sizeof(T);
       ++ptr) {
    lhs.push_back(*ptr);
  }

  return lhs;
}

std::vector<char>& append(std::vector<char>&, std::string_view);

template <typename TConstInputIterator>
void npy_save(std::string const& fname, TConstInputIterator start,
              cnpypp::span<size_t const> const shape,
              std::string_view mode = "w",
              MemoryOrder memory_order = MemoryOrder::C) {
  std::fstream fs;
  std::vector<size_t>
      true_data_shape; // if appending, the shape of existing + new data

  using value_type =
      typename std::iterator_traits<TConstInputIterator>::value_type;

  // forbid implementations of std::bool with sizeof(bool) != 1
  // numpy can't handle these
  static_assert(sizeof(value_type) == 1 || !std::is_same_v<value_type, bool>,
                "platforms with sizeof(bool) != 1 not supported");

  if (mode == "a" && _exists(fname)) {
    // file exists. we need to append to it. read the header, modify the array
    // size
    fs.open(fname,
            std::ios_base::binary | std::ios_base::in | std::ios_base::out);

    std::vector<size_t> word_sizes_exist;
    std::vector<char> data_types_exist;
    std::vector<std::string> labels_exist;
    cnpypp::MemoryOrder memory_order_exist;

    parse_npy_header(fs, word_sizes_exist, data_types_exist, labels_exist,
                     true_data_shape, memory_order_exist);

    if (sizeof(value_type) != word_sizes_exist.at(0)) {
      throw std::runtime_error{
          "npy_save(): appending failed: element size not matching"};
    } else if (map_type(value_type{}) != data_types_exist.at(0)) {
      throw std::runtime_error{
          "npy_save(): appending failed: data type descriptor not matching"};
    }

    if (memory_order != memory_order_exist) {
      throw std::runtime_error{
          "libcnpy++ error in npy_save(): memory order does not match"};
    }

    if (true_data_shape.size() != shape.size()) {
      throw std::runtime_error{"npy_save: ranks not matching"};
    }

    if (!std::equal(std::next(shape.begin()), shape.end(),
                    std::next(true_data_shape.begin()))) {
      std::stringstream ss;
      ss << "libnpy error: npy_save attempting to append misshaped data to "
         << std::quoted(fname);
      throw std::runtime_error{ss.str().c_str()};
    }

    if (memory_order == MemoryOrder::C)
      true_data_shape.front() += shape.front();
    else
      true_data_shape.back() += shape.back();

  } else { // write mode
    fs.open(fname, std::ios_base::binary | std::ios_base::out);
    true_data_shape = std::vector<size_t>{shape.begin(), shape.end()};
  }

  std::vector<char> const header =
      create_npy_header(true_data_shape, map_type(value_type{}),
                        sizeof(value_type), memory_order);
  size_t const nels =
      std::accumulate(shape.begin(), shape.end(), 1, std::multiplies<size_t>());

  fs.seekp(0, std::ios_base::beg);
  fs.write(&header[0], sizeof(char) * header.size());
  fs.seekp(0, std::ios_base::end);

  // now write actual data
  write_data(start, nels, fs);
}

template <typename TConstInputIterator>
void npy_save(std::string const& fname, TConstInputIterator start,
              std::initializer_list<size_t> const shape,
              std::string_view mode = "w",
              MemoryOrder memory_order = MemoryOrder::C) {
  npy_save<TConstInputIterator>(
      fname, start, cnpypp::span<size_t const>{std::data(shape), shape.size()},
      mode, memory_order);
}

#ifndef NO_LIBZIP
std::tuple<size_t, zip_t*> prepare_npz(std::string const& zipname,
                                       cnpypp::span<size_t const> const shape,
                                       std::string_view mode);
#endif

#ifndef NO_LIBZIP
void finalize_npz(zip_t*, std::string, detail::additional_parameters&);
#endif

#ifndef NO_LIBZIP
template <typename TConstInputIterator>
void npz_save(std::string const& zipname, std::string const& fname,
              TConstInputIterator start, cnpypp::span<size_t const> const shape,
              std::string_view mode = "w",
              MemoryOrder memory_order = MemoryOrder::C) {
  using value_type =
      typename std::iterator_traits<TConstInputIterator>::value_type;
  size_t constexpr wordsize = sizeof(value_type);

  // forbid implementations of std::bool with sizeof(bool) != 1
  // numpy can't handle these
  static_assert(sizeof(value_type) == 1 || !std::is_same_v<value_type, bool>,
                "platforms with sizeof(bool) != 1 not supported");

  auto [nels, archive] = prepare_npz(zipname, shape, mode);
  auto const Nels = nels; // clang++ can't capture nels in lambda (?)

  size_t elements_written_total = 0;

  auto callback = [&it = start, nels = Nels, wordsize, &elements_written_total](
                      cnpypp::span<char> libzip_buffer,
                      detail::additional_parameters* parameters) -> size_t {
    size_t const n_tbw = std::min(libzip_buffer.size() / wordsize,
                                  nels - elements_written_total);
    value_type* libzip_word_buffer =
        reinterpret_cast<value_type*>(libzip_buffer.data());

    for (size_t i = 0; i < n_tbw; ++i) {
      libzip_word_buffer[i] = *(it++);
    }

    elements_written_total += n_tbw;

    if (elements_written_total < nels &&
        libzip_buffer.size() > wordsize * n_tbw) {
      // some space left that could not be filled with a single element
      // write one into temp. buffer
      auto* const tmp = reinterpret_cast<value_type*>(&parameters->buffer[0]);
      *tmp = *(it++);
      parameters->buffer_size = wordsize;

      ++elements_written_total;
    }

    return n_tbw * wordsize; // number of bytes written to libzip's buffer
  };

  detail::additional_parameters parameters{
      create_npy_header(shape, map_type(value_type{}), wordsize, memory_order),
      wordsize, callback};

  finalize_npz(archive, fname, parameters);
}
#endif

#ifndef NO_LIBZIP
template <typename TTupleIterator>
void npz_save(std::string const& zipname, std::string const& fname,
              std::vector<std::string_view> const& labels, TTupleIterator first,
              cnpypp::span<size_t const> const shape,
              std::string_view mode = "w",
              MemoryOrder memory_order = MemoryOrder::C) {
  using value_type = typename std::iterator_traits<TTupleIterator>::value_type;

  // forbid implementations of std::bool with sizeof(bool) != 1
  // numpy can't handle these
  static_assert(sizeof(bool) == 1 || !tuple_info<value_type>::has_bool_element,
                "platforms with sizeof(bool) != 1 not supported");

  if (labels.size() != std::tuple_size_v<value_type>) {
    throw std::runtime_error(
        "libcnpy++: number of labels does not match tuple size");
  }

  static auto constexpr dtypes = tuple_info<value_type>::data_types;
  static auto constexpr sizes = tuple_info<value_type>::element_sizes;
  auto constexpr sum_size = tuple_info<value_type>::sum_sizes;

  // forbid implementations of std::bool with sizeof(bool) != 1
  // numpy can't handle these
  static_assert(sizeof(bool) == 1 || !tuple_info<value_type>::has_bool_element,
                "platforms with sizeof(bool) != 1 not supported");

  auto [nels, archive] = prepare_npz(zipname, shape, mode);
  auto const Nels = nels; // clang++ can't capture nels in lambda (?)
  size_t elements_written_total = 0;

  auto callback = [&it = first, nels = Nels, sum_size, &elements_written_total](
                      cnpypp::span<char> libzip_buffer,
                      detail::additional_parameters* parameters) -> size_t {
    size_t const n_tbw = std::min(libzip_buffer.size() / sum_size,
                                  nels - elements_written_total);

    for (size_t i = 0; i < n_tbw; ++i) {
      auto const& tup = *(it++);
      fill<value_type>(tup, libzip_buffer.data() + i * sum_size);
    }

    elements_written_total += n_tbw;

    if (elements_written_total < nels &&
        libzip_buffer.size() > sum_size * n_tbw) {
      // some space left that could not be filled with a single element
      // write one into temp. buffer
      char* const tmp = reinterpret_cast<char*>(&parameters->buffer[0]);
      auto const& tup = *(it++);
      fill<value_type>(tup, parameters->buffer.get());
      parameters->buffer_size = sum_size;

      ++elements_written_total;
    }

    return n_tbw * sum_size; // number of bytes written to libzip's buffer
  };

  detail::additional_parameters parameters{
      create_npy_header(shape, labels, dtypes, sizes, memory_order), sum_size,
      callback};

  finalize_npz(archive, fname, parameters);
}
#endif

#ifndef NO_LIBZIP
template <typename TConstInputIterator>
void npz_save(std::string const& zipname, std::string fname,
              TConstInputIterator start,
              std::initializer_list<size_t const> shape,
              std::string_view mode = "w",
              MemoryOrder memory_order = MemoryOrder::C) {
  npz_save(zipname, std::move(fname), start,
           cnpypp::span<size_t const>{std::data(shape), shape.size()}, mode,
           memory_order);
}
#endif

template <typename TForwardIterator>
void npy_save(std::string const& fname, TForwardIterator first,
              TForwardIterator last, std::string_view mode = "w") {
  static_assert(
      std::is_base_of_v<
          std::forward_iterator_tag,
          typename std::iterator_traits<TForwardIterator>::iterator_category>,
      "forward iterator necessary");

  auto const dist = std::distance(first, last);
  if (dist < 0) {
    throw std::runtime_error(
        "npy_save() called with negative-distance iterators");
  }

  npy_save(fname, first, {static_cast<size_t>(dist)}, mode);
}

template <typename T>
void npy_save(std::string const& fname, cnpypp::span<T const> data,
              std::string_view mode = "w") {
  npy_save<T>(fname, data.cbegin(), data.cend(), mode);
}

template <typename TTupleIterator>
void npy_save(std::string const& fname,
              std::vector<std::string_view> const& labels, TTupleIterator first,
              cnpypp::span<size_t const> const shape,
              std::string_view mode = "w",
              MemoryOrder memory_order = MemoryOrder::C) {
  using value_type = typename std::iterator_traits<TTupleIterator>::value_type;

  if (labels.size() != std::tuple_size_v<value_type>) {
    throw std::runtime_error("number of labels does not match tuple size");
  }

  auto constexpr& dtypes = tuple_info<value_type>::data_types;
  auto constexpr& sizes = tuple_info<value_type>::element_sizes;

  std::fstream fs;
  std::vector<size_t>
      true_data_shape; // if appending, the shape of existing + new data

  if (mode == "a" && _exists(fname)) {
    // file exists. we need to append to it. read the header, modify the array
    // size
    fs.open(fname,
            std::ios_base::binary | std::ios_base::in | std::ios_base::out);

    std::vector<size_t> word_sizes_exist, shape;
    std::vector<char> data_types_exist;
    std::vector<std::string> labels_exist;
    cnpypp::MemoryOrder memory_order_exist;

    parse_npy_header(fs, word_sizes_exist, data_types_exist, labels_exist,
                     true_data_shape, memory_order_exist);

    if (tuple_info<value_type>::size != labels_exist.size()) {
      throw std::runtime_error{"libcnpy++ error in npy_save(): appending "
                               "failed: sizes not matching"};
    }
    if (!std::equal(data_types_exist.cbegin(), data_types_exist.cend(),
                    dtypes.cbegin())) {
      throw std::runtime_error{"libcnpy++ error in npy_save(): appending "
                               "failed: data type descriptors not matching"};
    }
    if (!std::equal(word_sizes_exist.cbegin(), word_sizes_exist.cend(),
                    sizes.cbegin())) {
      throw std::runtime_error{"libcnpy++ error in npy_save(): appending "
                               "failed: element sizes not matching"};
    }

    if (memory_order != memory_order_exist) {
      throw std::runtime_error{
          "libcnpy++ error in npy_save(): memory order does not match"};
    }

    if (true_data_shape.size() != shape.size()) {
      std::stringstream ss;
      ss << "libcnpy++ error: npy_save attempting to append misdimensioned "
            "data to "
         << std::quoted(fname);
      throw std::runtime_error{ss.str().c_str()};
    }

    if (shape.size() > 0 && !std::equal(std::next(shape.begin()), shape.end(),
                                        std::next(true_data_shape.begin()))) {
      std::stringstream ss;
      ss << "libcnpy++ error: npy_save attempting to append misshaped data to "
         << std::quoted(fname);
      throw std::runtime_error{ss.str().c_str()};
    }

    if (memory_order == MemoryOrder::C)
      true_data_shape.front() += shape.front();
    else
      true_data_shape.back() += shape.back();

  } else { // write mode
    fs.open(fname, std::ios_base::binary | std::ios_base::out);
    true_data_shape = std::vector<size_t>{shape.begin(), shape.end()};
  }

  auto const header =
      create_npy_header(shape, labels, dtypes, sizes, memory_order);

  size_t const nels =
      std::accumulate(shape.begin(), shape.end(), 1, std::multiplies<size_t>());

  fs.seekp(0, std::ios_base::beg);
  fs.write(&header[0], sizeof(char) * header.size());
  fs.seekp(0, std::ios_base::end);

  // now write actual data
  write_data_tuple(first, nels, fs);
}

template <typename TTupleIterator>
void npy_save(std::string const& fname,
              std::vector<std::string_view> const& labels, TTupleIterator first,
              std::initializer_list<size_t const> shape,
              std::string_view mode = "w",
              MemoryOrder memory_order = MemoryOrder::C) {
  npy_save<TTupleIterator>(
      fname, labels, first,
      cnpypp::span<size_t const>{std::data(shape), shape.size()}, mode,
      memory_order);
}
} // namespace cnpypp
