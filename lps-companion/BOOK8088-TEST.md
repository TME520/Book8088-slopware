# Book 8088 – liste de tests sur le vrai materiel (LPS COMPANION v1.15)

Tests a faire sur le Book 8088 (8088, carte CGA) apres LPS-DOS-115-CHG14. Les
temps de reference viennent de DOSBox a la vitesse d'un 8088
(`cputype=8086`, `cycles=fixed 300`) ; noter le temps reel a cote.

## 1. Fichiers a copier

Dans un meme dossier (par exemple `C:\LPSCOMP\`), depuis la racine du depot :

| Fichier | Taille | Role |
|---|---|---|
| `LPSCOMP.EXE` | 106 288 o | programme |
| `ROSARY.DAT` | 10 217 o | textes du chapelet |
| `MEALS.DAT` | 199 964 o | images du planificateur de repas (EGA) |
| `MEALSCGA.DAT` | 49 996 o | images du planificateur de repas (CGA, LPS-DOS-115-CHG14) |

Optionnel : `HELLO.EXE` (2 776 o) pour un premier test de la machine.
Les donnees vont dans `C:\LPS\` (cree au premier lancement). Pour le test
aller-retour PicoCalc, copier aussi `SAVE_A.JSN` / `SAVE_B.JSN` de la carte SD.

## 2. Points a verifier

| # | Test | Attendu | Ref. DOSBox | Resultat |
|---|---|---|---|---|
| 1 | Lancer `LPSCOMP` | banniere puis barre de titre **LPS COMPANION v1.15** | – | |
| 2 | Vitesse des touches : Bas sur l'Accueil, cocher une activite, deplacer une tache Kanban | reponse sans attente visible, aucune touche perdue en tapant vite | Accueil 0,17–0,19 s ; 2 coches 0,28–0,33 s | |
| 3 | Chargement JSON au demarrage (sauvegarde courte, puis sauvegarde de 31 jours) | Accueil affiche, donnees presentes | 0,7 s / 3,1 s | |
| 4 | Ecriture JSON : quitter Activites (Esc), Config > 4 Enregistrer | pas d'erreur, `SAVE_A.JSN`/`SAVE_B.JSN` alternes | 0,7 s / 3,2 s (31 jours) | |
| 5 | Planificateur de repas (Espace) sur EGA couleur (seulement si une carte EGA/VGA est presente) | images des cartes en EGA, Gauche/Droite, Entree joue la carte | Tab vers Config 0,17 s / retour 0,33 s | |
| 6 | Planificateur en CGA (LPS-DOS-115-CHG13/14) : Espace, Gauche/Droite sur les 16 cartes, Entree | 320×200 4 couleurs, image tramee de chaque carte (`MEALSCGA.DAT`), JOUÉE #n et bord trame une fois jouee ; accents lisibles (Entrée, Échap). Noter le temps de la premiere vue d'une carte (lecture disque) puis d'une carte deja vue | ouverture 0,55 s ; carte nouvelle 0,22–0,24 s ; deja vue 0,20–0,21 s ; jouer 0,28 s | |
| 6b | CGA : Tab vers Config puis Tab retour, puis Echap | retour propre au mode texte (pas de clignotement, curseur cache, couleurs du theme), puis au 320×200 | Tab 0,17 s / retour 0,39 s | |
| 6c | CGA : « neige » sur les ecrans texte (accueil, Kanban) en defilant vite | noter s'il y a des parasites ; aucun attendu en 320×200 | – | |
| 6d | Sans EGA ni CGA couleur (MDA/Hercules), ou `MEALS.DAT` absent en EGA | repli en mode texte, pas de plantage | – | |
| 6e | CGA : renommer `MEALSCGA.DAT`, ouvrir le planificateur | reste en 320×200, cadres vides en pointilles, ligne 23 « MEALSCGA.DAT absent : pas d'images » ; remettre le fichier ensuite | – | |
| 7 | `KEYB FR` (AZERTY) | chiffres de la rangee du haut sans Maj (& é " ' ( - è _ ç à) sur l'Accueil/Config ; taper é è ç à dans une tache Kanban | – | |
| 8 | Export ICS : cloturer le jour | `C:\LPS\EXPORT\YYYYMMDD.ICS` lisible | – | |
| 9 | CSV des poids : saisir un poids | `C:\LPS\WEIGHTS\YYYY.csv` | – | |
| 10 | CSV du planificateur : jouer une carte, quitter (Espace ou Esc) | `C:\LPS\EXPORT\MPYYMMDD.CSV` | – | |
| 11 | Aller-retour PicoCalc : sauvegarde SD -> DOS, modifier, recopier sur la SD | la PicoCalc v1.15 ouvre la sauvegarde ; entrees masquees (Config > Activites) conservees | – | |
| 12 | Tab / Espace depuis chaque ecran, Q pour quitter puis relancer | Config/planificateur s'ouvrent et se referment ; donnees conservees | – | |

Noter aussi : modele exact (CPU, carte video), version de DOS, et tout
caractere mal affiche (les capitales À È Ê Ô Â sont sans accent : normal).

---

# Book 8088 – real-device checklist (EN, short)

Copy `LPSCOMP.EXE` (106,288 B), `ROSARY.DAT` (10,217 B), `MEALS.DAT`
(199,964 B) and `MEALSCGA.DAT` (49,996 B) into one folder; data goes to `C:\LPS\`. Check, against the
DOSBox 8088-speed reference times above:

1. Title bar **LPS COMPANION v1.15**.
2. Key speed: Home Down 0.17–0.19 s, no lost keys when typing fast.
3. JSON load at start-up 0.7 s (short save) / 3.1 s (31 days); save on leaving Activities 0.7 s / 3.2 s.
4. Meal Planner (Space): on the Book's CGA, 320×200 dithered card pictures from `MEALSCGA.DAT` (CHG14), open 0.55 s, new card 0.22–0.24 s (note the real first-read time), seen card 0.20–0.21 s; without `MEALSCGA.DAT`, empty dotted frames and a row-23 message; Tab to Config and back is clean; note any CGA snow on text screens; text fallback without colour EGA/CGA.
5. `KEYB FR`: top-row digits unshifted, é è ç à typed in Kanban.
6. Exports: `EXPORT\YYYYMMDD.ICS`, `WEIGHTS\YYYY.csv`, `EXPORT\MPYYMMDD.CSV`.
7. PicoCalc round-trip: SD save -> DOS -> edit -> back to SD, opens on the PicoCalc v1.15 with hidden Home entries kept.
8. Tab/Space from every screen; Q, restart, data kept.
