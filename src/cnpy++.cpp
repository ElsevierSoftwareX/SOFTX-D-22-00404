// Copyright (C) 2011  Carl Rogers, 2020-2022 Maximilian Reininghaus
// Released under MIT License
// license available in LICENSE file, or at
// http://www.opensource.org/licenses/mit-license.php

#include <algorithm>
#include <complex>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <regex>
#include <stdexcept>
#include <stdint.h>

#include <boost/endian/conversion.hpp>
#include <boost/filesystem.hpp>
#include <boost/iostreams/device/mapped_file.hpp>

#ifndef NO_LIBZIP
#include <zip.h>
#endif

#include "cnpy++.hpp"

using namespace cnpypp;

char cnpypp::BigEndianTest() {
  int32_t const x = 1;
  static_assert(sizeof(x) > 1);

  return (((char*)&x)[0]) ? '<' : '>';
}

std::vector<char>& cnpypp::append(std::vector<char>& vec,
                                  std::string_view view) {
  vec.insert(vec.end(), view.begin(), view.end());
  return vec;
}

bool cnpypp::_exists(std::string const& fname) {
  return boost::filesystem::exists(fname);
}

static std::regex const num_regex("[0-9][0-9]*");
static std::regex const
    dtype_tuple_regex("\\('(\\w+)', '([<>|])([a-zA-z])(\\d+)'\\)");

void cnpypp::parse_npy_header(std::istream::char_type const* buffer,
                              std::vector<size_t>& word_sizes,
                              std::vector<char>& data_types,
                              std::vector<std::string>& labels,
                              std::vector<size_t>& shape,
                              cnpypp::MemoryOrder& memory_order) {
  uint8_t const major_version = *reinterpret_cast<uint8_t const*>(buffer + 6);
  uint8_t const minor_version = *reinterpret_cast<uint8_t const*>(buffer + 7);
  uint16_t const header_len =
      boost::endian::endian_load<boost::uint16_t, 2,
                                 boost::endian::order::little>(
          (unsigned char const*)buffer + 8);
  cnpypp::span<char const> header(reinterpret_cast<char const*>(buffer + 0x0a),
                                  header_len);

  if (!(major_version == 1 && minor_version == 0)) {
    throw std::runtime_error("parse_npy_header: version not supported");
  }

  parse_npy_dict(header, word_sizes, data_types, labels, shape, memory_order);
}

static std::string_view const npy_magic_string = "\x93NUMPY";

void cnpypp::parse_npy_header(std::istream& fs, std::vector<size_t>& word_sizes,
                              std::vector<char>& data_types,
                              std::vector<std::string>& labels,
                              std::vector<size_t>& shape,
                              cnpypp::MemoryOrder& memory_order) {
  std::array<std::istream::char_type, 10> buffer;
  fs.read(buffer.data(), 10);

  if (!std::equal(npy_magic_string.begin(), npy_magic_string.end(),
                  buffer.cbegin())) {
    throw std::runtime_error("parse_npy_header: NPY magic string not found");
  }

  uint8_t const major_version = buffer[6];
  uint8_t const minor_version = buffer[7];

  if (major_version != 1 || minor_version != 0) {
    throw std::runtime_error(
        "parse_npy_header: NPY format version not supported");
  }

  uint16_t const header_len =
      boost::endian::endian_load<boost::uint16_t, 2,
                                 boost::endian::order::little>(
          reinterpret_cast<unsigned char*>(&buffer[8]));

  auto const header_buffer =
      std::make_unique<std::istream::char_type[]>(header_len);
  fs.read(header_buffer.get(), header_len);

  parse_npy_dict(
      cnpypp::span<std::istream::char_type>(header_buffer.get(), header_len),
      word_sizes, data_types, labels, shape, memory_order);
}

void cnpypp::parse_npy_dict(cnpypp::span<std::istream::char_type const> buffer,
                            std::vector<size_t>& word_sizes,
                            std::vector<char>& data_types,
                            std::vector<std::string>& labels,
                            std::vector<size_t>& shape,
                            cnpypp::MemoryOrder& memory_order) {
  if (buffer.back() != '\n') {
    throw std::runtime_error("invalid header: missing terminating newline");
  } else if (buffer.front() != '{') {
    throw std::runtime_error("invalid header: malformed dictionary");
  }

  std::string_view const dict{buffer.data(), buffer.size()};

  if (std::cmatch matches;
      !std::regex_search(dict.begin(), dict.end(), matches,
                         std::regex{"'fortran_order': (True|False)"})) {
    throw std::runtime_error("invalid header: missing 'fortran_order'");
  } else {
    memory_order = (matches[1].str() == "True") ? cnpypp::MemoryOrder::Fortran
                                                : cnpypp::MemoryOrder::C;
  }

  word_sizes.clear();
  data_types.clear();
  labels.clear();
  shape.clear();

  // read & fill shape

  std::string_view const sh = "'shape': (";

  if (auto const pos_start_shape = dict.find(sh);
      pos_start_shape == std::string_view::npos) {
    throw std::runtime_error("invalid header: missing 'shape'");
  } else {
    if (auto const pos_end_shape = dict.find(')', pos_start_shape);
        pos_end_shape == std::string_view::npos) {
      throw std::runtime_error("invalid header: malformed dictionary");
    } else {
      std::regex digit_re{"\\d+"};
      auto dims_begin =
          std::cregex_iterator(dict.begin() + pos_start_shape,
                               dict.begin() + pos_end_shape, digit_re);
      auto dims_end = std::cregex_iterator();

      for (std::cregex_iterator it = dims_begin; it != dims_end; ++it) {
        shape.push_back(std::stoi(it->str()));
      }
    }
  }

  std::string_view const desc = "'descr': ";
  if (auto const pos_start_desc = dict.find(desc);
      pos_start_desc == std::string_view::npos) {
    throw std::runtime_error("invalid header: missing 'descr'");
  } else {
    if (auto const c = dict[pos_start_desc + desc.size()]; c == '\'') {
      // simple type

      if (std::cmatch matches;
          !std::regex_search(dict.begin() + pos_start_desc, dict.end(), matches,
                             std::regex{"'([<>\\|])([a-zA-z])(\\d+)'"})) {
        throw std::runtime_error(
            "parse_npy_header: could not parse data type descriptor");
      } else if (matches[1].str() == ">") {
        throw std::runtime_error("parse_npy_header: data stored in big-endian "
                                 "format (not supported)");
      } else {
        data_types.push_back(*(matches[2].first));
        word_sizes.push_back(std::stoi(matches[3].str()));
      }
    } else if (c == '[') {
      // structured type / tuple

      if (auto const pos_end_list =
              dict.find(']', pos_start_desc + desc.size());
          pos_end_list == std::string_view::npos) {
        throw std::runtime_error("invalid header: malformed list in 'descr'");
      } else {
        auto tuples_begin = std::cregex_iterator(
            dict.begin() + pos_start_desc + desc.size(),
            dict.begin() + pos_end_list, dtype_tuple_regex);
        auto tuples_end = std::cregex_iterator();

        for (std::cregex_iterator it = tuples_begin; it != tuples_end; ++it) {
          auto&& match = *it;
          labels.emplace_back(match[1].str());

          if (match[2].str() == ">") {
            throw std::runtime_error("parse_npy_header: data stored in "
                                     "big-endian format (not supported)");
          }

          data_types.push_back(*(match[3].first));
          word_sizes.push_back(std::stoi(match[4].str()));
        }
      }
    } else {
      throw std::runtime_error("invalid header: malformed 'descr'");
    }
  }
}

#ifndef NO_LIBZIP
cnpypp::NpyArray load_npy(zip_t* archive, zip_int64_t index) {
  zip_stat_t fileinfo;
  zip_stat_index(archive, index, ZIP_FL_ENC_RAW, &fileinfo);
  if (!(fileinfo.valid & ZIP_STAT_SIZE)) {
    throw std::runtime_error{"libcnpy++: zip_stat() failed, size invalid"};
  }
  if (!(fileinfo.valid & ZIP_STAT_COMP_METHOD)) {
    throw std::runtime_error{
        "libcnpy++: zip_stat() failed, comp_method invalid"};
  }

  zip_file_t* file = zip_fopen_index(archive, index, ZIP_FL_ENC_RAW);

  size_t const max_header_size = 0x10000 + 10; // for npy version 1
  auto header_buffer = std::make_unique<char[]>(max_header_size);

  auto const read_bytes = zip_fread(file, header_buffer.get(), max_header_size);
  if (read_bytes == -1) {
    zip_fclose(file);
    throw std::runtime_error{"libcnpy++: zip_fread() failed"};
  }

  std::vector<size_t> shape, word_sizes;
  std::vector<char> data_types; // filled but not used
  std::vector<std::string> labels;
  MemoryOrder memory_order;
  parse_npy_header(header_buffer.get(), word_sizes, data_types, labels, shape,
                   memory_order);

  auto const num_vals = std::accumulate(shape.begin(), shape.end(), size_t{1},
                                        std::multiplies<size_t>());
  auto const total_value_size = std::accumulate(
      word_sizes.begin(), word_sizes.end(), size_t{0}, std::plus<size_t>());
  auto const num_bytes = total_value_size * num_vals;

  auto buffer = std::make_unique<InMemoryBuffer>(num_bytes);

  zip_int64_t const offset = fileinfo.size - num_bytes;
  if (fileinfo.size <= max_header_size) {
    // file fits completely into buffer, meaning we have
    // read it completely already

    std::copy_n(&header_buffer[offset], num_bytes,
                reinterpret_cast<char*>(buffer->data()));
  } else { // we have only parts, seek back to beginning of data and read all
           // from there
    // works only if zip_source is seekable itself
    // change this to zip_file_is_seekable() when libzip 1.9.0 is in use
    // everywhere
    if (fileinfo.comp_method == ZIP_CM_STORE) {
      if (zip_fseek(file, offset, SEEK_SET) != 0) {
        zip_fclose(file);
        throw std::runtime_error{"libcnpy++: zip_seek() failed"};
      }
    } else { // compressed data, seek impossible
      // reopen file
      zip_fclose(file);

      file = zip_fopen_index(archive, index, ZIP_FL_ENC_RAW);
      auto tmp = std::make_unique<char[]>(offset);
      if (zip_fread(file, tmp.get(), offset) != offset) {
        zip_fclose(file);
        throw std::runtime_error{"libcnpy++: zip_fread() failed"};
      }
    }

    // now read data
    if (zip_fread(file, buffer->data(), num_bytes) !=
        static_cast<zip_int64_t>(num_bytes)) {
      zip_fclose(file);
      throw std::runtime_error{"libcnpy++: zip_fread() failed"};
    }
  }

  zip_fclose(file);
  return NpyArray{std::move(shape), std::move(word_sizes), std::move(labels),
                  memory_order, std::move(buffer)};
}
#endif

#ifndef NO_LIBZIP
cnpypp::npz_t cnpypp::npz_load(std::string const& fname) {
  int errcode = 0;
  zip_t* const archive = zip_open(fname.c_str(), ZIP_RDONLY, &errcode);
  if (!archive) {
    zip_error_t err;
    zip_error_init_with_code(&err, errcode);
    throw std::runtime_error(zip_error_strerror(&err));
  }

  cnpypp::npz_t arrays;
  std::vector<std::string> names{};
  zip_int64_t const num_files = zip_get_num_entries(archive, ZIP_FL_UNCHANGED);
  for (zip_int64_t i = 0; i < num_files; ++i) {
    char const* const filename = zip_get_name(archive, i, ZIP_FL_ENC_RAW);
    std::string_view const filename_view{filename};
    auto const extension =
        filename_view.substr(filename_view.size() - 4, filename_view.size());

    if (extension != ".npy") {
      std::cerr << "file containes file not ending with \".npy\" (\""
                << filename << "\"); skipping" << std::endl;
      continue;
    }

    auto const stripped_name =
        filename_view.substr(0, filename_view.size() - 4);

    // BREAK HERE INTO SUBFUNCION
    auto array = load_npy(archive, i);
    arrays.emplace(std::string{stripped_name}, std::move(array));
  }

  zip_close(archive);
  return arrays;
}
#endif

#ifndef NO_LIBZIP
cnpypp::NpyArray cnpypp::npz_load(std::string const& fname,
                                  std::string const& varname) {
  int errcode = 0;
  zip_t* const archive = zip_open(fname.c_str(), ZIP_RDONLY, &errcode);
  if (!archive) {
    zip_error_t err;
    zip_error_init_with_code(&err, errcode);
    throw std::runtime_error(zip_error_strerror(&err));
  }

  std::string const full_filename = varname + ".npy";
  zip_int64_t const index =
      zip_name_locate(archive, full_filename.c_str(), ZIP_FL_ENC_RAW);
  if (index == -1) {
    // if we get here, we haven't found the variable in the file
    std::stringstream ss;
    ss << "npz_load: Variable name " << std::quoted(varname) << " not found in "
       << std::quoted(fname);
    throw std::runtime_error{ss.str().c_str()};
  }

  auto array = load_npy(archive, index);
  zip_close(archive);
  return array;
}
#endif

cnpypp::NpyArray cnpypp::npy_load(std::string const& fname,
                                  bool memory_mapped) {
  std::ifstream fs{fname, std::ios::binary};

  if (!fs)
    throw std::runtime_error("npy_load: Unable to open file " + fname);

  std::vector<size_t> word_sizes, shape;
  std::vector<char> data_types;
  std::vector<std::string> labels;
  cnpypp::MemoryOrder memory_order;

  cnpypp::parse_npy_header(fs, word_sizes, data_types, labels, shape,
                           memory_order);

  auto const num_vals = std::accumulate(shape.begin(), shape.end(), size_t{1},
                                        std::multiplies<size_t>());
  auto const total_value_size = std::accumulate(
      word_sizes.begin(), word_sizes.end(), size_t{0}, std::plus<size_t>());
  auto const num_bytes = total_value_size * num_vals;

  std::unique_ptr<Buffer> buffer;

  if (!memory_mapped) {
    buffer = std::make_unique<InMemoryBuffer>(num_bytes);
    fs.read(reinterpret_cast<char*>(buffer->data()), num_bytes);
  } else {
    buffer = std::make_unique<MemoryMappedBuffer>(fname, fs.tellg(), num_bytes);
  }

  return cnpypp::NpyArray{std::move(shape), std::move(word_sizes),
                          std::move(labels), memory_order, std::move(buffer)};
}

std::vector<char>
cnpypp::create_npy_header(cnpypp::span<size_t const> const shape,
                          cnpypp::span<std::string_view const> labels,
                          cnpypp::span<char const> dtypes,
                          cnpypp::span<size_t const> sizes,
                          MemoryOrder memory_order) {
  std::vector<char> dict;
  append(dict, "{'descr': [");

  if (labels.size() != dtypes.size() || dtypes.size() != sizes.size() ||
      sizes.size() != labels.size()) {
    throw std::runtime_error(
        "create_npy_header: sizes of argument vectors not equal");
  }

  for (size_t i = 0; i < dtypes.size(); ++i) {
    auto const& label = labels[i];
    auto const& dtype = dtypes[i];
    auto const& size = sizes[i];

    append(dict, "('");
    append(dict, label);
    append(dict, "', '");
    dict.push_back(BigEndianTest());
    dict.push_back(dtype);
    append(dict, std::to_string(size));
    append(dict, "')");

    if (i + 1 != dtypes.size()) {
      append(dict, ", ");
    }
  }

  if (dtypes.size() == 1) {
    dict.push_back(',');
  }

  append(dict, "], 'fortran_order': ");
  append(dict, (memory_order == MemoryOrder::C) ? "False" : "True");
  append(dict, ", 'shape': (");
  append(dict, std::to_string(shape[0]));
  for (size_t i = 1; i < shape.size(); i++) {
    append(dict, ", ");
    append(dict, std::to_string(shape[i]));
  }
  if (shape.size() == 1) {
    append(dict, ",");
  }
  append(dict, "), }");

  // pad with spaces so that preamble+dict is modulo 16 bytes. preamble is 10
  // bytes. dict needs to end with \n
  int const remainder = 16 - (10 + dict.size()) % 16;
  dict.insert(dict.end(), remainder, ' ');
  dict.back() = '\n';

  std::vector<char> header;
  header += (char)0x93;
  append(header, "NUMPY");
  header += (char)0x01; // major version of numpy format
  header += (char)0x00; // minor version of numpy format
  header += (uint16_t)dict.size();
  header.insert(header.end(), dict.begin(), dict.end());

  return header;
}

std::vector<char>
cnpypp::create_npy_header(cnpypp::span<size_t const> const shape, char dtype,
                          int wordsize, MemoryOrder memory_order) {
  std::vector<char> dict;
  append(dict, "{'descr': '");
  dict += BigEndianTest();
  dict.push_back(dtype);
  append(dict, std::to_string(wordsize));
  append(dict, "', 'fortran_order': ");
  append(dict, (memory_order == MemoryOrder::C) ? "False" : "True");
  append(dict, ", 'shape': (");
  append(dict, std::to_string(shape[0]));
  for (size_t i = 1; i < shape.size(); i++) {
    append(dict, ", ");
    append(dict, std::to_string(shape[i]));
  }
  if (shape.size() == 1) {
    append(dict, ",");
  }
  append(dict, "), }");

  // pad with spaces so that preamble+dict is modulo 16 bytes. preamble is 10
  // bytes. dict needs to end with \n
  int const remainder = 16 - (10 + dict.size()) % 16;
  dict.insert(dict.end(), remainder, ' ');
  dict.back() = '\n';

  std::vector<char> header;
  header += (char)0x93;
  append(header, "NUMPY");
  header += (char)0x01; // major version of numpy format
  header += (char)0x00; // minor version of numpy format
  header += (uint16_t)dict.size();
  header.insert(header.end(), dict.begin(), dict.end());

  return header;
}

// for C compatibility
int cnpypp_npy_save(char const* fname, cnpypp_data_type dtype,
                    void const* start, size_t const* shape, size_t rank,
                    char const* mode, enum cnpypp_memory_order memory_order) {
  int retval = 0;
  try {
    std::string const filename = fname;
    std::vector<size_t> shapeVec{};
    shapeVec.reserve(rank);
    std::copy_n(shape, rank, std::back_inserter(shapeVec));

    switch (dtype) {
    case cnpypp_int8:
      cnpypp::npy_save(filename, reinterpret_cast<int8_t const*>(start),
                       shapeVec, mode,
                       static_cast<cnpypp::MemoryOrder>(memory_order));
      break;
    case cnpypp_uint8:
      cnpypp::npy_save(filename, reinterpret_cast<uint8_t const*>(start),
                       shapeVec, mode,
                       static_cast<cnpypp::MemoryOrder>(memory_order));
      break;
    case cnpypp_int16:
      cnpypp::npy_save(filename, reinterpret_cast<int16_t const*>(start),
                       shapeVec, mode,
                       static_cast<cnpypp::MemoryOrder>(memory_order));
      break;
    case cnpypp_uint16:
      cnpypp::npy_save(filename, reinterpret_cast<uint16_t const*>(start),
                       shapeVec, mode,
                       static_cast<cnpypp::MemoryOrder>(memory_order));
      break;
    case cnpypp_int32:
      cnpypp::npy_save(filename, reinterpret_cast<int32_t const*>(start),
                       shapeVec, mode,
                       static_cast<cnpypp::MemoryOrder>(memory_order));
      break;
    case cnpypp_uint32:
      cnpypp::npy_save(filename, reinterpret_cast<uint32_t const*>(start),
                       shapeVec, mode,
                       static_cast<cnpypp::MemoryOrder>(memory_order));
      break;
    case cnpypp_int64:
      cnpypp::npy_save(filename, reinterpret_cast<int64_t const*>(start),
                       shapeVec, mode,
                       static_cast<cnpypp::MemoryOrder>(memory_order));
      break;
    case cnpypp_uint64:
      cnpypp::npy_save(filename, reinterpret_cast<uint64_t const*>(start),
                       shapeVec, mode,
                       static_cast<cnpypp::MemoryOrder>(memory_order));
      break;
    case cnpypp_float32:
      cnpypp::npy_save(filename, reinterpret_cast<float const*>(start),
                       shapeVec, mode,
                       static_cast<cnpypp::MemoryOrder>(memory_order));
      break;
    case cnpypp_float64:
      cnpypp::npy_save(filename, reinterpret_cast<double const*>(start),
                       shapeVec, mode,
                       static_cast<cnpypp::MemoryOrder>(memory_order));
      break;
    case cnpypp_float128:
      cnpypp::npy_save(filename, reinterpret_cast<long double const*>(start),
                       shapeVec, mode,
                       static_cast<cnpypp::MemoryOrder>(memory_order));
      break;
    default:
      std::cerr << "npy_save: unknown type argument" << std::endl;
    }
  } catch (...) {
    retval = -1;
  }

  return retval;
}

#ifndef NO_LIBZIP
int cnpypp_npz_save(char const* zipname, char const* filename,
                    enum cnpypp_data_type dtype, void const* data,
                    size_t const* shape, size_t rank, char const* mode,
                    enum cnpypp_memory_order memory_order) {
  int retval = 0;
  try {
    std::vector<size_t> shapeVec{};
    shapeVec.reserve(rank);
    std::copy_n(shape, rank, std::back_inserter(shapeVec));

    switch (dtype) {
    case cnpypp_int8:
      cnpypp::npz_save(zipname, filename, reinterpret_cast<int8_t const*>(data),
                       shapeVec, mode,
                       static_cast<cnpypp::MemoryOrder>(memory_order));
      break;
    case cnpypp_uint8:
      cnpypp::npz_save(zipname, filename,
                       reinterpret_cast<uint8_t const*>(data), shapeVec, mode,
                       static_cast<cnpypp::MemoryOrder>(memory_order));
      break;
    case cnpypp_int16:
      cnpypp::npz_save(zipname, filename,
                       reinterpret_cast<int16_t const*>(data), shapeVec, mode,
                       static_cast<cnpypp::MemoryOrder>(memory_order));
      break;
    case cnpypp_uint16:
      cnpypp::npz_save(zipname, filename,
                       reinterpret_cast<uint16_t const*>(data), shapeVec, mode,
                       static_cast<cnpypp::MemoryOrder>(memory_order));
      break;
    case cnpypp_int32:
      cnpypp::npz_save(zipname, filename,
                       reinterpret_cast<int32_t const*>(data), shapeVec, mode,
                       static_cast<cnpypp::MemoryOrder>(memory_order));
      break;
    case cnpypp_uint32:
      cnpypp::npz_save(zipname, filename,
                       reinterpret_cast<uint32_t const*>(data), shapeVec, mode,
                       static_cast<cnpypp::MemoryOrder>(memory_order));
      break;
    case cnpypp_int64:
      cnpypp::npz_save(zipname, filename,
                       reinterpret_cast<int64_t const*>(data), shapeVec, mode,
                       static_cast<cnpypp::MemoryOrder>(memory_order));
      break;
    case cnpypp_uint64:
      cnpypp::npz_save(zipname, filename,
                       reinterpret_cast<uint64_t const*>(data), shapeVec, mode,
                       static_cast<cnpypp::MemoryOrder>(memory_order));
      break;
    case cnpypp_float32:
      cnpypp::npz_save(zipname, filename, reinterpret_cast<float const*>(data),
                       shapeVec, mode,
                       static_cast<cnpypp::MemoryOrder>(memory_order));
      break;
    case cnpypp_float64:
      cnpypp::npz_save(zipname, filename, reinterpret_cast<double const*>(data),
                       shapeVec, mode,
                       static_cast<cnpypp::MemoryOrder>(memory_order));
      break;
    case cnpypp_float128:
      cnpypp::npz_save(zipname, filename,
                       reinterpret_cast<long double const*>(data), shapeVec,
                       mode, static_cast<cnpypp::MemoryOrder>(memory_order));
      break;
    default:
      std::cerr << "npz_save: unknown type argument" << std::endl;
    }
  } catch (...) {
    retval = -1;
  }

  return retval;
}
#endif

cnpypp_npyarray_handle* cnpypp_load_npyarray(char const* fname) {
  cnpypp::NpyArray* arr = nullptr;

  try {
    arr = new cnpypp::NpyArray(cnpypp::npy_load(fname));
  } catch (...) {
  }

  return reinterpret_cast<cnpypp_npyarray_handle*>(arr);
}

void cnpypp_free_npyarray(cnpypp_npyarray_handle* npyarr) {
  delete reinterpret_cast<cnpypp::NpyArray*>(npyarr);
}

void const* cnpypp_npyarray_get_data(cnpypp_npyarray_handle const* npyarr) {
  auto const& array = *reinterpret_cast<cnpypp::NpyArray const*>(npyarr);
  return array.data<void>();
}

size_t const* cnpypp_npyarray_get_shape(cnpypp_npyarray_handle const* npyarr,
                                        size_t* rank) {
  auto const& array = *reinterpret_cast<cnpypp::NpyArray const*>(npyarr);

  if (rank != nullptr) {
    *rank = array.shape.size();
  }

  return array.shape.data();
}

enum cnpypp_memory_order
cnpypp_npyarray_get_memory_order(cnpypp_npyarray_handle const* npyarr) {
  auto const& array = *reinterpret_cast<cnpypp::NpyArray const*>(npyarr);
  return (array.memory_order == cnpypp::MemoryOrder::Fortran)
             ? cnpypp_memory_order_fortran
             : cnpypp_memory_order_c;
}

#ifndef NO_LIBZIP
zip_int64_t cnpypp::detail::npzwrite_source_callback(void* userdata, void* data,
                                                     zip_uint64_t length,
                                                     zip_source_cmd_t cmd) {
  auto* const parameters =
      reinterpret_cast<cnpypp::detail::additional_parameters*>(userdata);
  char* data_char = reinterpret_cast<char*>(data);

  switch (cmd) {
  case ZIP_SOURCE_OPEN:
    return 0;

  case ZIP_SOURCE_READ: {
    size_t bytes_written = 0;
    if (parameters->header_bytes_remaining) {
      auto const& npyheader = parameters->npyheader;
      auto const tbw = std::min(length, npyheader.size());
      data_char = std::copy_n(
          std::next(npyheader.cbegin(),
                    npyheader.size() - parameters->header_bytes_remaining),
          tbw, data_char);
      parameters->header_bytes_remaining -= tbw;
      bytes_written = tbw;
    }
    if (parameters->header_bytes_remaining == 0) {
      auto const buffer_tbw =
          parameters->buffer_size - parameters->bytes_buffer_written;
      auto* const e =
          std::copy_n(&parameters->buffer[parameters->bytes_buffer_written],
                      std::min(buffer_tbw, length - bytes_written), data_char);
      auto const bytes_written_from_buffer = std::distance(data_char, e);
      parameters->bytes_buffer_written += bytes_written_from_buffer;
      bytes_written += bytes_written_from_buffer;
      data_char = e;

      if (parameters->bytes_buffer_written == parameters->buffer_size) {
        // buffer copied completely, treat as emtpy again
        parameters->bytes_buffer_written = 0;
        parameters->buffer_size = 0;

        bytes_written += parameters->func(
            cnpypp::span<char>(data_char, length - bytes_written), parameters);
      }
    }

    return bytes_written;
  }

  case ZIP_SOURCE_CLOSE:
    return 0;

  case ZIP_SOURCE_STAT: {
    zip_stat_t* stat = reinterpret_cast<zip_stat_t*>(data);
    zip_stat_init(stat);
    return sizeof(zip_stat_t);
  }

  case ZIP_SOURCE_ERROR:
    return 0;

  case ZIP_SOURCE_SUPPORTS:
    return zip_source_make_command_bitmap(
        ZIP_SOURCE_OPEN, ZIP_SOURCE_READ, ZIP_SOURCE_CLOSE, ZIP_SOURCE_STAT,
        ZIP_SOURCE_ERROR, ZIP_SOURCE_FREE, -1);

  case ZIP_SOURCE_FREE:
    return 0;

  case ZIP_SOURCE_SEEK:
    return 0;

  case ZIP_SOURCE_TELL:
    return 0;

  default:
    std::cerr << "libcnpy++: should not happen: " << cmd << std::endl;
    return 0;
  }
}
#endif

#ifndef NO_LIBZIP
std::tuple<size_t, zip_t*>
cnpypp::prepare_npz(std::string const& zipname,
                    cnpypp::span<size_t const> const shape,
                    std::string_view mode) {
  int errcode = 0;
  zip_t* const archive = zip_open(
      zipname.c_str(), (mode == "w") ? (ZIP_CREATE | ZIP_TRUNCATE) : ZIP_CREATE,
      &errcode);
  if (!archive) {
    zip_error_t err;
    zip_error_init_with_code(&err, errcode);
    throw std::runtime_error(zip_error_strerror(&err));
  }

  size_t const nels =
      std::accumulate(shape.begin(), shape.end(), 1, std::multiplies<size_t>());

  return std::tuple{nels, archive};
}
#endif

#ifndef NO_LIBZIP
void cnpypp::finalize_npz(zip_t* archive, std::string fname,
                          detail::additional_parameters& parameters) {
  zip_source_t* source =
      zip_source_function(archive, detail::npzwrite_source_callback,
                          reinterpret_cast<void*>(&parameters));

  fname += ".npy";
  zip_file_add(archive, fname.c_str(), source,
               ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
  zip_close(archive);
}
#endif
