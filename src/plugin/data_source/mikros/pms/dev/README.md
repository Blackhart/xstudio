# FakeBackend (local only)

`FakeBackend` is a fixed in-memory catalog for UI and integration work. It is
not part of the versioned library. Copy the examples before building the plugin
or the FakeBackend tests:

```bash
mkdir -p src/pms/fake tests/fake
cp dev/fake_backend.hpp.example src/pms/fake/backend.hpp
cp dev/fake_backend.cpp.example src/pms/fake/backend.cpp
cp dev/test_fake_backend.cpp.example tests/fake/test_fake_backend.cpp
```

CMake links `src/pms/fake/backend.cpp` and the FakeBackend doctest only when
those files exist on disk. Paths are relative to `mikros/pms/`.
