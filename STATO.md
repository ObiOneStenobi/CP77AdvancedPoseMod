# STATO – AdvancedPoseMod 1.4.0 (in corso)

Aggiornato: 2026-10-22 (sera)

## Obiettivo 1.4.0
1. Supporto gioco 2.5 (3.0.88.46235) oltre alla 2.31 (3.0.80.51928).
2. Fix: il clic su un pulsante vale solo se pressione e rilascio sono sullo stesso pulsante.
3. Talloni (LeftHeel/RightHeel): nel pannello, gizmo solo da vicino.
4. Pose (ed espressioni) lette anche dalle sottocartelle.
5. Marker sovrapposti: il più vicino sopra e cliccabile, quelli dietro più trasparenti (sempre selezionabili).
6. Pose della mano (dita e palmo) salvate, un file per entrambe le mani.
7. Gizmo classici rotazione/spostamento (pulsante a ciclo off/rotazione/spostamento, assi locali con tasto per
   passare a quelli del modello; quando attivi i marker attuali servono solo a selezionare).
8. Studio posa sui veicoli (sulla 2.5).

## Fatto (build ok, script ok su 2.31, copiato in progetto + dist, md5 verificato)
- 2: `m_pressedButtonId` in PoseEditor.reds (azzerato all'update dopo il rilascio).
- 3: BoneCatalog (in coda, `closeUp`), JointLimits `Heel` {-30,30,15,15} da tarare, nativo `APM_GizmoIsPartCloseUp`,
  linea delle dita del piede sempre dal piede, marker kind CloseUp (vicino o selezionato).
- 4: `src/Core/NamedFiles.hpp` (List ricorsivo, Resolve sicuro, nomi UTF-8 "Cartella/Nome"); i salvataggi nuovi
  restano nella cartella principale; rinomina resta nella sua cartella.
- 5: `ResolveMarkerOverlaps` (raggio 12 px, opacità x0.45, ReorderChild solo quando cambia).
- 6: libreria `hands/` (PhotoModeUi.cpp, natives APM_*HandPose*), pulsante "Hands" nell'intestazione delle pose,
  applicata alla mano della scheda aperta; OpenFolder(2).
- 1 (parziale): CheckGameVersion accetta 2.31 e 2.5; MenuGuard cerca `lea rcx,[rdi+0x1xx]` (0x140 nella 2.5).
- Versione 1.4.0 in CMakeLists; translation_template rigenerato.

## Verifica 2.5 (statica, exe/json/bundle in ~/ref231 e ~/ref25 della sessione cloud)
- Hash PhotoModeSystem: tutti presenti nella 2.5.
- Output::Sample: prologo identico (2.5 +0x3AACCC), slot vtable 0x128 invariato.
- gamePhotoModeSystem: PlayerList 0x130, CharacterList 0x180, stride 0x190, SceneSlots 0x1D0, Type 0x5C invariati.
- gameuiPhotoModeMenuController: forward da this+0x140 (era 0x130) -> MenuGuard aggiornato.
- Script: redscript 0.5.31 non legge il bundle 2.5 ("failed to fill whole buffer", flags=2): attendere redscript.
  Nomi delle classi/metodi wrappati presenti nel bundle 2.5.
- Da fare in gioco: contesto anim (ctx+0x48->+0x118), offset struct SDK (Entity::components,
  AnimatedComponent::rig, Rig), ID attributi Photo Mode (15, 74, 28, 56, 68).
- Dipendenze da aggiornare dagli autori: RED4ext, redscript, Codeware (e SDK per la build).

## Fatto dopo (seconda parte)
- Marker sovrapposti: attenuazione graduale solo da vicino (marker davanti < 1,3 m): 1 cm pieno -> 5 cm al 45%.
- Robustezza patch: `src/Game/EngineLayout.*` (offset da RTTI: IComponent.name, AnimatedComponent.rig, Rig.boneNames,
  Rig.tags; components dell'entità verificati/cercati), usato da PhotoModeTracker; Resolve() in OnPostRegisterTypes.
- Gizmo classici: pulsante "Direct/Rotate/Move" accanto a Gizmos, "Local/Global" (pulsante assi);
  anelli 4x32 segmenti (rosso/verde/blu = assi del pannello via nativo `APM_GizmoGetPartAxis`, bianco = vista),
  frecce 3 (hips, oggetti, IK); hit = quadrati invisibili 18 px; drag = spostamento cursore proiettato sulla
  tangente (rad o m); marker solo selezione quando attivo; pref "ClassicGizmo", "ClassicGizmoModelAxes".
- README e RESEARCH.md aggiornati (2.5, veicoli, file mani, sottocartelle, gizmo).
- Il gioco è già alla 2.5: nessun test in gioco possibile finché RED4ext non è aggiornato.

## Prossimi passi
- Quando escono RED4ext/redscript/Codeware per 2.5: check_scripts sul bundle 2.5, test in gioco di tutto
  (soprattutto verso degli anelli, limiti talloni, log "Layout:"), test veicoli (piano in RESEARCH.md).
- In attesa: pose adattate all'altezza (rimandato dall'utente), area clic marker (non serve per ora).

## Note
- Cartella download Vortex: C:\Users\ObiOneStenobi\AppData\Roaming\Vortex\downloads\cyberpunk2077
- Gioco: C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077
