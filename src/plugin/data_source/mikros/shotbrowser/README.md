# Mikros Shot Browser

Panel Shot Browser Mikros pour xSTUDIO. Le cœur de requête couvre `project`,
`sequence`, `shot`, `asset`, `playlist`, `version` et le verbe `find`.

## Architecture

```text
shotbrowser/
  src/
    model/
      pms/             bibliothèque C++17 pure, sans Qt ni CAF
        fake/          FakeBackend (local, gitignored)
        shotgrid/      ShotGridBackend + mapping.yaml + translate
        schema/        schema.yaml (domaine)
        tools/         gen_entities.py
        tests/         doctest
        dev/           exemples FakeBackend
      poco/            Project, Sequence, Shot, Asset, Playlist, …
      dal/             seule porte vers pms
      context/         acteur CAF, sélection, attributs UI
    view/qml/…         QML et ressources uniquement
    mikros_shotbrowser_plugin.cpp
```

Flux : `pms` → DAL → context → vue.

Backends `pms` : `FakeBackend` (local), `ShotGridBackend` (REST sync), toujours
derrière `CachingBackend`.

## Backend Fake (défaut)

`FakeBackend` n'est pas versionné. Pour le panel en local, copier les exemples
depuis `src/model/pms/dev/` vers `src/model/pms/fake/` et
`src/model/pms/tests/fake/`. Avec `simulate_latency`, `find()` ajoute une
latence aléatoire ; le context appelle la DAL sur un thread worker.

## Backend ShotGrid

1. Remplir [`src/model/pms/shotgrid/mapping.yaml`](src/model/pms/shotgrid/mapping.yaml)
   (noms d'entités / champs / liens ShotGrid du studio).
2. Lancer xSTUDIO avec :

```bash
export MIKROS_PMS_BACKEND=shotgrid
export MIKROS_SG_URL=https://your-site.shotgunstudio.com
export MIKROS_SG_SCRIPT_NAME=your_script_name
export MIKROS_SG_SCRIPT_KEY=your_script_key
# optionnel :
# export MIKROS_SG_MAPPING=/path/to/mapping.yaml
```

Auth runtime = script (`client_credentials`). L'API `ShotGridBackend` accepte
aussi un login user (`password` grant) pour des tests de permissions.

## Build du plugin

```bash
cmake --preset <votre-preset> -DSTUDIO_PLUGINS=mikros
```

## Tests de pms, sans compiler xSTUDIO

```bash
cmake -S src/plugin/data_source/mikros/shotbrowser/src/model/pms -B /tmp/pms-build -DBUILD_TESTING=ON
cmake --build /tmp/pms-build
ctest --test-dir /tmp/pms-build --output-on-failure
```

## Utilisation

- Menu des onglets de panel → **Mikros Shot Browser**
- Icône dossier dans la shelf du viewport (fenêtre flottante)
