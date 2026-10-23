# STATO – AdvancedPoseMod 1.4.0 (in corso)

Aggiornato: 2026-10-23

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

## Fatto il 23/10
- Build con RED4ext.SDK 0f5eba4 (2.5): CBaseFunction/CStackFrame cambiati -> la 1.4.0 accetta SOLO 2.5 (2.31 resta 1.3.0).
  setup.sh e CMakeLists.txt aggiornati; redscript 0.5.32 in setup.sh.
- Script della mod: 0 errori sul bundle 2.5 (errori solo dentro Codeware, non ancora aggiornato per 2.5).
- Pulsante modalità gizmo "Direct / Rotate / Move" accanto a Gizmos; pulsante assi "Local / Global".
- FIX crash/freeze (segnalazioni utenti 1.3.0: freeze toccando un osso; CTD uscendo da Photo Mode dopo molto tempo):
  TargetRegistry teneva lo SRW lock condiviso per tutta la posa (VisitTarget) e i limiti articolari lo riprendevano
  (GetBoneAxes/GetReferencePose) -> deadlock con Set/Clear esclusivo. Ora i lettori usano uno snapshot
  shared_ptr e rilasciano il lock.
- Exe 2.5 ricontrollato il 23/10: invariato (cambiato solo GameServicesSteam.dll).

## Fatto il 23/10 (robustezza, build ok, DLL in dist)
- Genitori: in PhotoModeTracker (costruzione Target) parent >= indice osso -> -1 (radice) + Warn una volta per rig;
  controlli locali in JointLimits, PoseProcessor (occhi, KeepLookAt), GizmoState (catena, punta foglia),
  PoseOverrides (posa neutra), PhotoModeUi (specchio x2), FaceRig, TargetRegistry::GetBoneAxes.
- g_noLookAtCaches protetto da `g_noLookAtMutex` (anche ResetCharacter).
- JointLimits per personaggio: overload con `aCharacter` (LimitLocal, GetReferenceFrame; IK passa il personaggio
  del target, la UI quello selezionato); cache per slot valida per il rig (`TargetRegistry::GetMainRig`), svuotata
  se il rig cambia e con `JointLimits::ResetCache()` ad Activate/Deactivate/uscita. TargetRegistry: GetBoneAxes e
  GetReferencePose con personaggio, ReferencePose.rig.
- Despawn: `EngineLayout::LockAlive` (legge ISerializable::ref +0x08 con SafeMemory, instance == oggetto,
  strongRefs > 0, poi WeakHandle::Lock = IncRefIfNotZero) usato in AddPuppetTargets (entità e componenti, handle
  tenuti per tutta la funzione; entityRef/componentRef presi dagli handle) e ProbePuppet.
- Build: il CMake 3.22 di VS 2022 non conosce DOWNLOAD_EXTRACT_TIMESTAMP -> usare CMake >= 3.24 (quello di VS 18:
  ...\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe 4.2).

## Test in gioco (quando escono RED4ext/Codeware per 2.5)
- Avvio: log red4ext (versione 2.5 accettata, "Layout:" senza Warn inattesi, MenuGuard), redscript_rCURRENT.log
  senza errori della mod; nessun Warn "parent after them".
- Pulsanti: pressione su uno e rilascio su un altro non clicca.
- Talloni: gizmo solo da vicino; limiti Heel {-30,30,15,15} da tarare.
- Pose/espressioni/pose mani (hands/) da sottocartelle; salvataggio nella principale; rinomina resta nella sua cartella.
- Marker sovrapposti (vicino sopra/cliccabile, dietro trasparenti, attenuazione < 1,3 m).
- Pose mani: salva/carica (pulsante Hands), applicata alla mano della scheda aperta.
- Gizmo classici: Direct/Rotate/Move, Local/Global, verso degli anelli, frecce (hips, oggetti, IK), drag.
- Freeze/CTD 1.3.0: trascinare a lungo con IK + limiti; uscire da Photo Mode dopo molto tempo.
- Nuovo: IK + limiti su un personaggio aggiunto (NPC, corporatura diversa) e su V mentre è selezionato l'altro:
  ginocchia/gomiti nel verso giusto; cambiare selezione avanti e indietro.
- Nuovo: aggiungere, sostituire e rimuovere personaggi più volte, uscire/rientrare in Photo Mode (despawn): niente
  crash, target aggiornati.
- Nuovo: "Look at camera" con cambio della posa di Photo Mode (posa ricordata scartata, nessun scatto).
- Da verificare: contesto anim (ctx+0x48->+0x118), offset SDK (Entity::components, AnimatedComponent::rig, Rig),
  ID attributi Photo Mode (15, 74, 28, 56, 68); veicoli (piano in RESEARCH.md).

## Editor espressioni dalle pose del facialsetup (piano approvato in parte, 23/10)
- Ricerca: WolvenKit.Console 9.1.0 in tools\wolvenkit (gitignored), legge gli archivi 2.5. 388 `.facialsetup`
  (`..._rigsetup.facialsetup`, uno per testa) + rig `..._skeleton.rig` (face_rig: 344 ossa, 414 tracce).
  Pose principali FACS: viso 121 (133 con inbetween, es. jaw_mid_open 0.5/1), lingua 18, occhi 12; 255 correttive.
  Dati: mainPosesData.Face.Poses[] {TransformIdx, NumTransforms} + Transforms[] {Rotation, Translation, Bone};
  ordine = bakedData.Face.AllMainPoses[] {Track -> rig.trackNames, NumInbetweens}, pesi inbetween in
  AllMainPosesInbetweens. Delta locali: rotazione di riposo del face_rig = identità, traslazione delta (mm).
- Teste (pwa base vs variante pwa, pma, pma variante, wa, ma): stesse 344 ossa e 121 pose; delta diversi per testa
  (spostamento: mediana 0,2 mm su 1,3 mm, p90 ~1 mm; rotazione: mediana ~1,2° su 4,6°, p90 ~5°). Diff. tra generi
  uguale a quella tra due teste dello stesso genere -> ogni testa è tarata a sé.
- Decisioni: slider = peso posa (0-100%) sommato all'espressione di Photo Mode; togliere i 23 controlli euristici e
  Observe; escludere sguardo, collo, testa; modalità Semplice (~25-30, lista da proporre dopo l'anteprima) /
  Avanzata (tutte); L+R collegati; espressioni salvate: + pesi nel JSON. Niente "parti da neutro" (c'è già
  l'espressione neutra di Photo Mode). Correttive e gizmo osso per osso dopo; rughe non fattibili (materiale).
- Deciso: dati delle teste in file JSON pwa (femminili, e NPC come riserva) e pma (maschili) in
  dist\red4ext\plugins\AdvancedPoseMod\face\; scelta col tipo Photo Mode (1 = F, 2 = M). Obiettivo dopo i test in
  gioco: leggere il facialsetup già caricato per il personaggio (taratura esatta), JSON come riserva.
- Fase A FATTA (23/10): `tools\extract_face_poses.ps1` (WolvenKit unbundle+serialize di
  player_base_heads\player_(female|man)_average\h0_000_p(w|m)a_c__basehead, lavoro in %TEMP%\apm_face o -WorkDir)
  -> `tools\face_poses.py` -> dist\...\face\pwa.json e pma.json (~340 KB, una posa per riga);
  `tools\face_preview.py` -> face_preview.html nella cartella di lavoro (aprirla via http locale, es.
  `python -m http.server`; file:// dal browser della app non va).
  Formato: {version, head, game, bones[nomi], poses[{name, zone, side, mirror, inbetweens[{w, t[[osso,qx,qy,qz,qw,
  tx,ty,tz]]}]}]}, metri. 87 pose: brows 8, eyelids 10, nose 8, lips 35, cheeks 4, jaw 5, ears 2, tongue 15; 257 ossa.
  Inbetween: tra due inbetween (il primo a w=0 è il riposo) slerp rotazione / lerp traslazione; pose sommate per osso
  (rotazioni moltiplicate, traslazioni sommate); locale = riposo*rot, riposo+trasl. Verificato in anteprima
  (jaw_mid_open 31 mm, a 0,5 14 mm; blink solo palpebre).
  Note: lips_together_up/dn, lips_tighten_up/dn, jaw_mid_close, jaw_mid_clench sono VUOTE nelle pose principali
  (agiscono solo via correttive, es. `lips_together_dn__Corr`) -> valutare le correttive singole nella fase B/C.
  tongue_mid_thick scartata (posa di scala). La pwa di head\pwa\ (NPC) ha delta diversi da quella del giocatore.
- Deciso (23/10, dopo la prova in anteprima): NIENTE modalità Semplice/Avanzata, tutte le pose visibili (~55 slider
  con L+R e bipolari). Zone richiudibili (lingua chiusa all'inizio), pose più usate in cima a ogni zona (ordine come
  in tools\face_preview.py ORDER), L+R collegati nascondono le righe R, slider bipolari -100..+100 per le pose opposte
  (BIPOLAR in face_preview.py; nel JSON salvato restano due pesi), "Reset" per zona.
- Fase B approvata (23/10): src/Pose/FacePoses.* sostituisce FaceRig.* (via controlli, Observe, misura); pesi 0-1 (max
  100%) per posa in PoseOverrides (FaceWeights, undo); attributi UI 7800 + indice posa (Last 7999); natives
  APM_FacePose*; espressioni salvate formato 2 {"weights": {nome: peso}}, le vecchie (solo test) non si caricano più.
  NPC (tipo 4) -> pwa. Correttive: DOPO la fase C (regola del peso da ricavare, prima in anteprima), con i 4 slider
  mancanti (lips_together_up/dn, lips_tighten_up/dn).
- Fase B FATTA (23/10, build ok, DLL in dist): src/Pose/FacePoses.* (LoadData in Main, Apply in
  ProcessSecondarySkeleton al posto di FaceRig::Apply, cache ossa per rig+testa, libreria espressioni v2);
  PoseOverrides FaceWeights[128] + faceName (undo); PhotoModeUi Attr::FacePose 7800, Last 7999, g_faceLinked;
  natives APM_FaceAvailable/PoseCount/PoseName/PoseZone/PoseSide/PoseMirror/SetLinked/IsLinked/IsEdited/ResetPose/
  ResetZone/Reset (tolti FaceControl*, FaceResetControl, FaceUnload); Natives.reds aggiornato. FaceRig.* e Observe
  rimossi. ATTENZIONE: PoseEditor.reds usa ancora i natives vecchi -> script non compilano e dist non è coerente
  (DLL nuova, script vecchi) finché non si fa la fase C.
- Ipotesi da verificare in gioco (RESEARCH.md "Face poses"): scrivere i pesi nelle tracce float del face_rig prima del
  risolutore facciale del gioco -> correttive e taratura per testa automatiche; sostituirebbe solo FacePoses::Apply.
- Fasi: A) tools\extract_face_poses.ps1 + tools\face_poses.py (-> dist\...\face\*.json) + tools\face_preview.py;
  B) src/Pose/FacePoses.* (carica, mappa ossa per nome, Apply al posto di FaceRig::Apply), PoseOverrides, natives;
  C) UI APM_Face (zone, Semplice/Avanzato, L+R), testi + gen_loc_template, APM_FaceFeatureEnabled true;
  D) build, check script, dist, STATO.
- File temporanei della ricerca nello scratchpad della sessione (rigenerabili con gli script della fase A).

## Da fare (ordine proposto)
1. Rilascio 1.4.0: changelog/testo Nexus + lista test in gioco.
2. Editor espressioni (sopra), fase A subito, B-D dopo.
3. Pose adattate all'altezza (opzionale).
- In attesa: release RED4ext con 2.5 (supporto già su master) e Codeware per 2.5.

## Prossimi passi
- Quando escono RED4ext/redscript/Codeware per 2.5: check_scripts sul bundle 2.5, test in gioco di tutto
  (soprattutto verso degli anelli, limiti talloni, log "Layout:"), test veicoli (piano in RESEARCH.md).
- In attesa: pose adattate all'altezza (rimandato dall'utente), area clic marker (non serve per ora).

## Note
- Cartella download Vortex: C:\Users\ObiOneStenobi\AppData\Roaming\Vortex\downloads\cyberpunk2077
- Gioco: C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077
