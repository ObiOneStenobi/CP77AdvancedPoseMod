# AdvancedPoseMod (Cyberpunk 2077)

Repo GitHub ObiOneStenobi/CP77AdvancedPoseMod, Nexus 35013. Plugin RED4ext in C++ (src/) e script redscript
(scripts/AdvancedPoseMod/). Commit e push li fa l'utente: dopo ogni modifica dare i comandi git con un messaggio che
contenga solo le novità.

All'inizio di ogni sessione leggere STATO.md (obiettivi, decisioni, file toccati, prossimi passi). Approfondimenti
tecnici in docs/RESEARCH.md (sezioni "Game 2.5" e "Vehicles").

## Versioni
- Gioco 2.5 (3.0.88.46235). La 1.4.0 è compilata con RED4ext.SDK 0f5eba4 e accetta solo la 2.5; la 2.31 resta alla 1.3.0.
- Script: redscript 0.5.32. In gioco non si può ancora provare: mancano le release di RED4ext e Codeware per la 2.5.

## Build (solo in locale, mai in cloud: tools/cloud_build non va usato)
- Il CMake 3.22 di VS 2022 non conosce DOWNLOAD_EXTRACT_TIMESTAMP: usare CMake >= 3.24, per esempio quello di VS 18
  (`C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`).
  Il generatore resta "Visual Studio 17 2022"; serve `%ProgramFiles(x86)%\Microsoft Visual Studio\Installer` nel PATH (vswhere).
- `cmake --preset vs2022` poi `cmake --build --preset release` -> build\RelWithDebInfo\AdvancedPoseMod.dll.
  Se la build fallisce per un SDK vecchio: cancellare build e riconfigurare.

## Dopo ogni modifica
- Copiare la DLL in dist\red4ext\plugins\AdvancedPoseMod\ e gli script in dist\r6\scripts\AdvancedPoseMod\.
- Dopo modifiche ai testi dell'interfaccia: `python tools\gen_loc_template.py`.

## Controllo script senza il gioco (redscript-cli)
- redscript-cli v0.5.32 per Windows (release di https://github.com/jac3km4/redscript) in tools\redscript\ (gitignored).
- Cartella temporanea fuori dal progetto con src\AdvancedPoseMod (scripts\AdvancedPoseMod\*.reds) e src\Codeware
  (script di Codeware installati nel gioco, red4ext\plugins\Codeware\Scripts), poi:
  `redscript-cli.exe compile -s <temp>\src -b "<gioco>\r6\cache\final.redscripts" -o <temp>\out.redscripts`
- Contano solo gli errori nei file di AdvancedPoseMod (quelli dentro Codeware sono attesi finché non è aggiornato).
- Da fare: tools\check_scripts.ps1 che automatizzi tutto (proporlo prima di crearlo).

## Strumenti
- WolvenKit.Console 9.1.0 in tools\wolvenkit\ (gitignored): `WolvenKit.CLI.exe archive|unbundle|convert serialize`.
  Legge gli archivi della 2.5. Estrarre sempre in cartelle temporanee fuori dal progetto.

## Cartelle del gioco (sola lettura)
- C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077 (red4ext\logs, r6\cache, r6\logs,
  red4ext\plugins\AdvancedPoseMod\probe, archive\pc\content)
- %LOCALAPPDATA%\REDEngine\ReportQueue (crash dump), %APPDATA%\Vortex\downloads\cyberpunk2077 (zip di altre mod)
- Debug in gioco: config.ini con [Debug] Log=1 e [Probe] DumpRig=1; errori script in r6\logs\redscript_rCURRENT.log.
