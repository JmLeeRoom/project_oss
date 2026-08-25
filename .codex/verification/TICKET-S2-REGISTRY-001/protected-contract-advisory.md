# Protected Contract Advisory

## `ToolRegistry::export_order_version()` allocation under `noexcept`

An independent static review noted that `include/cogito/registry.hpp` declares:

```cpp
std::string export_order_version() const noexcept { return "name-asc-v1"; }
```

Constructing the returned `std::string` can allocate. In an out-of-memory condition, the `noexcept` function would terminate rather than return an error. This declaration is part of the Gemini-owned public-header contract and was not changed by Codex because `include/**` is outside `TICKET-S2-REGISTRY-001`'s six-file write allowlist. It does not affect the implemented registry paths or current acceptance tests.

Recommended Gemini-owned follow-up: decide whether the stable ABI/API should return `std::string_view` or `const char*`, or remove `noexcept` from the allocating return. That decision must be made in the public contract before any implementation-side change.

