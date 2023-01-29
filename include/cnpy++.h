// Copyright (C) 2022 Maximilian Reininghaus
// Released under MIT License
// license available in LICENSE file, or at
// http://www.opensource.org/licenses/mit-license.php

#ifndef _CNPYPP_H_
#define _CNPYPP_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum cnpypp_memory_order {
  cnpypp_memory_order_fortran = 0,
  cnpypp_memory_order_c = 1
};
enum cnpypp_data_type {
  cnpypp_int8 = 0,
  cnpypp_uint8 = 1,
  cnpypp_int16 = 2,
  cnpypp_uint16 = 3,
  cnpypp_int32 = 4,
  cnpypp_uint32 = 5,
  cnpypp_int64 = 6,
  cnpypp_uint64 = 7,
  cnpypp_float32 = 8,
  cnpypp_float64 = 9,
  cnpypp_float128 = 10
};

uint32_t _crc32(unsigned long int, uint8_t const*,
                unsigned int); // calls crc32() from zlib

struct cnpypp_npyarray_handle;

int cnpypp_npy_save(char const* fname, enum cnpypp_data_type, void const* start,
                    size_t const* shape, size_t rank, char const* mode,
                    enum cnpypp_memory_order);
int cnpypp_npy_save_1d(char const* fname, enum cnpypp_data_type,
                       void const* start, size_t const num_elem,
                       char const* mode);

#ifndef NO_LIBZIP
int cnpypp_npz_save(char const* zipname, char const* fname,
                    enum cnpypp_data_type dtype, void const* data,
                    size_t const* shape, size_t rank, char const* mode,
                    enum cnpypp_memory_order);
#endif

#ifndef NO_LIBZIP
int cnpypp_npz_save_1d(char const* zipname, char const* fname,
                       enum cnpypp_data_type dtype, void const* data,
                       size_t num_elem, char const* mode);
#endif

struct cnpypp_npyarray_handle* cnpypp_load_npyarray(char const* fname);

void cnpypp_free_npyarray(struct cnpypp_npyarray_handle* npyarr);

void const*
cnpypp_npyarray_get_data(struct cnpypp_npyarray_handle const* npyarr);

size_t const*
cnpypp_npyarray_get_shape(struct cnpypp_npyarray_handle const* npyarr,
                          size_t* rank);

enum cnpypp_memory_order
cnpypp_npyarray_get_memory_order(struct cnpypp_npyarray_handle const* npyarr);

#ifdef __cplusplus
}
#endif
#endif // _CNPYPP_H
