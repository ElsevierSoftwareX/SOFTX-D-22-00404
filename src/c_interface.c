// Copyright (C) 2022 Maximilian Reininghaus
// Released under MIT License
// license available in LICENSE file, or at
// http://www.opensource.org/licenses/mit-license.php

#include "cnpy++.h"

int cnpypp_npy_save_1d(char const* fname, enum cnpypp_data_type dtype,
                       void const* start, size_t const num_elem,
                       char const* mode) {
  size_t const shape[] = {num_elem};
  return cnpypp_npy_save(fname, dtype, start, shape, 1, mode,
                         cnpypp_memory_order_c);
}

#ifndef NO_LIBZIP
int cnpypp_npz_save_1d(char const* zipname, char const* fname,
                       enum cnpypp_data_type dtype, void const* data,
                       size_t num_elem, char const* mode) {
  size_t const shape[] = {num_elem};
  return cnpypp_npz_save(zipname, fname, dtype, data, shape, 1, mode,
                         cnpypp_memory_order_c);
}
#endif
