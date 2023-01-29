// Copyright (C) 2023 Maximilian Reininghaus
// Released under MIT License
// license available in LICENSE file, or at
// http://www.opensource.org/licenses/mit-license.php

#include <cstddef>

#include <boost/iostreams/device/mapped_file.hpp>

#include <cnpy++/buffer.hpp>

cnpypp::InMemoryBuffer::InMemoryBuffer(size_t size)
    : buffer{std::make_unique<std::byte[]>(size)} {}

std::byte const* cnpypp::InMemoryBuffer::data() const { return buffer.get(); };

std::byte* cnpypp::InMemoryBuffer::data() {
  return const_cast<std::byte*>(
      static_cast<InMemoryBuffer const&>(*this).data());
}

static auto const alignment = boost::iostreams::mapped_file::alignment();

cnpypp::MemoryMappedBuffer::MemoryMappedBuffer(std::string const& path,
                                               size_t offset_, size_t length)
    : offset{offset_ % alignment},
      buffer{path, boost::iostreams::mapped_file::mapmode::priv,
             offset + length,
             static_cast<boost::iostreams::stream_offset>(
                 (offset_ / alignment) * alignment)} {}

std::byte const* cnpypp::MemoryMappedBuffer::data() const {
  return reinterpret_cast<std::byte const*>(buffer.data() + offset);
}

std::byte* cnpypp::MemoryMappedBuffer::data() {
  return reinterpret_cast<std::byte*>(buffer.data() + offset);
}
