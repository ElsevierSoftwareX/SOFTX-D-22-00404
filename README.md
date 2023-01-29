# cnpy++

`cnpy++` is a C++17 library that allows to read and write NumPy data files (.npy and .npz).
It is designed in a way to integrate well into the modern C++ ecosystem and it provides features not available
in any similar C++/npy library.

Additionally, C bindings are provided for a limited, but most useful subset of the C++ functionality.

If you find cnpy++ useful for your research, please cite
M. Reininghaus, *cnpy++: A C++17 library for reading and writing .npy/.npz files*, SoftwareX **21**, 101324 (2023),
doi:[10.1016/j.softx.2023.101324](https://doi.org/10.1016/j.softx.2023.101324).

## Motivation
NumPy data files are a binary data format for serializing multi-dimenstional arrays.
Due to its simplicity, it is a convenient format for scientific computing to be used not only from within Python. 

## Building cnpy++

### Requirements

* a C++17-compatible compiler (gcc and clang have been tested succesfully)
* libzip-devel (required by default but optional)
* boost (at least 1.74; if using >=1.78, you can use `boost::span` (see below)
* optional: pre-installed versions of either Microsoft GSL or gsl-lite

### Instructions

cnpy++ is built via cmake. After downloading the code (say, into `/path/to/cnpy++`), create
a build directory (say, `/path/to/cnpy++-build`). From within that directory, call
`cmake -DCNPYPP_SPAN_IMPL=<...> /path/to/cnpy++`. cnpy++ needs an implementation of the
`span<T>` type. This is available in Microsoft GSL, gsl-lite, boost since version 1.78 and in the STL
if compiling with C++20 support. To select which implementation you want to use, set the CMake
cache variable `CNPYPP_SPAN_IMPL` to either `MS_GSL`, `GSL_LITE` or `BOOST`. If set to `MS_GSL`
or `GSL_LITE`, the corresponding library will be downloaded by cmake (using git) if not found already
on the system.

Another option is `CNPYPP_USE_LIBZIP`, which by default is `ON`, but can be set to `OFF`. In that case,
all functionality requiring libzip is disabled, i.e. no support for reading/writing NPZ archives.

After the cmake invocation returned successfully, call `make cnpy++` to compile the library,
or just `make` to compile the examples, too.

## Usage

`cnpy++` consists of a header part, `cnpy++.hpp`, which needs to be included in your source file,
and a compiled part, which can either be a shared or a static library.


### Manual
If you build `cnpy++` with `cmake -DBUILD_SHARED_LIBS=ON`, you obtain a shared library, `libcnpy++.so`,
that you need to link to your executable. On Unix systems with g++ or clang++ compilers, you can run

```bash
g++ -o my_executable my_executable.cpp -L/path/to/install/dir -lcnpy++
```

This works analogously if you use the C bindings with a C compiler.

In case of a static `cnpy++` build, you need to provide the path to `libcnpy++.a`:
```bash
g++ -o my_executable my_executable.cpp /path/to/libcnpy++.a
```

### cmake-assisted compilation

You can include cnpy++ in your own cmake-based project without having to install it first e.g. by using
cmake's `FetchContent`. Add the following snippet to your `CMakeLists.txt`.

```
include(FetchContent)
FetchContent_Declare(cnpy++
    GIT_REPOSITORY "https://gitlab.iap.kit.edu/mreininghaus/cnpypp.git"
    GIT_SHALLOW True
)
FetchContent_MakeAvailable(cnpy++)
```

## API documentation
All functions, data structures, etc. are placed inside the `cnpypp` namespace. The type alias
`cnpypp::span<T>` is an alias to the implementation of `span<T>` as explained above.

### Writing data to .npy

To write data into a NPY file, use one of the overloads of `npy_save()`:

```c++
template <typename TConstInputIterator>
void npy_save(std::string const& fname, TConstInputIterator start,
              std::initializer_list<size_t> const shape,
              std::string_view mode = "w",
              MemoryOrder memory_order = MemoryOrder::C);
```
This function writes data from an interator `start` into the file indicated by the filename `fname`.  
The `shape` tuple describes the dimensions of the array, with the total number of elements given
by the product of all entries of `shape`.  
The `mode` parameter can be either "w" or "a". With "w", a potentially existing file is overwritten.
With "a", data are appended if the file already exists. In that case, the data shape has to match the
shape in the existing file in all entries except the first.  
The `memory_order` parameter indicates the memory order and can be either `MemoryOrder::C`, `MemoryOrder::Fortran`,
or their aliases `MemoryOrder::RowMajor` and `MemoryOrder::ColumnMajor`.

```c++
template <typename TConstInputIterator>
void npy_save(std::string const& fname, TConstInputIterator start,
              cnpypp::span<size_t const> const shape, std::string_view mode = "w",
              MemoryOrder memory_order = MemoryOrder::C)
```
Use this overload if `shape` is an array, vector or alike.

```c++
template <typename TForwardIterator>
void npy_save(std::string const& fname, TForwardIterator first,
              TForwardIterator last, std::string_view mode = "w")
```
This is an overload provided for convenience when the data to be written are available
via a pair of multiple-pass forward iterators. They are assumed to be one-dimensional.

```c++
template <typename T>
void npy_save(std::string const& fname, cnpypp::span<T const> data,
              std::string_view mode = "w")
```
This overload is provided for convenience when your data are in contiguous memory.

```c++
template <typename TTupleIterator>
void npy_save(std::string const& fname,
              std::vector<std::string_view> const& labels, TTupleIterator first,
              cnpypp::span<size_t const> const shape, std::string_view mode = "w",
              MemoryOrder memory_order = MemoryOrder::C)
```
With this overload, it is possible to write labeled "structured arrays" (in the terminology of NumPy).
The iterator must yield `std::tuple`s of values (e.g. `std::tuple<int, float>`) or references (e.g.
`std::tuple<int const&, float const&>`). The `label` vector must have a size equal to the number of
elements in the tuple. A potential use-case is to use a `zip_iterator` from the [range-v3](https://github.com/ericniebler/range-v3)
library. This way, you can serialize data in a structure-of-arrays layout as array-of-structures.
An example of this usage is provided in `examples/range_zip_example.cpp`.

### Writing data to .npz
NPZ files are just zip archives containing one or more NPY files.

```c++
template <typename TConstInputIterator>
void npz_save(std::string const& zipname, std::string fname,
              TConstInputIterator start,
              std::initializer_list<size_t const> shape,
              std::string_view mode = "w",
              MemoryOrder memory_order = MemoryOrder::C)
              
template <typename TConstInputIterator>
void npz_save(std::string const& zipname, std::string fname,
              TConstInputIterator start, cnpypp::span<size_t const> const shape,
              std::string_view mode = "w",
              MemoryOrder memory_order = MemoryOrder::C)

template <typename TTupleIterator>
void npz_save(std::string const& zipname, std::string const& fname,
              std::vector<std::string_view> const& labels, TTupleIterator first,
              cnpypp::span<size_t const> const shape,
              std::string_view mode = "w",
              MemoryOrder memory_order = MemoryOrder::C)
```
The first parameter, `zipname`, refers to the filename of the NPZ archive, while `fname` refers to
the filename inside the archive (excluding the "`.npy`" extension).
`shape` and `memory_order` are equal to their counterparts in `npy_save()`.
If `mode` is equal to `"w"`, an already existing NPZ file is overwritten. If equal to `"a"`, another
array is added to the archive. Note that it is not possible to extend an already existing array
in the same way as it is possible with `npy_save()`.

### Reading data
```c++
NpyArray npy_load(std::string const& fname, bool memory_mapped = false)
```
reads data from a file with filename `fname`. If `memory_mapped` is false (default), the whole file content is copied into memory.
If true, the file gets memory-mapped, meaning its content can be read via pointers just like normal memory. The OS takes care to
read the requested data from disk when necessary. This is useful when the file is larger than the free memory available.
The address space available in 64 bit architechtures should be sufficient to map even the largest files.
The return type, `NpyArray` contains the raw data as well as a number of methods to query its metadata and convenience functionality
like iterators.

```c++
NpyArray npz_load(std::string const& fname, std::string const& varname)
```
reads the array named `varname` from a NPZ archive with filename `fname` into memory (files with data larger than available memory are currently not supported).
The return type, `NpyArray` contains the raw data as well as a number of methods to query its metadata and convenience functionality
like iterators.

```c++
std::map<std::string, NpyArray> npz_load(std::string const& fname)
```
reads all arrays from a NPZ archive with filename `fname` into memory (files with data larger than available memory are currently not supported).
The invividual arrays can be accessed from the returned map with their name as key.

The `NpyArray` class provides the following attributes:
```c++
std::vector<size_t> const NpyArray::shape
```
The shape vector.

```c++
MemoryOrder NpyArray::memory_order
```
The memory order.

```c++
std::vector<std::string> const NpyArray::labels
```
A vector of the labels if the array is structured. In case of a plain array without labels,
this vector is empty.

```c++
std::vector<size_t> const NpyArray::word_sizes
```
The byte sizes (e.g. 4 for `uint32_t`) of the fields of a structured array. In case of a plain array, this vector has only one element.

```c++
template <typename T>
T* NpyArray::begin<T>()
```
returns a pointer to the first element, interpreted as type `T`. Note that it makes no sense to provide a `std::tuple` for `T`
as the data in the file are packed, while a `std::tuple` is likely padded to have its member fields properly aligned. Moreover,
`std::tuple` does not guarantee any particular order of its members.  
A number of similar methods are `cbegin<T>()`, `end<T>()`, `cend<T>()`, `data<T>()`.

```c++
template <typename T>
subrange<T const*, T const*> NpyArray::make_range() const
```
Return a range-like object (meaning in particular that it has `begin()`, `end()`, `size()` and alike methods)
which can be used, e.g., in range-based for-loops.

```c++
template <typename... TArgs>
subrange<tuple_iterator<std::tuple<TArgs...>>> NpyArray::tuple_range(bool force_check = false) const
```
Returns a range-like object for structured arrays. You need to provide the types of
the elements of the structured array as template arguments. If `force_check` is set to `true`, the byte sizes
of the requested data types are checked against the values found in the file header and an exception is thrown
if not.

```c++
template <typename TValueType>
subrange<stride_iterator<TValueType>> NpyArray::column_range(std::string_view name) const
```
If you interested only in a particular field of a structured array (data "column"). `column_range()` returns
a range that iterates only over the field indicated by its label `name` as parameter.

