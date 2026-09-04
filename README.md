# AudioXL

Audio framework for Cyberpunk 2077 2.31: merge audio-metadata records instead of replacing the cooked file, and play new sounds through the game's own engine with no REDmod, no soundbank authoring and no second audio engine.

A mod drops a folder of WAV, MP3, OGG or FLAC files and a `sounds.json` under `red4ext/plugins/AudioXL/sounds/<Mod>/`. AudioXL feeds them into the engine's custom-sound registry, so each becomes a first-class Wwise event: it can replace a vanilla event by name, be posted from any script, sit on an NPC with occlusion, and follow the SFX, Dialogue, Music or Vehicle Radio slider.

Full documentation, manifest reference and the REDmod conversion recipe: the Nexus page, whose text is `r6/storages/RedscriptConfigFramework/AudioXL.docs.txt` in this tree.

## Layout

- `r6/scripts/AudioXL/AudioXL.reds` - the RedScript half: metadata merge, patchers, the public `AudioXLAPI`, voice lines with subtitles.
- `red4ext/plugins/AudioXL/audioxl_routing.bnk` - a 984-byte soundbank adding the `axl_*` routing events on the vanilla mixers.
- `plugin/` - the RED4ext plugin: the registry bridge, manifest loader, decoders, the Audio Input feed, bank loading.

## Building the plugin

Visual Studio 2022 and CMake. RED4ext.SDK and RedLib are expected as sibling checkouts, see `plugin/CMakeLists.txt`.

```
cd plugin
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

The DLL goes to `red4ext/plugins/AudioXL/AudioXL.dll`. Engine addresses are resolved at load through RED4ext from the game's own `cyberpunk2077_addresses.json` (hash list), so they follow game patches; struct layouts were reverse-engineered on 2.31, and an untested build runs with a warning in the log. A build missing any symbol disables the plugin and names the symbol in its status.

## Requirements

RED4ext, RedScript, Codeware.
