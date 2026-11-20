# Mikros Shot Browser

Mikros Shot Browser panel for xSTUDIO. The query core covers `project`,
`sequence`, `shot`, `asset`, `playlist`, `version`, and the `find` verb.

## Architecture

```text
mikros/
  pms/                 shared lib (libpms.so): query, schema, backends,
                       process-wide Session + CachingBackend
  shotbrowser/
    src/
      model/
        poco/          Project, Sequence, Shot, Asset, Playlist, …
        dal/           sole gateway to pms for this plugin
        context/       CAF actor, selection, UI attributes
      view/…           QML and resources only
      mikros_shotbrowser.cpp
```

Flow: `pms` → DAL → context → view.

## Version media prefetch (two phases)

Opening a version must not wait on a fresh ShotGrid find for every click when
paths are already known:

1. **List find** — each `VersionListElement` carries local `frames` / `movie`
   path fields (and `frame_range`). `uploadedMovie` is not requested here, so
   ShotGrid does not mint signed URLs for the whole page.
2. **After append** — a detached resolve (same pattern as thumbnails) checks
   disk and the movie cache. Versions that still need a signed URL go through
   one batch `findVersionTranscodedMovies`. The panel writes
   `uploadedMovie` onto the owning `VersionCard` (no process-wide store).
3. **Preview / open** — `retrieveVersionMedia` reads frames / movie /
   uploadedMovie from the card. Download of the MP4 still happens only on first
   open via `resolveVersionPaths`. If the batch has not finished yet, a one-shot
   signed-URL find fills the gap.

Cached pages live for 5 minutes and up to 64 MiB in the shared `pms` library.
Override either limit with `MIKROS_PMS_CACHE_TTL_SECONDS` or
`MIKROS_PMS_CACHE_BYTES`. There is no cap on the number of cached queries: the
byte budget evicts the least recently used page. Other Mikros plugins that link
`libpms.so` share the same cache.

A catalog page holds 50 rows. Override it with `MIKROS_PMS_PAGE_SIZE`. ShotGrid
accepts at most 500 rows in one page, and a larger value is rejected.

`pms` backends: `FakeBackend` (local), `ShotGridBackend` (synchronous REST), always
behind `CachingBackend`.

## Fake backend (default)

`FakeBackend` is not versioned. For the local panel, copy the examples from
`mikros/pms/dev/` into `mikros/pms/src/pms/fake/` and `mikros/pms/tests/fake/`.
With `simulate_latency`, `find()` adds a random delay; the context calls the DAL
on a worker thread.

## ShotGrid backend

1. Copy `mikros/pms/config/shotgrid/mapping.yaml.example` to the studio mapping. The example is not loaded on its own. Keep `version.links.project` on the ShotGrid field `project` (not `entity`) so project-scoped version finds stay a single filter.
2. Start xSTUDIO with:

```bash
export MIKROS_PMS_BACKEND=shotgrid
export PMS_SCHEMA_PATH=/path/to/schema.yaml   # optional; shotbrowser sets the installed copy when unset
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

See [`../pms/README.md`](../pms/README.md).

## Usage

- Panel tab menu → **Mikros Shot Browser**
- Folder icon on the viewport shelf (floating window)
