# Phosphoric v1.15.1 — Corrections ACIA pour OricTel

Bonjour,

Suite à vos retours sur les problèmes de timing de l'ACIA 6551 rencontrés avec OricTel, nous avons implémenté plusieurs corrections dans Phosphoric. Voici le résumé.

---

## Nouveau : Backend Digitelec DTL 2000 (recommandé)

Le plus gros changement : un backend modem qui émule le comportement du Digitelec DTL 2000. L'ACIA 6551 reste 100% fidèle au datasheet MOS, et c'est le modem qui gère le buffering et le flow control — comme sur le vrai hardware.

```bash
./oric1-emu -r roms/basic10.rom -t orictel.tap -f \
  --serial digitelec:minitel.3614.fr:516
```

Ce que fait le backend :
- **Buffer interne 512 octets** (le vrai DTL 2000 avait sa propre RAM) → plus d'overrun pendant les clear screen ou les commandes REP Vidéotex
- **Flow control CTS automatique** : le modem coupe CTS quand son buffer dépasse 400 octets, le réactive sous 256 → le serveur distant est naturellement freiné
- **DCD piloté par la connexion TCP** : PEEK($031D) bit 5 = état de la porteuse
- **DTR pour connecter/déconnecter** : POKE $031E,3 = décrocher (TCP connect), POKE $031E,0 = raccrocher
- **V23 activé automatiquement** (1200 RX / 75 TX)
- **Pas de commandes AT** (fidèle au Digitelec qui n'était pas Hayes)

Côté programme ORIC, la connexion est simple :
```basic
10 POKE 798,3:REM DTR ON = DECROCHER
20 S=PEEK(797):IF (S AND 32)=32 THEN 20:REM ATTENDRE DCD
30 REM ... CONNECTE, LIRE/ECRIRE SUR 796 ...
40 POKE 798,0:REM DTR OFF = RACCROCHER
```

---

## Alternative : Patchs ACIA (si vous préférez garder votre architecture actuelle)

Si vous ne voulez pas changer de backend, les 3 problèmes que vous aviez identifiés sont aussi corrigés via des options CLI :

### 1. Overrun (buffer 1 octet)

```bash
--serial-buffer 256
```

Ajoute un FIFO RX transparent de 256 octets dans l'ACIA. Quand le CPU lit RDR ($031C), le byte suivant est automatiquement chargé depuis la file. RDRF reste à 1 tant que la file n'est pas vide. Pas fidèle au MOS 6551 mais fonctionnel.

### 2. IRQ perdue en TX+RX simultané

```bash
--serial-irq-on-rdrf
```

Mode WDC 65C51 : l'IRQ re-fire tant que RDRF est set, même après lecture du Status Register. Résout le problème où lire $031D pour vérifier TDRE efface l'IRQ d'un byte RX en attente.

### 3. Timing premier byte

Corrigé par défaut (pas d'option nécessaire). `tx_cycles` et `rx_cycles` sont maintenant initialisés à `tx_reload`/`rx_reload` au lieu de 0. Le changement de baud rate resynchronise aussi les compteurs.

---

## Ligne de commande complète (avec patchs ACIA)

```bash
# Option A : Backend Digitelec (recommandé, résout tout naturellement)
./oric1-emu -r roms/basic10.rom -t orictel.tap -f \
  --serial digitelec:minitel.3614.fr:516

# Option B : TCP direct + patchs ACIA (votre architecture actuelle)
./oric1-emu -r roms/basic10.rom -t orictel.tap -f \
  --serial tcp:minitel.3614.fr:516 \
  --serial-v23 \
  --serial-buffer 256 \
  --serial-irq-on-rdrf
```

L'option A est recommandée car elle résout les problèmes de timing architecturalement (le buffer est dans le modem, pas dans l'ACIA).

---

## Autres améliorations ACIA dans cette version

- Horloge ACIA corrigée : cristal 1.8432 MHz / 16 = 115200 Hz (était approximé à 1 MHz)
- Frame format variable : 5-8 databits + parité + 1-2 stopbits (était fixe à 10)
- Bitmask appliqué sur TX/RX (mode 7 bits masque le bit 7)
- DCD transition génère un IRQ (conforme datasheet)
- Savestate : état ACIA sauvé dans les fichiers .ost (section SER)
- Offset ACIA configurable : --acia-addr (défaut $031C)
- bas2tap corrigé : tokenizer BASIC avec table de 120 tokens extraite de la ROM

## Build

```bash
git clone https://git.nagominosato.fr:6775/chipinette/Phosphoric.git
cd Phosphoric
make SDL2=1
```

303 tests unitaires, 100% pass.

---

N'hésitez pas si vous avez des questions ou si vous rencontrez d'autres problèmes de timing.

bmarty
