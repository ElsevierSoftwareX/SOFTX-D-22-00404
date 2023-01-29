// Copyright (C) 2023 Maximilian Reininghaus
// Released under MIT License
// license available in LICENSE file, or at
// http://www.opensource.org/licenses/mit-license.php

#pragma once

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>

#include <boost/iostreams/device/mapped_file.hpp>

namespace cnpypp {
class Buffer {
protected:
  Buffer() = default;

public:
  virtual ~Buffer() = default;
  virtual std::byte* data() = 0;
  virtual std::byte const* data() const = 0;
};

class InMemoryBuffer : public Buffer {
public:
  InMemoryBuffer(size_t size);
  InMemoryBuffer(InMemoryBuffer const&) = delete;
  InMemoryBuffer(InMemoryBuffer&&) = default;
  ~InMemoryBuffer() = default;

  virtual std::byte* data() override;
  virtual std::byte const* data() const override;

private:
  std::unique_ptr<std::byte[]> buffer;
};

class MemoryMappedBuffer : public Buffer {
public:
  MemoryMappedBuffer(std::string const& path, size_t offset, size_t length);
  MemoryMappedBuffer(MemoryMappedBuffer const&) = delete;
  MemoryMappedBuffer(MemoryMappedBuffer&&) = default;
  ~MemoryMappedBuffer() = default;

  virtual std::byte* data() override;
  virtual std::byte const* data() const override;

private:
  size_t const offset;
  boost::iostreams::mapped_file buffer;
};
} // namespace cnpypp
