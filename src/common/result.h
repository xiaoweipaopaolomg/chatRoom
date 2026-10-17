#pragma once

#include <optional>
#include <utility>
#include <variant>

#include "common/error.h"

namespace chatroom {

template <typename T>
class Result {
 public:
  Result(T value) : value_(std::move(value)) {}
  Result(Error error) : value_(std::move(error)) {}

  bool has_value() const noexcept { return std::holds_alternative<T>(value_); }
  explicit operator bool() const noexcept { return has_value(); }
  T& value() { return std::get<T>(value_); }
  const T& value() const { return std::get<T>(value_); }
  T* operator->() { return &value(); }
  const T* operator->() const { return &value(); }
  T& operator*() { return value(); }
  const T& operator*() const { return value(); }
  Error& error() { return std::get<Error>(value_); }
  const Error& error() const { return std::get<Error>(value_); }

 private:
  std::variant<T, Error> value_;
};

template <>
class Result<void> {
 public:
  Result() = default;
  Result(Error error) : error_(std::move(error)) {}

  bool has_value() const noexcept { return !error_.has_value(); }
  explicit operator bool() const noexcept { return has_value(); }
  Error& error() { return error_.value(); }
  const Error& error() const { return error_.value(); }

 private:
  std::optional<Error> error_;
};

}  // namespace chatroom
