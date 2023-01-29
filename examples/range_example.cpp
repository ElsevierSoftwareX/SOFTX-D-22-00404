#include <cstdlib>
#include <ranges>

#include <cnpy++.hpp>

int main() {
  auto const r = std::ranges::iota_view{1, 11};

  cnpypp::npy_save("range_data.npy", r.begin(), {r.size()}, "w",
                   cnpypp::MemoryOrder::Fortran);

  return EXIT_SUCCESS;
}
