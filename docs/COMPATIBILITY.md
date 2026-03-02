# Liste de Compatibilite — Phosphoric v1.12.0-alpha

Derniere mise a jour : 2026-03-02

---

## Systemes d'exploitation

| Programme | Format | Modele | Statut | Notes |
|-----------|--------|--------|--------|-------|
| BASIC 1.0 ROM | .ROM | ORIC-1 | Fonctionnel | Interpreteur BASIC complet |
| BASIC 1.1 ROM | .ROM | Atmos | Fonctionnel | Auto-detection via JMP $ECCC |
| Sedoric V4.0 | .DSK | ORIC-1/Atmos | Fonctionnel | Boot, clavier, commandes DOS |

## Jeux

| Programme | Format | Statut | Notes |
|-----------|--------|--------|-------|
| Poker (poker-asn.tap) | .TAP | Fonctionnel | Graphismes HIRES corrects |
| Explode | .TAP | Fonctionnel | CLOAD, gameplay fonctionnel |

## Demos / Programmes de test

| Programme | Format | Statut | Notes |
|-----------|--------|--------|-------|
| Hello World | .TAP | Fonctionnel | Mode texte, sortie PRINT |
| Demo sonore | .TAP | Fonctionnel | Generation PSG tone/enveloppe |
| Demo graphique HIRES | .TAP | Fonctionnel | Rendu 240x200 |

---

## Resultats des tests unitaires

| Sous-systeme | Tests | Statut |
|-------------|-------|--------|
| CPU 6502 | 74/74 | Tous passent |
| Memoire | 19/19 | Tous passent |
| VIA 6522 I/O | 29/29 | Tous passent |
| Stockage (Sedoric/FDC) | 12/12 | Tous passent |
| Integration systeme | 7/7 | Tous passent |
| Export video | 11/11 | Tous passent |
| Audio PSG | 8/8 | Tous passent |
| Debogueur | 8/8 | Tous passent |
| Sauvegarde d'etat | 8/8 | Tous passent |
| Atmos | 10/10 | Tous passent |
| Joystick | 10/10 | Tous passent |
| Imprimante | 10/10 | Tous passent |
| Traceur MCP-40 | 10/10 | Tous passent |
| Rendu / Scaling | 10/10 | Tous passent |
| Trace CPU | 10/10 | Tous passent |
| Profileur CPU | 10/10 | Tous passent |
| Analyse ROM | 10/10 | Tous passent |
| **Total** | **256/256** | **100%** |

---

## Compatibilite ROM

| ROM | Taille | Vecteur RESET | Modele | Statut |
|-----|--------|--------------|--------|--------|
| basic10.rom (BASIC 1.0) | 16384 octets | $EA59 | ORIC-1 | Valide |
| basic11b.rom (BASIC 1.1) | 16384 octets | $ECCC | Atmos | Valide |
| microdis.rom (Microdisc) | variable | N/A | Overlay $E000 | Fonctionnel |

---

## Precision de l'emulation

| Composant | Precision | Notes |
|-----------|----------|-------|
| 6502 CPU | Cycle-accurate | 151 opcodes officiels, BCD, bug JMP indirect |
| VIA 6522 | Fonctionnel | Timers, interruptions, callbacks ports, CB1 edge |
| AY-3-8910 PSG | Precis | Courbe DAC Oricutron, diviseurs d'horloge |
| ULA Video | Fonctionnel | Texte + HIRES, attributs serie, timing PAL |
| WD1793 FDC | Fonctionnel | Commandes Type I/II, lecture/ecriture secteur |
| Clavier | Precis | Matrice 8x8 via VIA + PSG Port A |
| Joystick IJK | Fonctionnel | Port A PSG actif bas, clavier + gamepad |
| Imprimante | Fonctionnel | Centronics, STROBE via CA2 |
| MCP-40 | Fonctionnel | 8 commandes, 4 couleurs, export BMP |

---

## Limitations connues

- Le support Telestrat n'est pas encore implemente
- Certains programmes avec protection anti-copie peuvent ne pas se charger
- La couverture de code n'a pas ete mesuree formellement (estimation > 80%)
- Les tests de compatibilite jeux sont limites (95%+ game compatibility pending)

---

## Signaler la compatibilite

Si vous testez un programme non liste ici, merci de signaler :
- Nom du programme et format (.TAP/.DSK)
- S'il se charge et s'execute correctement
- Tout glitch visuel ou audio observe
- Etapes pour reproduire tout probleme
