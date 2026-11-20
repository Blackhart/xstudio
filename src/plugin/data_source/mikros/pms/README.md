# Mikros PMS

Shared C++17 library for Mikros production-tracking queries. No Qt, no CAF.
Mikros plugins (`shotbrowser`, later `versionDetails`, …) link the same
`libpms.so` and share one process-wide `Session` / `CachingBackend`.

## Layout

```text
pms/
  CMakeLists.txt
  cmake/                 build (SHARED lib, codegen, tests, install)
  src/
    pms/                 library sources and public headers
      shotgrid/
      fake/              FakeBackend (local, gitignored)
    tools/
      gen_entities.py
  config/
    schema.yaml          installed as mikros_pms/schema.yaml
    shotgrid/
      mapping.yaml.example
  tests/
  dev/                   FakeBackend examples
```

## Shared session

```cpp
pms::setSharedInfoLog([](std::string_view msg) { /* host logger */ });
pms::Session &session = pms::sharedSession();
```

`MIKROS_PMS_BACKEND` selects `fake` (default) or `shotgrid`. Schema path comes
only from `PMS_SCHEMA_PATH` (required). Mikros plugins may set it at launch when
the variable is unset (e.g. shotbrowser uses `xstudio_root("/mikros_pms/schema.yaml")`).

## Standalone tests

When `src/pms/fake/` is present, OpenImageIO is required (thumb generation).
Point CMake at the xSTUDIO vcpkg install if needed:

```bash
cmake -S src/plugin/data_source/mikros/pms -B /tmp/pms-build -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH=$PWD/vcpkg_installed/x64-xstudio-linux-ubuntu2404
cmake --build /tmp/pms-build
ctest --test-dir /tmp/pms-build --output-on-failure   # or /tmp/pms-build/cmake
```


## Fake backend

```bash
mkdir -p src/pms/fake tests/fake
cp dev/fake_backend.hpp.example src/pms/fake/backend.hpp
cp dev/fake_backend.cpp.example src/pms/fake/backend.cpp
cp dev/test_fake_backend.cpp.example tests/fake/test_fake_backend.cpp
```
