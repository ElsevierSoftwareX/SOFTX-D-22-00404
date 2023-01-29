#include <cstdint>
#include <iostream>
#include <list>
#include <string_view>
#include <vector>

#include <cnpy++.hpp>

int main() {
  std::string_view const str1 = "abcdefghijklmno";
  std::string_view const str2 = "pqrstuvwxyz";

  // now write to an npz file
  {
    cnpypp::npz_save("out.npz", "str", str1.cbegin(), {str1.size()}, "w");

    // load str1 back from npz file
    cnpypp::NpyArray arr = cnpypp::npz_load("out.npz", "str");
    auto const* const loaded_data = arr.data<char>();

    // make sure the loaded data matches the saved data
    if (!(arr.word_sizes.at(0) == sizeof(decltype(str1)::value_type))) {
      std::cerr << "error in line " << __LINE__ << std::endl;
      return EXIT_FAILURE;
    }

    if (arr.shape.size() != 1 || arr.shape.at(0) != str1.size()) {
      std::cerr << "error in line " << __LINE__ << std::endl;
      return EXIT_FAILURE;
    }

    if (!std::equal(str1.cbegin(), str1.cend(), loaded_data)) {
      std::cerr << "error in line " << __LINE__ << std::endl;
      return EXIT_FAILURE;
    }
  }

  // append to npz
  {
    cnpypp::npz_save("out.npz", "str2", str2.cbegin(), {str2.size()}, "a");

    // load str2 back from npz file
    cnpypp::NpyArray arr = cnpypp::npz_load("out.npz", "str2");
    auto const* const loaded_data = arr.data<char>();

    // make sure the loaded data matches the saved data
    if (!(arr.word_sizes.at(0) == sizeof(decltype(str2)::value_type))) {
      std::cerr << "error in line " << __LINE__ << std::endl;
      return EXIT_FAILURE;
    }

    if (arr.shape.size() != 1 || arr.shape.at(0) != str2.size()) {
      std::cerr << "error in line " << __LINE__ << std::endl;
      return EXIT_FAILURE;
    }

    if (!std::equal(str2.cbegin(), str2.cend(), loaded_data)) {
      std::cerr << "error in line " << __LINE__ << std::endl;
      return EXIT_FAILURE;
    }
  }

  std::list<uint32_t> const list_u{str1.cbegin(), str1.cend()}; // copy to list
  std::list<float_t> const list_f{str2.cbegin(), str2.cend()};

  std::vector<size_t> const shape_u = {list_u.size()};
  std::vector<size_t> const shape_f = {list_f.size()};

  // append list to npz
  {
    cnpypp::npz_save("out.npz", "arr1", list_u.cbegin(), shape_u, "a");
    cnpypp::npz_save("out.npz", "arr2", list_f.cbegin(), shape_f, "a");
  }

  // load the entire npz file
  {
    cnpypp::npz_t my_npz = cnpypp::npz_load("out.npz");

    {
      cnpypp::NpyArray const& arr = my_npz.find("str")->second;
      char const* const loaded_str = arr.data<char>();

      // make sure the loaded data matches the saved data
      if (!(arr.word_sizes.at(0) == sizeof(decltype(str1)::value_type))) {
        std::cerr << "error in line " << __LINE__ << std::endl;
        return EXIT_FAILURE;
      }

      if (arr.shape.size() != 1 || arr.shape.at(0) != str1.size()) {
        std::cerr << "error in line " << __LINE__ << std::endl;
        return EXIT_FAILURE;
      }

      if (!std::equal(str1.cbegin(), str1.cend(), loaded_str)) {
        std::cerr << "error in line " << __LINE__ << std::endl;
        return EXIT_FAILURE;
      }
    }
    {
      cnpypp::NpyArray const& arr = my_npz.find("arr1")->second;
      cnpypp::NpyArray const& arr2 = my_npz.find("arr2")->second;
      uint32_t const* const loaded_arr_u = arr.data<uint32_t>();
      float const* const loaded_arr_f = arr2.data<float>();

      // make sure the loaded data matches the saved data
      if (arr.word_sizes.at(0) != sizeof(uint32_t) ||
          arr2.word_sizes.at(0) != sizeof(float)) {
        std::cerr << "error in line " << __LINE__ << std::endl;
        return EXIT_FAILURE;
      }

      if (arr.shape != shape_u || arr2.shape != shape_f) {
        std::cerr << "error in line " << __LINE__ << std::endl;
        return EXIT_FAILURE;
      }

      if (!std::equal(list_u.cbegin(), list_u.cend(), loaded_arr_u)) {
        std::cerr << "error in line " << __LINE__ << std::endl;
        return EXIT_FAILURE;
      }

      if (!std::equal(list_f.cbegin(), list_f.cend(), loaded_arr_f)) {
        std::cerr << "error in line " << __LINE__ << std::endl;
        return EXIT_FAILURE;
      }
    }
  }
}
