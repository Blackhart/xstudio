# Mikros Shot Browser

Squelette du panel Shot Browser Mikros pour xSTUDIO. Panel vide prêt à être enrichi ; le backend tracking viendra plus tard.

## Build

Activer le plugin via CMake :

```bash
cmake --preset <votre-preset> -DSTUDIO_PLUGINS=mikros
```

Ou laisser `STUDIO_PLUGINS` à `mikros` dans `CMakePresets.json` (preset `default`).

## Utilisation

Une fois xSTUDIO lancé :

- Menu des onglets de panel → **Mikros Shot Browser**
- Icône dossier dans la shelf du viewport (fenêtre flottante)
