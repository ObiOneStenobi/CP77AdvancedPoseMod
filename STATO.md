# STATO – AdvancedPoseMod 1.4.0 (in corso)

Aggiornato: 2026-10-25

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
  all'attenuazione per sovrapposizione.
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

## Da provare in gioco (2.31)
- Palpebre / sguardo, fase A: config.ini del gioco [Probe] FaceTracks=1, FaceTrack=eye_l_dir_dn, FaceTrackWeight=0.5
  (poi 1, poi eye_l_dir_up / in / out); occhi dritti, Guarda la camera spenta; poi Guarda la camera sugli occhi muovendo
  la camera. Mandare il log: decide la fase B (direzione nelle tracce eye_*_dir_* oppure riserva EyelidFollow neutra
  con zona palpebre modificata).
- Johnny: con FaceTracks=1 il log dice quali pose del suo rig vanno sulle ossa e il genere letto; verificare se gli
  slider che "non facevano niente" ora si muovono.
- Viso: slider = espressione attuale, pallino, reset, cambio espressione nel menu (V e NPC) con undo, espressioni
  salvate intere, valori >100 delle mod.
- Verificare con Debug Log=1 se l'attributo 56 arriva anche selezionando un NPC (non solo cambiando espressione).

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
