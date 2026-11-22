# Book 8088 – liste de tests sur le vrai materiel (LPS COMPANION v1.16)

Tests a faire sur le Book 8088 (8088, ecran CGA) avec la version finale
LPS116-CHG07b. Les temps de reference viennent de DOSBox Staging a la
vitesse d'un 8088 (`cycles=fixed 300`, section 7) ; noter le
temps reel a cote. DOSBox Staging 0.83.1 n'accepte pas `cputype=8086`
ni `cputype=8088` et retombe sur `auto` : la vitesse est bien celle des
300 cycles (4,77 MHz). DOSBox lit le disque presque instantanement : sur le Book,
la premiere lecture d'un fichier prendra plus de temps.

## 1. Fichiers a copier

Dans un meme dossier (par exemple `C:\LPSCOMP\`), depuis la racine du depot :

| Fichier | Taille | Role |
|---|---|---|
| `LPSCOMP.EXE` | 111 564 o | programme |
| `ROSARY.DAT` | 10 217 o | textes du chapelet |
| `MEALS.DAT` | 199 964 o | images du planificateur de repas (EGA/VGA) |
| `MEALSCGA.DAT` | 49 996 o | images du planificateur de repas (CGA, LPS-DOS-115-CHG14) |

Optionnel : `HELLO.EXE` (2 776 o) pour un premier test de la machine.
Les donnees vont dans `C:\LPS\` (cree au premier lancement). Pour le test
aller-retour PicoCalc, copier aussi `SAVE_A.JSN` / `SAVE_B.JSN` de la carte SD.

## 2. Points generaux

| # | Test | Attendu | Ref. DOSBox | Resultat |
|---|---|---|---|---|
| 1 | Lancer `LPSCOMP` | banniere puis barre de titre **LPS COMPANION v1.16** | demarrage 0,77 s (petite sauvegarde) | |
| 2 | Vitesse des touches : Bas sur l'accueil, cocher une activite, deplacer une tache Kanban | reponse sans attente visible, aucune touche perdue en tapant vite | Bas 0,17 s ; cocher 0,11 s | |
| 3 | Chargement JSON au demarrage (sauvegarde courte, puis sauvegarde de 31 jours) | accueil affiche, donnees presentes | 0,8 s / 3,1 s | |
| 4 | Ecriture JSON : quitter Activites (Echap), Config > 4 Enregistrer | pas d'erreur, `SAVE_A.JSN`/`SAVE_B.JSN` alternes | 0,7 s / 3,2 s (31 jours) | |
| 5 | `KEYB FR` (AZERTY) | chiffres de la rangee du haut sans Maj (& é " ' ( - è _ ç à) sur l'accueil/Config ; taper é è ç à dans une tache Kanban | – | |
| 6 | « Neige » CGA sur les ecrans texte (accueil, Kanban, Revue journaliere) en defilant vite | noter s'il y a des parasites ; aucun attendu en 320×200 | – | |
| 7 | Tab / Espace depuis chaque ecran, Q pour quitter puis relancer | Config/planificateur s'ouvrent et se referment ; donnees conservees | – | |

## 3. Planificateur de repas en CGA (ecran du Book, LPS-DOS-115-CHG13/14/15)

| # | Test | Attendu | Ref. DOSBox | Resultat |
|---|---|---|---|---|
| C1 | Espace depuis l'accueil | 320×200 4 couleurs ; carte avec son image tramee (`MEALSCGA.DAT`) ; titre, compteurs et pied de page sur 40 colonnes, accents lisibles (Entrée, Échap) | 0,55 s | |
| C2 | Gauche/Droite sur les 16 cartes (Haut/Bas pour changer de rangee) | chaque image s'affiche ; noter le temps de la premiere vue d'une carte (lecture disque) puis d'une carte deja vue | nouvelle 0,23 s ; deja vue 0,21 s | |
| C3 | Entree : jouer, puis Entree : remettre | jouee : toute la carte grisee en damier, PLAYED #n / JOUÉE #n net sur un cadre plein ; remise : carte redessinee comme avant | jouer 0,22 s ; remettre 0,28 s | |
| C4 | Aller sur une carte deja jouee | carte dessinee puis grisee | 0,33 s | |
| C5 | Theme Bleu (Config > 2 Couleurs) puis jouer une carte | damier vers le bleu, JOUÉE #n lisible ; noter si le damier scintille ou se lit mal sur l'ecran du Book | – | |
| C6 | Tab vers Config puis Tab retour, puis Echap | retour propre au mode texte (pas de clignotement, curseur cache, couleurs du theme), puis au 320×200 | Tab 0,17 s / retour 0,39 s ; Echap 0,72 s | |
| C7 | Renommer `MEALSCGA.DAT`, ouvrir le planificateur | reste en 320×200, cadres vides en pointilles, ligne 23 « MEALSCGA.DAT absent : pas d'images » ; remettre le fichier ensuite | – | |

## 4. Autres ecrans (si disponibles)

| # | Test | Attendu | Ref. DOSBox | Resultat |
|---|---|---|---|---|
| E1 | EGA/VGA couleur : Espace, Gauche/Droite, Entree | images EGA ; carte jouee en gris (palette), JOUÉE #n | ouverture 0,50 s ; carte 0,24 / 0,14 s ; jouer 0,11 s ; Tab 0,17 / 0,33 s | |
| E2 | EGA sans `MEALS.DAT` | planificateur en mode texte, message, pas de plantage | – | |
| M1 | Ecran mono (MDA/Hercules) | planificateur en mode texte (« No EGA/CGA colour screen: Meal Planner in text mode »), PLAYED #n dans le cadre | ouverture 0,17 s ; touche 0,09–0,11 s | |

## 5. Focus du Kanban (LPS116-FEAT01)

| # | Test | Attendu | Ref. DOSBox | Resultat |
|---|---|---|---|---|
| K1 | Kanban : F sur une carte A FAIRE ou EN COURS | un `*` s'affiche devant le nom ; F a nouveau l'enlève | – | |
| K2 | Mettre une carte en focus, Echap (sauvegarde), Q, relancer | la carte est toujours en focus (`*` et `"focus":true` dans le JSON) | – | |
| K3 | F3 pour passer une carte en focus vers FINI ; F sur une carte FINI | le focus disparait (plus de `*`) ; F sur FINI ne fait rien | – | |

## 6. Revue journaliere (LPS116-FEAT02)

| # | Test | Attendu | Ref. DOSBox | Resultat |
|---|---|---|---|---|
| R1 | 5 depuis l'accueil (jour en cours) | date complete et semaine ISO (`Monday 16 November 2026 - Week 47` / `Lundi 16 novembre 2026 - Semaine 47`) ; titre **DAILY REVIEW** / **REVUE JOURNALIÈRE** sans `YYYY-MM-DD` ; poids `Actual weight: … Kg` / `Poids effectif : … Kg` ou `-- Kg` ; section KANBAN avec les cartes en focus (ou `No focused card` / `Aucune carte en focus`) ; compteurs Boissons/Nourriture/Blank 6/8/2 et `Next:` / `Prochain :` (premiere Nourriture puis Boisson, ou `--`) ; activites cochees | ouverture, cas 67 lignes : 0,30 s (EGA), 0,29 s (CGA), 0,28 s (Hercules) | |
| R2 | Liste plus longue que 21 lignes : Haut/Bas, PgUp/PgDn, Debut/Fin, Gauche/Droite | une ligne sans boucler ; page de 21 ; Debut en haut, Fin en bas ; fleches CP437 ↑/↓ a droite ; pied `Up/Down: scroll` / `Haut/Bas : défiler` et la plage (`1-21/67`) seulement s'il faut defiler | un pas Bas : 0,11 s (EGA, CGA, Hercules) | |
| R3 | Jour archive : entree **3 Revue journaliere** / **3 Daily review**, puis cette revue | le menu a 3 entrees (la PicoCalc v1.15 n'en a que 1–2) ; date du jour archive, bandeau, poids du jour, pas de KANBAN, repas en `--` (y compris Prochain), activites figees ; Echap revient au menu, curseur sur 3 ; Tab/Espace restent en lecture seule | – | |

## 7. Fichiers et aller-retour PicoCalc

| # | Test | Attendu | Ref. DOSBox | Resultat |
|---|---|---|---|---|
| F1 | Export ICS : terminer le jour | `C:\LPS\EXPORT\YYYYMMDD.ICS` lisible, identique a celui de la PicoCalc pour le meme jour | – | |
| F2 | CSV des poids : saisir un poids, terminer le jour | `C:\LPS\WEIGHTS\YYYY.csv` | – | |
| F3 | CSV du planificateur : jouer une carte, quitter (Espace ou Echap) | `C:\LPS\EXPORT\MPYYMMDD.CSV` | – | |
| F4 | Aller-retour PicoCalc : sauvegarde SD -> DOS, modifier (activite, carte), recopier sur la SD (sans carte en focus) | la PicoCalc v1.15 ouvre la sauvegarde ; entrees masquees (Config > Activites), activites personnalisees et cartes jouees conservees ; fichiers identiques a l'octet pres | – | |
| F5 | Sauvegarde DOS avec une carte en focus, ouverte sur la PicoCalc v1.15 | la PicoCalc ouvre le fichier (elle ignore `"focus"`) ; une reecriture sur la PicoCalc retire la cle | – | |

## 8. A noter

Modele exact (CPU, carte video), version de DOS, type de disque (CF, disque
dur), et tout caractere mal affiche (les capitales À È Ê Ô Â sont sans accent :
normal). Sur un jour archive, la revue n'a pas de KANBAN et les lignes de repas
sont toujours `--` : une sauvegarde des repas par jour est reportee.

## 9. Temps de reference DOSBox (LPS116-CHG07b)

DOSBox Staging 0.83.1, `cputype=auto`, `cycles=fixed 300`, sauvegarde courte ;
l'horloge avance par pas de 55 ms. Les temps du planificateur (CHG16) n'ont pas
ete remesures. Ceux de la revue journaliere (67 lignes, pire cas) l'ont ete
dans LPS116-CHG07b : duree jusqu'a la derniere modification de l'image
(`cycles=fixed 300`) ; une confirmation de 0,12 s n'est pas comptee.

| Action | EGA | CGA | Hercules (texte) |
|---|---|---|---|
| Demarrage jusqu'a l'accueil | 0,77 s | 0,77 s | 0,72 s |
| Accueil, Bas | 0,17 s | 0,17 s | 0,16 s |
| Activites, cocher | 0,11 s | 0,11 s | 0,10 s |
| Activites, Echap (sauvegarde) | 0,72 s | 0,66 s | 0,66 s |
| Espace, ouvrir le planificateur | 0,50 s | 0,55 s | 0,17 s |
| Carte pas encore vue / deja vue | 0,24 / 0,14 s | 0,23 / 0,21 s | 0,09 s |
| Entree, jouer / remettre | 0,11 / 0,11 s | 0,22 / 0,28 s | 0,11 s |
| Aller sur une carte jouee | 0,22 s | 0,33 s | 0,11 s |
| TAB → Config / → planificateur | 0,17 / 0,33 s | 0,17 / 0,39 s | 0,11 / 0,11 s |
| Echap, planificateur → accueil (sauvegarde + CSV) | 0,72 s | 0,72 s | 0,66 s |
| Ouvrir la revue journaliere (67 lignes) | 0,30 s | 0,29 s | 0,28 s |
| Un pas de defilement (Bas) | 0,11 s | 0,11 s | 0,11 s |

---

# Book 8088 – real-device checklist (EN, short)

Copy `LPSCOMP.EXE` (111,564 B), `ROSARY.DAT` (10,217 B),
`MEALS.DAT` (199,964 B) and `MEALSCGA.DAT` (49,996 B) into one folder; data
goes to `C:\LPS\`. Check against the DOSBox 8088-speed reference times (section 9
above; DOSBox disk reads are near-instant, so real first reads take longer):

1. Title bar **LPS COMPANION v1.16**; start-up 0.77 s with a short save, 3.1 s with 31 days.
2. Key speed: Home Down 0.17 s, a tick 0.11 s, no lost keys when typing fast; save on leaving Activities 0.7 s / 3.2 s.
3. CGA Meal Planner (the Book's screen):
   - Space opens it in 0.55 s, with a dithered picture on every card from `MEALSCGA.DAT`.
   - New card 0.23 s (note the real first-read time); a card already seen 0.21 s.
   - Enter greys the whole card with a checkerboard, with PLAYED #n on a solid box (0.22 s). Enter again redraws it normally (0.28 s). Check this with the Blue theme too.
   - Tab to Config and back is clean (0.17 / 0.39 s).
   - Without `MEALSCGA.DAT`: empty dotted frames and a message on row 23.
   - Note any CGA snow on the text screens.
4. EGA/VGA, if available: EGA pictures, played card in grey. Mono: text-mode planner.
5. `KEYB FR`: top-row digits unshifted, é è ç à typed in Kanban.
6. Kanban focus: **F** toggles a `*` before the name; Esc, Q, reload keeps it; moving the card to DONE clears it; **F** on DONE does nothing.
7. Daily Review: full date and ISO week, counters 6/8/2, Next/Prochain, focused cards; Up/Down (one line, no wrap), PgUp/PgDn, Home/End, scroll markers and the range in the footer. Opening the 67-line worst case takes 0.30 s (EGA), 0.29 s (CGA), 0.28 s (Hercules); each Down step 0.11 s on all three.
8. Archived day: menu entry **3 Daily review** (PicoCalc v1.15 still has only 1–2); no KANBAN, meal lines `--`, Esc returns with the cursor on that entry.
9. Exports: `EXPORT\YYYYMMDD.ICS`, `WEIGHTS\YYYY.csv`, `EXPORT\MPYYMMDD.CSV`.
10. PicoCalc round-trip without focus: SD save -> DOS -> edit -> back to SD, byte-identical. A DOS save with `"focus":true` opens on PicoCalc v1.15; a PicoCalc resave drops the key.
11. Tab/Space from every screen; Q, restart, data kept.
