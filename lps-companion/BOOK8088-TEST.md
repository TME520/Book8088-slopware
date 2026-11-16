# Book 8088 – liste de tests sur le vrai materiel (LPS COMPANION v1.15)

Tests a faire sur le Book 8088 (8088, EGA/CGA) apres LPS-DOS-115-CHG12. Les
temps de reference viennent de DOSBox a la vitesse d'un 8088
(`cputype=8086`, `cycles=fixed 300`) ; noter le temps reel a cote.

## 1. Fichiers a copier

Dans un meme dossier (par exemple `C:\LPSCOMP\`), depuis la racine du depot :

| Fichier | Taille | Role |
|---|---|---|
| `LPSCOMP.EXE` | 99 580 o | programme |
| `ROSARY.DAT` | 10 217 o | textes du chapelet |
| `MEALS.DAT` | 199 964 o | images du planificateur de repas |

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
| 5 | Planificateur de repas (Espace) sur EGA couleur | images des cartes en EGA, Gauche/Droite, Entree joue la carte | Tab vers Config 0,17 s / retour 0,33 s | |
| 6 | Planificateur sans EGA (ecran CGA/mono, ou `MEALS.DAT` absent) | repli en mode texte, pas de plantage | – | |
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

Copy `LPSCOMP.EXE` (99,580 B), `ROSARY.DAT` (10,217 B) and `MEALS.DAT`
(199,964 B) into one folder; data goes to `C:\LPS\`. Check, against the
DOSBox 8088-speed reference times above:

1. Title bar **LPS COMPANION v1.15**.
2. Key speed: Home Down 0.17–0.19 s, no lost keys when typing fast.
3. JSON load at start-up 0.7 s (short save) / 3.1 s (31 days); save on leaving Activities 0.7 s / 3.2 s.
4. Meal Planner (Space) in EGA colour; text fallback without EGA or `MEALS.DAT`.
5. `KEYB FR`: top-row digits unshifted, é è ç à typed in Kanban.
6. Exports: `EXPORT\YYYYMMDD.ICS`, `WEIGHTS\YYYY.csv`, `EXPORT\MPYYMMDD.CSV`.
7. PicoCalc round-trip: SD save -> DOS -> edit -> back to SD, opens on the PicoCalc v1.15 with hidden Home entries kept.
8. Tab/Space from every screen; Q, restart, data kept.
