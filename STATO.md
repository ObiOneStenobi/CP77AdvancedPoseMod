# STATO – AdvancedPoseMod 1.4.0 (in corso)

Aggiornato: 2026-10-30

## Obiettivo 1.4.0
1. Supporto gioco 2.5 (3.0.88.46235) oltre alla 2.31 (3.0.80.51928).
2. Fix: il clic su un pulsante vale solo se pressione e rilascio sono sullo stesso pulsante.
3. Talloni (LeftHeel/RightHeel): TOLTI di nuovo il 24/10 (l'osso è il perno che muove l'avampiede).
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
- Marker sovrapposti: attenuazione graduale solo da vicino (vedi 25/10: < 1,0-1,5 m, 0,5 cm pieno -> 3 cm al 15%).
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
  rimossi.
- Fase C FATTA (24/10): redscript-cli 0.5.32 scaricato in tools\redscript; tools\check_scripts.ps1 automatizza il
  controllo (provato anche con un errore voluto); 0 errori nei file della mod (errori solo in Codeware, attesi); script e DLL in dist (md5 verificati). Selettore Body / Face sotto Tools (sezione APM_Mode, sessione
  "FaceMode"); in Face spariscono le sezioni del corpo e c'è la sezione viso: intestazione (espressione caricata, L+R
  pref "FaceLinked", cartella, reset), 8 schede zona (sessione "FaceZone"), righe della zona (LayoutFace le
  riposiziona solo se cambia zona/L+R), "Reset zone" e libreria espressioni sotto. Righe bipolari:
  APM_ValueRow.pairAttribute + RowValue/SetRowValue. Catalogo righe/etichette/chiavi in FacePoseCatalog.reds
  (lips_lower_raise = "Lower lip down", verificato in anteprima). gen_loc_template senza filtro viso: 322 testi.
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
- 24/10: RED4ext 1.31.0 (2.5) installato nel gioco. In attesa: Codeware per 2.5 (installato 1.20.5, per 2.31).
  Nel gioco c'è ancora la DLL APM del 17/10 (non la 1.4.0 di dist).
  RED4ext 1.31.0 verificato su GitHub: minimo gioco 2.5, API v1 (la nostra), nessun controllo SDK massimo.
  SDK: dopo il nostro 0f5eba4 c'è 5246e65 (#232, tipi RTTI 2.5): Entity, IComponent, IPlacedComponent,
  ISkinableComponent, AnimatedComponent, Rig, QsTransform invariati -> aggiornamento non necessario.
  Codeware su GitHub: v1.20.6 (19/10) usa ancora l'SDK pre-2.5 (f9ad533); supporto 2.5 solo in PR non ufficiali
  aperte #63 (KelpHect, completa, richiede SDK #227+#228) e #64 (butrico31, rinomina i tipi in conflitto con
  prefisso Codeware). La 2.5 ha nativi CallbackSystem, ScriptableService, KeyInputEvent, AttachController ecc.:
  gli script APM non li usano (grep 24/10), quindi la scelta dei nomi non ci tocca.

- 24/10: build di test 1.4.0 per la 2.31 (APM_GAME_231, preset vs2022-231 / release-231, SDK ad72777): il codice
  compila con entrambi gli SDK (cambiano solo layout, non API; tipi generati usati identici). DLL in dist231\.
  Downgrade: Steam -> Proprietà -> Beta -> `2_31_old` (manifest 8416009761612439010); a mano: depot 1091501
  manifest 3389237510439361019. Sulla 2.31 servono RED4ext 1.30.x e Codeware 1.20.5.

## Fatto il 24/10 (feedback dal test su 2.31; build 2.5 e 2.31 ok, script ok, dist e dist231 aggiornati)
- Reset all azzera anche viso e oggetti in mano (SetHandProp 0 = oggetti della posa); riga Body/Face con "Reset" della
  sola parte (APM_ResetBody = PoseOverrides::ResetAll(true), APM_FaceReset). Tolti gomma e cartella dall'header Face.
- Libreria unica (sezione Pose): Body mode -> schede Corpo | Mani; Face mode -> espressioni (stessa vista, sotto la
  sezione viso). Voci interne in testa ("::t", "::a", "::preset:N": solo Carica): T/A pose e preset mani; tolti i
  pulsanti T/A/Hands dall'header. Tolta la riga dei preset nella scheda mano (i preset sono solo nella lista Mani).
  APM_FaceRename (NamedFiles::Rename condiviso con le pose mani). Tolto il vecchio selettore espressioni.
- Viso: pallino (AddDot/SetDot) sulle schede zona al posto del testo blu (righe senza segno, come nel corpo); 12 righe per pagina
  (APM_FaceRowsPerPage, < n/N > accanto a Reset zone).
- Gizmo: pulsanti icona nella riga Tools (mouse-pointer-2 / move / rotate-3d, box/globe per gli assi, Lucide 1.54.0
  rasterizzate dal browser: stroke 2, 8x8 campioni, opacità per eccesso a quarti = identiche alle esistenti);
  header: titolo largo 286, Pose/Gizmos spostati. Info: tre righe brevi per i gizmo.
- Voci interne della lista con il lucchetto (AddLock, m_poseLocks) al posto di "(built-in)".
- Tasti rapidi (hook di TextEntry, come Ctrl+Z/Y): Ctrl+1/2/3 = Direct/Move/Rotate, Ctrl+4 = assi (PollHotkey 3-6).
- Move: clic+trascina sul marker della parte selezionata = spostamento libero come Direct (m_centerDrag); il marker
  dello sguardo si trascina sempre.
- Rotazione ad anello oltre il limite: RotatePart applica solo la frazione dentro i limiti (bisezione, 10 passi).
- Marker e loro linee: opacità in base alla profondità dietro il più vicino (0-40 cm -> 100-30%, DepthFade), oltre
  all'attenuazione per sovrapposizione (dal 25/10 solo da vicino, vedi sotto).
- Talloni tolti (catalogo, JointLimits Heel, closeUp, APM_GizmoIsPartCloseUp, marker CloseUp). Nota: il tipo CloseUp
  valeva 3 come EyeLook, per questo il rombo non si vedeva.

## Sonda risolutore facciale (24/10, fase 1, da provare sulla 2.31)
- config.ini [Probe] FaceTracks=1 (letto all'avvio): aggancio a animAnimNode_Sermo::Sample (vtable via RTTI, slot
  0x128); durante la chiamata il nodo di input (+0x58) usa una copia della sua vtable con Sample = FaceInput_Sample,
  che logga le tracce float non nulle (nomi da animRig.trackNames, EngineLayout RigView.trackNames) e aggiunge
  FaceTrackWeight alla traccia FaceTrack (ricaricati a ogni apertura di Photo Mode). Log "FaceProbe:".
- Nel gioco: FaceTracks=1, FaceTrack=jaw_mid_open, FaceTrackWeight=0.5. Nei dist: FaceTracks=0.
- Fase 1 OK (log 24/10): tracce con i nomi delle pose, le espressioni di Photo Mode sono valori delle tracce, bocca
  aperta in modo naturale con jaw_mid_open +0.5. Dettagli in RESEARCH.md "Face poses".
- Fase 2 FATTA (24/10, build 2.31/2.5 ok, script ok, nel gioco): hook Sermo sempre attivo ([Face] GameSolver=1,
  riconoscimento: "48 8B 49 58" nei primi 64 byte); FacePoses::ApplyTracks (mappa posa -> traccia per rig, somma
  limitata 0-1 o sostituzione) e FacePoses::Apply (ossa) saltato per le facce passate dal risolutore nell'ultimo
  secondo; pose solo-traccia aggiunte al catalogo (lips_together_up/dn, lips_tighten_up/dn, jaw_mid_close/clench) con
  righe nuove; "From Photo Mode" (APM_FaceCopyGameExpression): copia l'espressione letta dalle tracce e la sostituisce
  (PoseOverrides faceReplace, in undo, salvato come "replace" nel JSON); i reset in quel modo ridanno i valori di
  Photo Mode (FacePoses::ResetWeight). Nel config del gioco FaceTracks=1 solo per il log (peso 0).

## Feedback test fase 2 (25/10)
- Crash all'uscita dal gioco (25/10 00:11, 5 s dopo l'uscita da Photo Mode): Cyberpunk2077.exe+0x2F0892, lettura a 0,
  nessuna funzione della mod nello stack. Se ricapita: provare [Face] GameSolver=0.

## Fatto il 25/10 (build 2.5 e 2.31 ok, script ok, dist, dist231 e gioco aggiornati: DLL 2.31 + script)
- Viso assoluto: FaceWeights con FaceUnset (-1) = posa non toccata (tolto faceReplace, CopyGameExpression, ResetWeight,
  APM_FaceCopyGameExpression / APM_FaceIsReplacing e il pulsante "From Photo Mode"). Risolutore: posa fissata = valore
  nella traccia, le altre restano del gioco. Riserva ossa: somma come prima. Slider = FacePoses::ShownValue (fissata,
  altrimenti GameValue = espressione di Photo Mode letta dalle tracce al primo frame, di nuovo 0,5 s dopo e 0,5 s dopo
  il cambio nel menu, poi ferma). Pallino = IsEdited (fissata e diversa). Reset zone / Reset face = FaceUnset.
  Valori sopra 1 delle mod mostrati (es. 150) finché non si tocca la riga (poi max 100); tracce di controllo NON
  toccate (deciso: niente limite e niente riga Intensità).
- Cambio espressione nel menu (28 per V, 56 per l'NPC scelto con 68): FacePoses::OnExpressionChanged (indice iniziale 0)
  -> PoseOverrides::ResetFaceOf (checkpoint nella cronologia di quel personaggio) e rilettura dell'espressione.
- Espressioni salvate formato 3: tutte le pose (valori assoluti, max 4 al caricamento); la 2 non si carica più.
- Pose senza traccia nel rig: sulle ossa anche quando il risolutore prende il viso (log "Face poses: face rig ... (on
  the bones: ...)"). Testa per le ossa: pma per V maschile e per gli NPC con GetResolvedGenderName = Male
  (PhotoModeTracker::IsMalePuppet, log "added character gender"), altrimenti pwa.
- UI: riga Body/Face a due pulsanti larghi; "Reset body" nell'intestazione nuova della sezione APM_Part (titolo Body,
  sezione 196 alta); "Reset face" nell'intestazione Face al posto di From Photo Mode; numero di pagina 22 px accento.
- Sonda occhi: con [Probe] FaceTracks=1 log "FaceProbe: character N eyes after the solver" (gradi, quando cambiano
  di 0,5°, max 2 al secondo), insieme alle tracce non nulle (eye_*_dir_*) già loggate.

## Test 25/10 mattina (log advancedposemod-2026-10-25-10-45-19) e modifiche dopo
- Sonda occhi: le tracce eye_*_dir_* muovono occhi e palpebre (lineari: 1 = 30° giù, 21° su, 40° di lato; dn 0,5 ->
  15,3°); Guarda la camera usa le stesse tracce. FATTO: lo sguardo del gizmo / occhi verso la camera va nelle tracce
  (FacePoses::SetGaze da ProcessSecondarySkeleton, un frame dopo; EyesEditedByUs), EyelidFollow saltato per quei visi
  (SolverTakesGaze).
- Johnny = personaggio 1 (genere letto "Female": non affidabile, resta pwa); tracce ricevute (lips_l_corner_up 1.00)
  ma viso fermo. FATTO: controllo automatico per viso (OnSolverInput / OnSolverOutput / OnFinalFace: ossa delle pose
  prima e dopo il risolutore e a fine grafo, con una traccia >= 0,2; 30 frame senza risultato -> ossa per quel viso,
  log "the face ignores the facial solver").
- UI: tolti Reset body / Reset face dalle intestazioni (e l'intestazione Body); il cestino resetta la parte aperta
  (Body: posa + oggetti in mano, Face: viso), doppio clic; tolto Reset zone (e APM_FaceResetZone); nelle righe del
  viso il pulsante 0 è un'icona reset (valore di Photo Mode, anche la posa opposta delle righe bipolari); pagine
  < n/N > (sinistra, centro, destra).
- Cambio espressione nel menu: l'undo (solo se c'erano modifiche) ridà il viso intero di prima. Niente messaggi.
- Altri NPC provati: espressioni già funzionanti.

## Test 25/10 pomeriggio (log advancedposemod-2026-10-25-11-21-13): tutto ok
- Sguardo nelle tracce, Johnny sulle ossa, cestino, reset per riga, pagine: funzionano.
- Log: V e NPC "the facial solver poses the face"; Johnny (personaggio 1) ha due componenti viso: due "poses the
  face" e uno "ignores the facial solver (it does not move the face)" -> quello passa alle ossa.

## 25/10/2026: priorità dei marker sovrapposti (ResolveMarkerOverlaps) - da provare in gioco su 2.31 (dist231)
- Causa: copertura con raggio fisso 24 px (da vicino le dita della mano dietro escono dai 24 px dei marker della mano
  davanti -> di nuovo pieni); da lontano molti marker entro 24 px con 3 cm di differenza si attenuavano, e
  behindFade partiva dal marker dello sguardo (0,6 m davanti agli occhi) -> tutto il corpo attenuato.
- Ora: fattore di vicinanza dal marker più vicino (sguardo escluso): pieno < 1,0 m, nullo > 1,5 m; moltiplica
  coverFade e behindFade (da lontano nessuna attenuazione). Raggio di copertura = 3 cm proiettati alla profondità
  del più vicino (minimo 24 px), cerchio; ordine di disegno con metà raggio. Soglie 0,5-3 cm e 40 cm invariate.
- Valori da tarare in gioco (1,0/1,5 m, 3 cm). Le mod richieste per la 2.5 sono uscite: poi aggiornare e provare.
- Prova utente 2.31: funziona; dita della stessa mano troppo nascoste -> raggio 2 cm, attenuazione 1 -> 4 cm.
- Lista viso: 13 righe per pagina (Lips in 2 pagine invece di 3).
- Oggetti in mano nella cronologia (C++: EditState propRight/propLeft, ResetAll li azzera -> Reset Body + annulla li
  rimette in un passo; checkpoint script prima di scegli/svuota/ripristina oggetto).
- Salvataggio posa: se la mano tiene l'oggetto della posa di Photo Mode (prop 0), si salva quello
  (SavePoseWithItems imposta il prop, salva, lo rimette a 0). "Pistol Trumpet.json" (9/10) non ha "props": va
  risalvata. DLL ricompilate (2.5 e 2.31); provato in gioco su 2.31: funziona.
- 25/10: gioco aggiornato alla 2.5 con RED4ext 1.31.0, Codeware 1.21.0, ArchiveXL 1.28.0, TweakXL 1.12.0,
  redscript 0.5.32, CET 1.38.0. DLL 1.4.0 (dist) e script copiati nel gioco; check_scripts sul bundle 2.5: 0 errori
  (anche dentro Codeware). Da fare: primo avvio, lettura log, test in gioco (lista sotto) e veicoli.
- Primo avvio 2.5: crash su ACU (acu_rs.dll non aggiornato, disattivato). Poi errori script: r6\cache\modded era
  della 2.31 (30/08). Il deploy REDmod 2.5 fallisce alla compilazione script con gli script di CDPR stessi
  (tools\redmod\scripts\mods\codeware: "ISerializable must derive from something") appena una mod REDmod ha script
  (FastTravelFromAnywhereToAnyMapPin). Deploy riuscito con -modlist (ordine Vortex, senza quella mod, file senza
  BOM): modded ora 2.5. AdvancedPoseMod 1.4.0 sulla 2.5: hook, layout, AnimGraph, viso, API ok nel log.
- More Occluders disattivata (occluderType sparito nella 2.5): script ok. Crash al caricamento del salvataggio
  (Cyberpunk2077+0x20be897, thread redDispatcher, array con base sporca) anche SENZA AdvancedPoseMod e senza
  tweakdb_ep1.str vecchio (copia in scratchpad della sessione): causa in altre mod. Sospetti: mod che modificano
  settori del mondo (ArchiveXL: Kabuki Diner, Charter Hill Removals, sector count diversi nella 2.5).
- Profilo Vortex "2.5 APM" (solo RED4ext 1.31, Codeware 1.21, TweakXL 1.12, CET 1.38, redscript 0.5.32): il
  salvataggio si carica, AdvancedPoseMod 1.4.0 FUNZIONA sulla 2.5 (log senza errori/avvisi, script ok, Photo Mode,
  rig 71/181/56/344 ossa, viso 93/93 pose e solver, sguardo). Non salvare in quel profilo. Il profilo completo
  resta per la 2.31 (ramo beta, mod 2.31, DLL dist231, deploy REDmod).
- Test dell'utente sulla 2.5: funziona tutto. Veicoli rimandati alla release successiva.
- Release 1.4.0 preparata: release\AdvancedPoseMod-1.4.0.zip (2.5) e AdvancedPoseMod-1.4.0-game2.31.zip (2.31),
  changelog in release\CHANGELOG-1.4.0.md; README (requisiti per versione, build 2.31). Pubblicazione su Nexus e
  GitHub a cura dell'utente.
- Ritorno alla 2.31 per giocare (profilo Vortex completo): RED4ext 1.30, Codeware 1.20.5, ArchiveXL 1.27.4,
  TweakXL 1.11.4, CET 1.37.1, DLL dist231. Deploy REDmod 2.3 riuscito (480 mod, ordine dal load order Vortex,
  servono -rttiSchemaPath=<gioco>\tools\redmod\metadata.json e lancio dalla cartella del gioco, file senza BOM).
- 29/10: requisiti su GitHub: RED4ext 1.31.0 e redscript 0.5.32 invariati; SDK ultimo 5246e65 (già valutato);
  Codeware 1.21.2 (settori/nodi del mondo, eventi widget ink scriptati, inkWidgetLibraryResource: la mod usa solo
  RegisterToCallback vanilla); CET 1.38.1 (solo SDK); ArchiveXL 1.28.6 e TweakXL 1.12.0 non richiesti. Nessuna
  modifica necessaria; README può citare Codeware 1.21.2+.
- 29/10: analisi (piano in attesa di ok): occhi fermi con Guarda in camera = g_gazes non scade (ApplyTracks scrive
  sempre, SetGaze solo se la posa ha modifiche); rischi crash (vtable Sermo 96 voci, subclass finestra TextEntry,
  rig letto dopo despawn, runtime INDEPENDENT, detour senza try/catch) + gestore eccezioni con log; libreria a
  cartelle (ListFolder, Built-in in cima, ←, icone folder/arrow-left; sovrascrittura in sottocartella da
  verificare); veicoli: prima prova in gioco con Log=1/DumpRig=1.
## Fatto il 29/10 (build 2.5 e 2.31 ok, DLL in dist e dist231, NON ancora nel gioco né provate)
- Occhi fermi con Guarda in camera: Gaze.setAt, sguardo ignorato dopo 250 ms senza SetGaze (ApplyTracks e
  SolverTakesGaze); SetGaze solo dal viso con ossa degli occhi (`eyes`, Johnny); log Verbose "gaze from the editor/game".
- CrashGuard (src/Core/CrashGuard.*): DLL bloccata in memoria (PIN), gestore eccezioni vettoriale che scrive
  red4ext\plugins\AdvancedPoseMod\crash.log (modulo+offset, thread, fase) solo se il crash è nel plugin o in una fase
  della mod (Stage), max 8 righe; BeginShutdown all'Unload (i detour passano solo al gioco); Guard = try/catch nei
  detour (Output, input del risolutore, Photo Mode start/end/spawn/update, attributi del menu).
- Sermo: vtable copiata a 256 voci (riserva 96), scambio e ripristino in SampleWithFakeVtable con __try/__finally.
- TextEntry: WindowProc con originale nullo -> DefWindowProcW; Shutdown non azzera più g_originalProc.
- Main: nativi non registrati se la DLL è per l'altra versione del gioco (ScriptLayoutMatchesGame, errore nel log).
- Da provare: sulla 2.31 (dist231) Guarda in camera + modifica + annulla/Reset (occhi seguono); Johnny; uscita dal
  gioco con Photo Mode aperto; DLL 2.5 sulla 2.31 -> errore chiaro, niente crash (redscript segnala i nativi mancanti).
- Libreria a cartelle (script ok, build ok, dist/dist231): la lista mostra una cartella alla volta (m_libraryAll =
  tutti i file, BuildEntries -> "::up" (".." con freccia), "::builtin/" (cartella Built-in in cima, T/A e preset mani),
  sottocartelle "Nome/" con icona folder, poi i file col solo nome); clic su cartella = apre, cartella aperta per
  libreria (m_libraryFolders) e nel titolo ("Poses / Cartella 1/2"). Salva nuovo nella cartella aperta (nativi
  APM_SaveNewPose/APM_SaveNewHandPose/APM_FaceSaveNew con folder, NamedFiles::FreeName); OpenFolder apre la
  sottocartella; rinomina mostra solo il nome. FIX: sovrascrivere una posa del corpo in sottocartella creava
  "Cartella_Nome" nella principale (Save usa NamedFiles::SanitizeInFolder). Icone folder e arrow-left (Lucide,
  rasterizzate con lo stesso metodo: chevron-up identica). Testo nuovo APM-Folder-BuiltIn. 5 righe per pagina;
  niente crea/rinomina/elimina cartella; cartelle vuote non visibili (la lista viene dai file).
- Limite viso sbloccabile (richiesta utenti): lucchetto nell'intestazione Face (face:unlock "lock" / face:lock
  "lock-open" in colore di avviso, pref "FaceUnlocked"): righe fino a 400 (bipolari -400..400, FaceRowMax /
  SetFaceUnlocked), nativo APM_FaceSetUnlocked -> PhotoModeUi g_faceUnlocked (clamp 4 invece di 1). Risolutore: valore
  passato così com'è; ossa (Johnny): oltre l'ultimo inbetween la posa continua allo stesso ritmo (Slerp con t > 1).
  Richiudendo, i valori sopra 100 restano finché non si tocca la riga. Testi APM-Tip-FaceUnlock / FaceLock.
- 30/10 dopo la prova (log advancedposemod-2026-10-30-00-29-09): FIX limite 400% che tornava a 100 al rilascio
  (PoseOverrides::SetFaceWeight tagliava a 1, ora 4). Libreria: pagine in una riga sotto la lista (< n/N >, come il
  viso; posesBody 208, sezione 252); titolo = solo nome + cartella aperta (tolto il nome della posa caricata e la pagina).
- Veicoli, prova 30/10: sessione 1 a piedi (ok). Sessioni 2 e 3 = auto e moto con camera esterna, personaggio
  visibile e animato, ma slot 0 puppet=0: sul veicolo Photo Mode non crea il puppet e mostra il giocatore stesso.
  FATTO: APM_SetPlayerEntity (script, ogni frame con Photo Mode attivo: GetPlayerControlledObject) ->
  PhotoModeTracker g_playerEntity; RefreshTargets, se slot 0 non ha puppet, posa il giocatore come V (se
  IsHumanCharacter); log "no Photo Mode puppet (vehicle): V is the player character itself". Azzerato a Deactivate.
  Da provare: auto e moto, corpo, viso, T pose, IK, marker; uscita da Photo Mode (il giocatore torna normale).
- Prova 30/10 00:51 (sessioni 2-3 su veicolo): il giocatore arriva ma IsHumanCharacter fallisce ("player has no human
  skeleton"). Aggiunto log Info una volta per sessione: classe dell'entità e scheletri (nome(ossa)) -> da riprovare.
- Prova 30/10 01:08: sul veicolo il giocatore è PlayerPuppet con root(152) deformations(183) shadow face_rig(344)
  hair_dangle breasts: IsHumanCharacter voleva root == 71. FATTO: parametro aLargerRig (root >= 71 + ossa richieste)
  solo per il giocatore. NPC aggiunto sul veicolo: comparso lontano (posizione del gioco).
- Espressioni oltre 100: CONFERMATO limite del risolutore (FaceProbe: tracce a 4,00 / 3,41, nessun effetto oltre 1).
  FATTO: FacePoses::Apply per le pose con traccia applica sulle ossa solo la parte oltre l'ultimo inbetween
  (conj(R1)*R(w), T(w)-T1, delta delle teste JSON pwa/pma: approssimati per la testa del personaggio).
- Da provare: veicolo (auto/moto: marker, rotazioni, T pose, viso), espressioni a 200-400% su V, NPC e Johnny.
- Crash utente, precisazione 30/10: NON su veicolo (appartamento, stress test pose, niente oggetti in mano); 0,35 s
  dopo "deactivated", Entity::Detach+0x75 su worker, RBX = entità del personaggio 0 già liberata (primo qword non è
  una vtable). Ipotesi (sua): riferimento forte della mod ultimo proprietario. Verificato: riferimenti bilanciati e
  nessuno tenuto dopo l'uscita; MA RefreshTargets (ApplyPuppetTransforms, thread del gioco) faceva LockAlive di
  entità e di tutti i componenti (161) a ogni chiamata. FATTO: ricostruzione solo se cambia la firma letta senza
  riferimenti (ReadTargetSignature: puppet della lista, giocatore, slot scena, EngineLayout::ComponentsSignature =
  indirizzo+dimensione dell'array componenti, sessione), finché manca la root (rig in caricamento) o ogni 2 s.
- Prova 30/10 (veicoli ok: ossa ed espressioni). Problemi: sul veicolo arriva 15=1 (probabilmente una mod che
  ripristina le impostazioni; il gioco non gira il giocatore) -> la mod girava testa/occhi (KeepLookAt, occhi) e
  bloccava quelle ossa; V animato sul sedile. FATTO: PhotoModeTracker g_playerPosed / IsPlayerPosed (V = giocatore
  sul veicolo) -> LookAtCamera::g_ignored[0] (Reach 0, AimsAtCamera/IsCustom false); AnimGraphHook FreezeAnimation:
  con posa attiva (PoseOverrides::IsEnabled(0)) root/deformations/shadow prendono il fotogramma salvato all'accensione
  (viso, capelli, seno liberi); g_frozen svuotato a ogni attivazione. Log "V is the player in a vehicle ...".
- Prova 30/10: Guarda in camera ok; animazione ferma ma la posa non si vedeva (gizmo sì): deformations/shadow
  prendono le ossa della root nei loro grafi, congelarli nascondeva le modifiche -> ora si congela solo la root.
  Sovrascrivi con conferma al secondo clic come Elimina (m_confirmOverwrite, testo APM-Button-ConfirmOverwrite).
- Prova 30/10: blocco animazione e posa sul veicolo ok. Arma messa in mano sul veicolo restava al giocatore dopo
  Photo Mode (e diventava l'arma del veicolo). FATTO (solo script): PlaceProp sul giocatore (IsPlayer) non toglie mai
  oggetti dal suo inventario, ricorda cosa aveva in ogni mano (NotePlayerHand) e gli oggetti dati (m_playerGivenItems);
  RestorePlayerHands all'uscita (OnTryExitPhotomode / OnExitConfirm, riserva in SyncProps): svuota le mani toccate,
  toglie gli oggetti dati, rimette quelli di prima. Provato: funziona.
- POSE ON ricordato (deciso con l'utente: stessa logica a piedi e sul veicolo): PhotoModeTracker salva Pose on/off di
  V all'uscita (RememberPoseOn in OnExitRequested e Deactivate, pref "PoseOn") e lo rimette all'apertura
  (ActivateSession, PoseOverrides::SetEnabled(0, ...), default acceso). Sul veicolo ferma subito l'animazione; a
  piedi senza modifiche non cambia la posa. Nativo APM_IsPlayerPosed (per ora non usato). Da provare.
- Crash report utente (Desktop\CrashReports, 30/10 02:15): Epic 2.31, DLL 1.4.0 del 25/10, PhotoModeEx 1.4.1,
  RedHotTools; crash 0,6 s dopo l'uscita da Photo Mode su veicolo (Herrera Riptide, MountState Mounted), thread
  redDispatcher, ent::Entity::Detach+0x75, lettura 0xFFFF..., nessun frame di mod (AdvancedCrashReporter). Sul suo
  gioco Photo Mode su veicolo AVEVA il puppet (targets registrati). Simile al crash del 25/10 dopo l'uscita.
- Veicoli: log "PhotoMode: player list slot N puppet=... type=..." (Verbose, quando cambia) e "V has no puppet / no
  animated components" (Info, una volta per sessione); RESEARCH.md "Vehicles" aggiornato. Prova in gioco da fare.
## Guarda in camera: occhi e testa (30-31/10, committato e pushato, provato in gioco: ok)
Gioco installato: 2.31 (3.0.80) -> copiare build231 nel gioco (la DLL 2.5 lo fa crashare all'avvio).
File toccati: src/Pose/PoseProcessor.cpp, src/Game/AnimGraphHook.cpp.
DLL copiate in dist, dist231 e nel gioco. Script invariati.
- Scoperto: la mesh degli occhi (he_*.mesh) è skinnata al face_rig (Head, l/r_J_eye_JNT, pupil, wetness, palpebre).
  Il gioco gira la Head del face_rig per conto suo con Look at camera (30-65 gradi dalla Head della root su una posa
  salvata; posizione uguale, pose di riferimento identiche).
- Fatto:
  - AimFaceRigEyesAtCamera: occhi mirati nel face_rig (dalla sua testa, "avanti" = occhio->pupilla a riposo),
    limiti del gioco 40 lati / 21 su / 30 giù (FaceAxes, KeepInEyeRange). Prova lying: ok.
  - FollowMainNeck: Spine3/Neck/Neck1/Head del face_rig copiano quelli finali della root (g_mainNecks, salvati in
    AimEyesAtCamera) mentre la mod mira alla camera.
  - AnimGraphHook: Look at camera (Fotocamera, Testa/Torace) gestito dalla mod anche senza modifiche (POSE ON).
  - KeepLookAt: limite misurato dal corpo, lati e su/giù separati (ToAngles/FromAngles): Testa 60/35/45 dal petto,
    Torace 120/40/50 dal bacino; petto/bacino ricavati dalla forma (BodyAxes: Spine2->Neck + LeftArm-RightArm,
    Hips->Spine1 + cosce); collo/testa con TurnFaceTo (testa dritta: alto del viso verso l'alto del petto), quote
    Neck 0.35 / Neck1 0.5 / Head 1.
  - Movimento graduale (SmoothLook, molla critica): testa 0.4 s, occhi 0.12 s; reset dopo 0.3 s di pausa.
- Log diagnostici tolti (31/10); FacePoses.cpp torna uguale al commit.
- Collo torto sulla posa del PM pack (31/10, da provare in gioco): BodyAxes era giusto. Causa: sulle pose di Photo
  Mode KeepLookAt partiva dalla testa già girata dal Look at camera del gioco (camera dietro -> ~141 gradi), e la
  rotazione più corta verso il lato opposto (-60) passava dietro il collo (twist -263/114/95). Fix: (A) in
  ProcessMainSkeleton, se RemoveGameLookAt riesce e TakesOverLookAt, le ossa di LookAtChain ripartono dalla posa senza
  il giro del gioco; (B) in KeepLookAt, Neck/Neck1/Head: se la svolta più corta attraversa la schiena, ogni osso fa la
  sua quota passando davanti (max ~160 gradi). Build 2.5 e 2.31 ok, DLL in dist, dist231 e gioco (2.31).
  Da provare: PM pack con camera dietro a destra/sinistra, T-pose, laying_1, posa PM con Look at camera acceso prima
  di sceglierla (lì A non agisce, resta B).
- Limiti dopo il confronto con CDPR (31/10, posa Azione; da provare): Testa su 35 -> 60 (CDPR alza molto di più);
  Torace lati 120 -> 90 (il nostro girava troppo), su 40 -> 65. Giù invariato (45 / 50). Build 2.5 e 2.31 ok,
  DLL in dist, dist231 e gioco.
- Accensione/spegnimento graduale (31/10, da provare; build 2.5 e 2.31 ok, DLL in dist, dist231 e gioco): LookBlend
  per personaggio (peso 0-1 in 0.4 s, smoothstep, reach ricordato durante la dissolvenza). KeepLookAt mescola le ossa
  della catena tra la posa in ingresso e la girata; occhi (AimEyesAtCamera, AimFaceRigEyesAtCamera), FollowMainNeck,
  EyesEditedByUs e i link usano HeadLookShare. AnimGraphHook continua a posare finché ShowsLookAt (anche senza
  modifiche). Warning C4100 aContext sistemato (restano due C4100 'aOut' preesistenti in ScriptApi.cpp 87 e 155).
  Provato: ok. Scatto Torace <-> Testa corretto (31/10, da provare): LookBlend.torso (0-1 in 0.4 s), catena unica
  Spine1..Head con le quote della schiena moltiplicate per torso; limiti dal petto (testa) e dal bacino (torace)
  mescolati per torso, smorzamento sempre nel frame del petto della posa in ingresso.
- Marker degli occhi che balla con Guarda in camera spento (31/10, corretto, da provare; script e DLL in dist,
  dist231 e gioco): incrocio usato solo se le linee convergono (denom > 3e-4, ~1 grado), altrimenti resta l'ultima
  distanza; distanza addolcita (0.2 per frame) in EyeLookPoint (m_eyeLookDistance) e in GetEyeFocus
  (g_shownEyeFocus, riallineato da SetEyeFocus e dal reset). Provato: ok, ma al rilascio il marker si spostava
  (31/10, corretto, da provare): MoveEyeLook salva la distanza del punto trascinato (m_eyeLookDistance) e la fissa
  (m_eyeLookPinned) finché gli occhi non cambiano direzione di oltre ~5 gradi (m_eyeLookPinForward, spazio modello);
  anche il marker del singolo occhio usa quella distanza invece di 0.6 m. Provato: ok.
- Animazione ferma anche a piedi con POSE ON (31/10, da provare; build 2.5 e 2.31 ok, DLL in dist, dist231 e gioco):
  FreezeAnimation (AnimGraphHook.cpp) per ogni personaggio con POSE ON, solo lo scheletro "root" (shadow e
  deformations prendono le sue ossa; viso, capelli, giacca restano animati/fisica). Cambio di posa di Photo Mode
  (PoseGeneration): fotogramma preso dopo 0.6 s (anche all'accensione a piedi; nel veicolo subito). Le ossa del Look at
  camera del gioco (custom: anca, cosce, schiena, collo, testa, occhi; solo occhi: occhi) restano animate.
  POSE ON: V parte acceso la prima volta (preferenza PoseOn, default 1) poi resta com'era; i personaggi aggiunti si
  accendono con la prima modifica o posa caricata. Provato: ok.
- Guarda in camera Torace su pose già girate (Azione di CDPR: middle back girato troppo, 31/10, da provare; build 2.5
  e 2.31 ok, DLL in dist, dist231 e gioco): KeepLookAt riscritto. La schiena gira dal petto (BodyAxes), non dalla
  testa: petto al massimo 45 gradi dal bacino (mai indietro da dove la posa lo ha già), attorno all'asse del bacino,
  quote fisse Spine 0.3 / Spine1 0.25 / Spine2 0.25 / Spine3 0.2, ognuna con JointLimits::LimitLocal (come le
  modifiche; segue il lucchetto dei limiti). Poi testa limitata dal petto girato (60 lati / 60 su / 45 giù) e collo /
  testa come prima. Smorzamento sulla direzione della camera (nel frame del petto della posa) prima dei limiti.
  Provato: ok. Poi (31/10, da provare): con la camera dietro torace e testa scattavano dall'altra parte (la direzione
  rimisurata nel frame del bacino / del petto girato cambiava lato prima): ora i limiti partono dagli stessi angoli
  smorzati (lookSide + lato del petto sul bacino; testa: lookSide - giro reale del petto). SmoothLook con aBehind:
  resta dal suo lato finché la camera non supera di 25 gradi il dietro, poi passa davanti. Giro più lento:
  LookTurnSeconds 0.4 -> 0.6 s (occhi 0.12, accensione / spegnimento e cambio Torace-Testa restano 0.4 s).
  Provato: ok. Poi (31/10, da provare): dal basso CDPR si muove molto di più -> la schiena si piega anche su / giù
  verso la camera (max 15 su / 20 giù dal bacino, attorno all'asse laterale del bacino, stesse quote e limiti),
  testa giù 45 -> 55. Dall'alto il torace girava di lato con la camera davanti (angolo laterale da un offset
  orizzontale minuscolo): lookSide ridotto di cos(pitch) / cos(pitch raggiungibile), quasi zero con la camera
  sopra. Lower back (Spine) ora bloccato nell'editor con Guarda in camera Torace (IsLookAtLockedBone).
  Piedi diversi con POSE ON / OFF: probabile giro di anca e cosce del Look at camera Torace di CDPR (4-10 gradi),
  che la mod toglie; da verificare con Guarda in camera spento.
- Torace rifatto come quello di CDPR (31/10, da provare; build 2.5 e 2.31 ok, DLL in dist, dist231 e gioco). Dati
  dal grafo player_photomode.animgraph e dai preset TweakDB: vedi docs/RESEARCH.md ("How Photo Mode's Look at camera
  is built"). Angoli della camera visti dal personaggio (avanti del modello, in piano), non dal petto (con il petto già
  girato nella posa lo giravamo ancora, anche con la camera davanti). Anca 0.3 lati / 0.2 su-giù con le cosce tenute
  ferme nello spazio modello (gambe e piedi restano); schiena con quote e limiti di CDPR (lati Spine/1/2 0.2, Spine3
  il resto; limiti 7.5/15/15/7.5; su/giù fattori e limiti separati, giù fino a 70 in tutto), peso generale 0.5
  (suppress del preset; anca 4-10 gradi misurati). Lato che svanisce da 60 gradi su/giù. Niente spostamento
  dell'anca di 16 cm. Testa e occhi restano i nostri. Smorzamento del torace: SmoothLook parte 3.
  File di ricerca in %TEMP%\apm_lookat_research (da cancellare a fine lavoro, chiedere all'utente). Causa: EyeLookPoint
  (PoseEditor.reds ~3400) e GetEyeFocus (PhotoModeUi.cpp ~442) lo ricavano ogni frame dall'incrocio delle linee di
  vista degli occhi animati; con occhi quasi paralleli (denom = 1 - b^2 piccolo) l'incrocio salta di molti cm e
  scatta tra incrocio e ripiego (0.6 m / g_eyeFocus) alle soglie 0.15 / 4.0 / 0.05.
- Altri da fare: rendere graduale anche l'accensione/spegnimento di Guarda in camera (ora scatta); controllare se il
  gizmo degli occhi (AimPart / eye edit) ha lo stesso problema della testa del face_rig; attenzione warning
  C4100 'aContext' in AnimGraphHook.cpp:394 (preesistente).

- Torace solo dalla schiena, assi di CDPR (31/10, da provare; build 2.5 e 2.31 ok, DLL in dist, dist231 e gioco 2.31).
  Deciso con l'utente: anca e gambe fuori dal Guarda in camera (restano della posa e modificabili). Gli assi di CDPR
  sono quelli del personaggio (catena pla_*_off su pla_trajectory), non quelli dell'anca in posa: KeepLookAt gira
  Spine..Spine3 attorno alla verticale e all'asse laterale in piano, accumulati lungo la catena (con la posa
  accovacciata/piegata gli assi dell'anca storcevano il busto, Flex Down). Anca (0.3 / 0.2) tolta: Spine3 prende il
  resto del laterale entro 7.5. Fix POSE ON con camera bassa: il fotogramma congelato teneva anca abbassata e
  ginocchia piegate dall'IK del gioco; RemoveGameLookAt ora ricorda e rimette trasformazioni intere (anche posizione
  dell'anca) di LookAtChain + LeftLeg/RightLeg/LeftFoot/RightFoot. IsGameLookAtBone (Guarda in camera custom): anche
  ginocchia e piedi restano animati. Limite: posa di Photo Mode scelta con Guarda in camera già acceso -> nessun
  fotogramma pulito, la svolta del gioco resta. Da provare: Flex Down camera bassa/alta, POSE OFF/ON con camera bassa,
  piedi uguali con POSE ON/OFF, pose in piedi (Azione), custom.

## Prossimi passi
- Test in gioco (2.31, dist231) dei punti del 24/10: reset Body/Face/all (oggetti in mano), liste con voci interne,
  rinomina espressioni, pagine viso, pallini, icone gizmo, centro in Move, anelli al limite, opacità per profondità.
- Dopo il downgrade a 2.31: check_scripts.ps1 (bundle 2.31), test in gioco di dist231 (editor espressioni, FacePoses).
- Quando escono RED4ext/redscript/Codeware per 2.5: check_scripts sul bundle 2.5, test in gioco di tutto
  (soprattutto verso degli anelli, limiti talloni, log "Layout:"), test veicoli (piano in RESEARCH.md).
- In attesa: pose adattate all'altezza (rimandato dall'utente), area clic marker (non serve per ora).

## Note
- Cartella download Vortex: C:\Users\ObiOneStenobi\AppData\Roaming\Vortex\downloads\cyberpunk2077
- Gioco: C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077
