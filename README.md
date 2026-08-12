# PrismaSID 🌈

**Der erste moderne SID-Player seit Jahrzehnten.**

PrismaSID ist ein ultramoderner Player für C64-Chipmusik (SID-Dateien) — mit dunkler Glas-Oberfläche, GPU-beschleunigter Regenbogen-Visualisierung und vollem Funktionsumfang. Gebaut auf **libsidplayfp** (dem Goldstandard der SID-Emulation) und **Qt6**.



## Features

- 🎵 **SID-Playback** über libsidplayfp + reSIDfp — die genaueste 6581/8580-Emulation
- 🎚️ **Subsongs** (Umschaltung per Klick), **Chip-Modell** AUTO/MOS6581/MOS8580
- 🔊 **Stereo/Mono** — echtes L/R bei Dual-/Triple-SID-Tunes
- 🌈 **3 Visualisierungen** (WAVE / BARS / MIX) mit animiertem Regenbogen-Verlauf — GPU-gerendert (Scene-Graph), ~0% CPU
- 📂 **Samba-/KDE-Orte** — Netzlaufwerke direkt durchstöbern (kioclient5/KIO mit KWallet-Credentials, asynchron)
- 🖼 **HVSC-Browsing** — zuschaltbare Cover-Kachel-Ansicht (cover.jpg/png in Album-Ordnern) oder klassische Liste
- 📋 **Playlist** mit Hinzufügen/Entfernen/Leeren, Ordner-Import (inkl. Unterordner) und **Persistenz** (letzte Playlist wird wiederhergestellt)
- 🎧 **Automatische Geräte-Umschaltung** — wechsle Kopfhörer/Lautsprecher während der Wiedergabe, der Sound zieht nahtlos um
- 🔤 Korrekte Umlaute (SID-Tags sind Latin-1)
- 🖥 Läuft auf KDE/Plasma (Wayland & X11), Arch-basierten Distros

## Build

**Abhängigkeiten:** Qt6 (Core, Gui, Qml, Quick, Multimedia, Concurrent), libsidplayfp, libresidfp, CMake ≥ 3.16, C++17-Compiler

```bash
git clone https://github.com/shadowmaker/PrismaSID.git
cd PrismaSID
cmake -B build
cmake --build build
./build/prismasid            # startet leer
./build/prismasid song.sid   # startet und spielt sofort
```

## Bedienung

- **📂 Öffnen** — Datei-Browser mit KDE-Orten (auch Samba): Klick auf Song spielt sofort
- **＋ Zur Playlist** — Songs oder ganze Ordner (mit Unterordnern) zur Playlist
- **🖼 Cover** — schaltet die Cover-Kachel-Ansicht um (Cover-Bilder müssen in den Album-Ordnern liegen)
- **VIZ** — WAVE / BARS / MIX Visualisierung
- **CHIP** — AUTO / MOS6581 / MOS8580 Emulation
- **AUSGANG** — MONO / STEREO (Stereo nur hörbar bei Tunes mit ≥2 SID-Chips)

Die Playlist wird automatisch in `~/.config/sidplayer/playlist.json` gespeichert.

## Lizenz

BSD-3-Clause — siehe [LICENSE](LICENSE).

Copyright © 2026 Nika & Mike
