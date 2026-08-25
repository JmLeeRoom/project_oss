// SPDX-License-Identifier: Apache-2.0

#include "cogito/secret_string.hpp"

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace {

bool IsZeroed(const volatile void* ptr, std::size_t size) {
  const auto* bytes = static_cast<const volatile unsigned char*>(ptr);
  for (std::size_t i = 0; i < size; ++i) {
    if (bytes[i] != 0U) {
      return false;
    }
  }
  return true;
}

struct ObserverReset {
  ~ObserverReset() { cogito::testing::ClearCleanseObserver(); }
};

}  // namespace

static_assert(!std::is_copy_constructible<cogito::SecretString>::value,
              "SecretString must not be copy constructible");
static_assert(!std::is_copy_assignable<cogito::SecretString>::value,
              "SecretString must not be copy assignable");
static_assert(std::is_nothrow_move_constructible<cogito::SecretString>::value,
              "SecretString move construction must be noexcept");
static_assert(std::is_nothrow_move_assignable<cogito::SecretString>::value,
              "SecretString move assignment must be noexcept");

TEST_CASE("SecretString exposes, moves, and explicitly cleanses live storage", "[secret]") {
  ObserverReset reset;
  bool observed = false;
  std::size_t observed_size = 0U;
  std::size_t calls = 0U;
  cogito::testing::SetCleanseObserver(
      [&](const volatile void* ptr, std::size_t size) {
        ++calls;
        observed = IsZeroed(ptr, size);
        observed_size = size;
      });

  cogito::SecretString source("operator-token");
  REQUIRE(source.Expose() == "operator-token");
  cogito::SecretString moved(std::move(source));
  REQUIRE(source.empty());
  REQUIRE(moved.Expose() == "operator-token");
  REQUIRE(calls == 1U);
  REQUIRE(observed);
  REQUIRE(observed_size == std::string("operator-token").size());

  moved.Clear();
  REQUIRE(moved.empty());
  REQUIRE(calls == 2U);
  REQUIRE(observed);
  REQUIRE(observed_size == std::string("operator-token").size());
}

TEST_CASE("SecretString destructor observes zeroization before storage release", "[secret]") {
  ObserverReset reset;
  bool observed = false;
  cogito::testing::SetCleanseObserver(
      [&](const volatile void* ptr, std::size_t size) {
        observed = size == 17U && IsZeroed(ptr, size);
      });
  {
    cogito::SecretString secret("destructor-secret");
    REQUIRE_FALSE(secret.empty());
  }
  REQUIRE(observed);
}

TEST_CASE("SecretString move cleanses heap-backed source storage", "[secret]") {
  ObserverReset reset;
  const std::string value(256U, 's');
  std::size_t calls = 0U;
  bool source_zeroed = false;
  cogito::testing::SetCleanseObserver(
      [&](const volatile void* ptr, std::size_t size) {
        ++calls;
        if (size == value.size()) {
          source_zeroed = IsZeroed(ptr, size);
        }
      });

  cogito::SecretString source(value);
  cogito::SecretString moved(std::move(source));
  REQUIRE(source.empty());
  REQUIRE(source_zeroed);
  REQUIRE(calls == 1U);
  REQUIRE(moved.Expose() == value);
  moved.Clear();
  REQUIRE(calls == 2U);
}

TEST_CASE("SecretString move assignment cleanses the replaced destination", "[secret]") {
  ObserverReset reset;
  std::size_t calls = 0U;
  bool old_value_zeroed = false;
  bool source_value_zeroed = false;
  cogito::testing::SetCleanseObserver(
      [&](const volatile void* ptr, std::size_t size) {
        ++calls;
        if (size == 10U) {
          old_value_zeroed = IsZeroed(ptr, size);
        } else if (size == 12U) {
          source_value_zeroed = IsZeroed(ptr, size);
        }
      });

  cogito::SecretString destination("old-secret");
  cogito::SecretString source("newer-secret");
  destination = std::move(source);
  REQUIRE(old_value_zeroed);
  REQUIRE(source_value_zeroed);
  REQUIRE(calls == 2U);
  REQUIRE(destination.Expose() == "newer-secret");
  REQUIRE(source.empty());
}

TEST_CASE("Cleanse observers are thread-local and callback exceptions are contained", "[secret]") {
  ObserverReset reset;
  std::size_t main_calls = 0U;
  std::atomic<std::size_t> worker_calls{0U};
  cogito::testing::SetCleanseObserver(
      [&](const volatile void*, std::size_t) { ++main_calls; });

  std::thread worker([&] {
    ObserverReset worker_reset;
    cogito::testing::SetCleanseObserver(
        [&](const volatile void* ptr, std::size_t size) {
          if (IsZeroed(ptr, size)) {
            ++worker_calls;
          }
        });
    cogito::SecretString worker_secret("worker-secret");
    worker_secret.Clear();
  });
  worker.join();

  REQUIRE(worker_calls.load() == 1U);
  REQUIRE(main_calls == 0U);
  cogito::SecretString main_secret("main-secret");
  main_secret.Clear();
  REQUIRE(main_calls == 1U);

  cogito::testing::SetCleanseObserver(
      [](const volatile void*, std::size_t) { throw std::runtime_error("observer"); });
  cogito::SecretString throwing("throwing-observer");
  REQUIRE_NOTHROW(throwing.Clear());
}
