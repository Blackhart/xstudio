# Mikros Shot Browser

Mikros Shot Browser panel for xSTUDIO. The query core covers `project`,
`sequence`, `shot`, `asset`, `playlist`, `version`, and the `find` verb.

## Architecture

```text
shotbrowser/
  src/
    model/
      pms/             pure C++17 library, no Qt and no CAF
        fake/          FakeBackend (local, gitignored)
        shotgrid/      ShotGridBackend + translate
        schema/        schema.yaml (domain)
        tools/         gen_entities.py
        tests/         doctest
        dev/           FakeBackend examples
      poco/            Project, Sequence, Shot, Asset, Playlist, …
      dal/             sole gateway to pms
      context/         CAF actor, selection, UI attributes
    view/qml/…         QML and resources only
    mikros_shotbrowser_plugin.cpp
```

Flow: `pms` → DAL → context → view.

Cached pages live for 5 minutes and up to 64 MiB. Override either limit with
`MIKROS_PMS_CACHE_TTL_SECONDS` or `MIKROS_PMS_CACHE_BYTES`. There is no cap on
the number of cached queries: the byte budget evicts the least recently used
page.

`pms` backends: `FakeBackend` (local), `ShotGridBackend` (synchronous REST), always
behind `CachingBackend`.

## Fake backend (default)

`FakeBackend` is not versioned. For the local panel, copy the examples from
`src/model/pms/dev/` into `src/model/pms/fake/` and
`src/model/pms/tests/fake/`. With `simulate_latency`, `find()` adds a random
delay; the context calls the DAL on a worker thread.

## ShotGrid backend

1. Copy `src/model/pms/shotgrid/mapping.yaml.example` to the studio mapping. The example is not loaded on its own.
2. Start xSTUDIO with:

```bash
export MIKROS_PMS_BACKEND=shotgrid
export SHOTGRID_URL=https://your-site.shotgunstudio.com
export SHOTGRID_CLIENT_ID=your_script_name
export SHOTGRID_CLIENT_SECRET=your_script_key
export SHOTGRID_MAPPING=/path/to/mapping.yaml
```

Runtime auth is a script (`client_credentials`). The `ShotGridBackend` API also
accepts a user login (`password` grant) for permission tests.

## Plugin build

```bash
cmake --preset <your-preset> -DSTUDIO_PLUGINS=mikros
```

## pms tests, without building xSTUDIO

```bash
cmake -S src/plugin/data_source/mikros/shotbrowser/src/model/pms -B /tmp/pms-build -DBUILD_TESTING=ON
cmake --build /tmp/pms-build
ctest --test-dir /tmp/pms-build --output-on-failure
```

## Usage

- Panel tab menu → **Mikros Shot Browser**
- Folder icon on the viewport shelf (floating window)
