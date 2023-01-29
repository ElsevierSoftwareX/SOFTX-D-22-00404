#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <numeric>
#include <string>
#include <string_view>

#include <boost/iterator/zip_iterator.hpp>
#include <boost/type_index.hpp>

#include "cnpy++.hpp"

static const int Nx = 2;
static const int Ny = 4;
static const int Nz = 8;

static const int Nelem = Nx * Ny * Nz;

static std::vector<size_t> const shape{Nz, Ny, Nx};

int main() {
  auto const data = std::invoke([]() {
    std::vector<uint32_t> data(Nx * Ny * Nz);
    std::iota(data.begin(), data.end(), 1);
    return data;
  });

  // save it to file
  cnpypp::npy_save("arr1.npy", &data[0], shape, "w");          // via pointer
  cnpypp::npy_save("arr1-cpy.npy", data.cbegin(), shape, "w"); // via iterator

  // load it into a new array
  {
    cnpypp::NpyArray const arr = cnpypp::npy_load("arr1.npy", true);
    auto const* const loaded_data = arr.data<uint32_t>();

    // make sure the loaded data matches the saved data
    if (!(arr.word_sizes.at(0) == sizeof(decltype(data)::value_type))) {
      std::cerr << "error in line " << __LINE__ << std::endl;
      return EXIT_FAILURE;
    }

    if (arr.shape != shape) {
      std::cerr << "error in line " << __LINE__ << std::endl;
      return EXIT_FAILURE;
    }

    if (!std::equal(data.cbegin(), data.cend(), loaded_data)) {
      std::cerr << "error in line " << __LINE__ << std::endl;
      return EXIT_FAILURE;
    }
  }

  // append the same data to file
  // npy array on file now has shape (Nz+Nz,Ny,Nx)
  cnpypp::npy_save("arr1.npy", data.cbegin(), shape, "a");

  {
    cnpypp::NpyArray const arr = cnpypp::npy_load("arr1.npy");
    auto const* const loaded_data = arr.data<uint32_t>();

    // make sure the loaded data matches the saved data
    if (!(arr.word_sizes.at(0) == sizeof(decltype(data)::value_type))) {
      std::cerr << "error in line " << __LINE__ << std::endl;
      return EXIT_FAILURE;
    }

    auto constexpr new_shape = std::array{16, 4, 2};
    auto constexpr new_size =
        new_shape[0] * new_shape[1] *
        new_shape[2]; // std::accumulate(new_shape.begin(), new_shape.end(), 1,
                      // std::multiplies<size_t>());
    auto const complete_new_data = std::invoke([new_size]() {
      std::vector<uint32_t> data(new_size);
      std::iota(data.begin(), std::next(data.begin(), new_size / 2), 1);
      std::iota(std::next(data.begin(), new_size / 2), data.end(), 1);
      return data;
    });

    if (!std::equal(new_shape.begin(), new_shape.end(), arr.shape.begin())) {
      std::cerr << "error in line " << __LINE__ << std::endl;
      return EXIT_FAILURE;
    }

    if (!std::equal(complete_new_data.cbegin(), complete_new_data.cend(),
                    loaded_data)) {
      std::cerr << "error in line " << __LINE__ << std::endl;
      return EXIT_FAILURE;
    }
  }

  // tuples written to NPY with structured data type
  {
    std::vector<std::tuple<int32_t, int8_t, int16_t>> const tupleVec{
        {0xaaaaaaaa, 0xbb, 0xcccc},
        {0xdddddddd, 0xee, 0xffff},
        {0x99999999, 0x88, 0x7777}};

    cnpypp::npy_save("structured.npy", {"a", "b", "c"}, tupleVec.begin(),
                     {tupleVec.size()});

    // load memory-mapped
    cnpypp::NpyArray arr = cnpypp::npy_load("structured.npy", true);
    auto r = arr.tuple_range<int32_t, int8_t, int16_t>();

    if (!std::equal(tupleVec.begin(), tupleVec.end(), r.begin())) {
      std::cerr << "error in line " << __LINE__ << std::endl;
      return EXIT_FAILURE;
    }
  }

  // std::array written as structured type
  {

    std::vector<std::array<int8_t, 2>> const arrVec{
        {0x11, 0x22}, {0x33, 0x44}, {0x55, 0x66}};

    cnpypp::npy_save("structured2.npy", {"a", "b"}, arrVec.begin(),
                     {arrVec.size()});

    // load memory-mapped
    cnpypp::NpyArray arr = cnpypp::npy_load("structured2.npy", true);
    auto r = arr.tuple_range<int8_t, int8_t>();

    if (!std::equal(arrVec.begin(), arrVec.end(), r.begin(),
                    [](auto const& a, auto const& b) {
                      return a[0] == std::get<0>(b) && a[1] == std::get<1>(b);
                    })) {
      std::cerr << "error in line " << __LINE__ << std::endl;
      return EXIT_FAILURE;
    }
  }

  return EXIT_SUCCESS;
}
