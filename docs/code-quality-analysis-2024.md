# Analyse de Qualité du Code - Prusa-Firmware

**Date** : 2024-12-08
**Branche** : beta_2
**Commit** : c6b6faf2

---

## Résumé Exécutif

Analyse systématique du codebase Firmware/ pour identifier les opportunités d'amélioration de qualité sans changement de comportement (refactoring safe).

---

## Améliorations Appliquées (Option A)

### Comparaisons NULL modernisées (~35 occurrences)

| Fichier | Lignes | Transformation |
|---------|--------|----------------|
| `cardreader.cpp` | 183 | `!= NULL` → implicite |
| `cmdqueue.h` | 82, 83 | `!= NULL` → implicite |
| `cmdqueue.cpp` | 430, 463 | `!= NULL` → implicite |
| `menu.cpp` | 173, 174, 381 | `== NULL` → `!` |
| `planner.cpp` | 618 | `== NULL` → `!` |
| `stepper.cpp` | 319, 809, 812 | `== NULL` / `!= NULL` → implicite |
| `temperature.cpp` | 595 | `!= NULL` → implicite |
| `SdBaseFile.cpp` | 1120, 1134 | `!= NULL` → implicite |
| `Marlin_main.cpp` | 1751, 8965 | `== NULL` → `!` |
| `ultralcd.cpp` | 2880, 2906, 3030, 3035, 3043, 3053, 3138, 3157, 3183, 3190, 3195, 3241, 3249, 3260, 6059, 7286, 7585 | `== NULL` / `!= NULL` → implicite |

### Comparaisons booléennes simplifiées (~32 occurrences)

| Fichier | Lignes | Transformation |
|---------|--------|----------------|
| `cardreader.cpp` | 820 | `== false` → `!` |
| `cmdqueue.cpp` | 489 | `== false && == true` → `! &&` implicite |
| `temperature.cpp` | 306, 321, 334, 941, 990, 1001, 1009, 1017, 1026, 1033, 1522 | `== true` / `== false` → implicite |
| `Marlin_main.cpp` | 2280, 2286, 2608, 2820, 4223, 4333, 4337, 4386, 4431, 8869, 9041, 9252 | `== true` / `== false` → implicite |
| `mesh_bed_calibration.cpp` | 1016, 1139, 1151, 1157, 1170 | `== false` → `!` |
| `mmu2_reporting.cpp` | 133, 171 | `== false` → `!` |
| `ultralcd.cpp` | 1753, 5405, 5977 | `== false` / `== true` → implicite |
| `tmc2130.cpp` | 390 | `== false` → `!` |

---

## Améliorations Proposées (Non encore appliquées)

### Problème #3 : Conditions complexes dans temperature.cpp

**Localisation** : `temperature.cpp:904` et `temperature.cpp:920`

**Code actuel** :
```cpp
// Ligne 904 - Vérifier si la cible est atteinte
if ((_current_temperature > (_target_temperature - __hysteresis))
    && temp_runaway_status[_heater_id] == TempRunaway_PREHEAT)

// Ligne 920 - Vérifier si on est dans la plage cible
if ((_current_temperature > (_target_temperature - __hysteresis))
    && (_current_temperature < (_target_temperature + __hysteresis)))
```

**Problèmes identifiés** :
1. Lisibilité réduite - intention pas immédiatement claire
2. Duplication de logique - même pattern répété
3. Risque d'erreur lors de modifications
4. Tests unitaires impossibles sur cette logique isolée

**Solution proposée** :
```cpp
static inline bool is_target_reached(float current, float target, float hysteresis) {
    return current > (target - hysteresis);
}

static inline bool is_within_target_range(float current, float target, float hysteresis) {
    return current > (target - hysteresis) && current < (target + hysteresis);
}
```

**Impact** : Faible risque, amélioration lisibilité, pas d'impact taille binaire (inline)

---

### Problème #4 : Code dupliqué micrométrage dans Marlin_main.cpp

**Localisation** : Lignes 9323-9360, 9363-9552, 9554-9710

**Fonctions concernées** :
| Fonction | Lignes | Rôle |
|----------|--------|------|
| `d_ReadData()` | 9323-9360 | Lecture micrométrage simple |
| `bed_check()` | 9363-9552 | Vérification du lit avec micrométrage |
| `bed_analysis()` | 9554-9710 | Analyse complète du lit avec logging |

**Code dupliqué identifié** :
```cpp
// Apparaît 3 fois - lignes 9325, 9366, 9557
int digit[13];
for (int i = 0; i<13; i++) {
    for (int j = 0; j < 4; j++) {
        while (digitalRead(D_DATACLOCK) == LOW) {}
        while (digitalRead(D_DATACLOCK) == HIGH) {}
        bitWrite(digit[i], j, digitalRead(D_DATA));
    }
}
```

**Problèmes identifiés** :
1. Violation DRY - ~40 lignes copiées 3 fois
2. Maintenance difficile - bug à corriger à 3 endroits
3. Risque de divergence entre copies
4. Augmentation inutile taille Flash (ATmega2560 à 90%)
5. Fonctions trop longues (~150 lignes)

**Solution proposée** :
```cpp
static void read_micrometer_digits(int digit[13]) {
    digitalWrite(D_REQUIRE, HIGH);
    for (int i = 0; i < 13; i++) {
        for (int j = 0; j < 4; j++) {
            while (digitalRead(D_DATACLOCK) == LOW) {}
            while (digitalRead(D_DATACLOCK) == HIGH) {}
            bitWrite(digit[i], j, digitalRead(D_DATA));
        }
    }
    digitalWrite(D_REQUIRE, LOW);
}

static float micrometer_digits_to_float(const int digit[13]) {
    float output = 0;
    char mergeOutput[7];
    for (int r = 5; r <= 10; r++) {
        mergeOutput[r-5] = '0' + digit[r];
    }
    mergeOutput[6] = '\0';
    output = atof(mergeOutput);
    if (digit[4] == 8) output *= -1;
    for (int i = digit[11]; i > 0; i--) output /= 10;
    return output;
}
```

**Impact estimé** :
- Réduction ~100-150 lignes
- Économie Flash : ~500-800 bytes
- Risque modéré (nécessite tests hardware)

---

## Autres Problèmes Identifiés (Priorité Basse)

### Unsafe C-style casts (6 fichiers)
- `eeprom.cpp:327` - `uint8_t *dst = (uint8_t*)__dst;`
- `Marlin_main.cpp:1364` - `uint8_t* buff = (uint8_t*)block_buffer;`
- `xyzcal.cpp:953-955` - Trois casts consécutifs

**Recommandation** : Remplacer par `static_cast<>()` ou `reinterpret_cast<>()`

### TODO/FIXME non résolus
- `planner.cpp:209` - FIXME assert 64bit overflow
- `planner.cpp:328` - FIXME routine appelée 15x
- `planner.cpp:910` - FIXME moves_queued > 1?
- `planner.cpp:1064` - FIXME two lines planned?
- `Marlin_main.cpp:3019` - FIXME non documenté
- `optiboot_xflash.cpp:24` - FIXME signature ATmega2560

### Magic numbers non documentés
- `tmc2130.cpp` - Valeurs hexadécimales GCONF sans explication bits
- `temperature.cpp:653-663` - Offsets de température

### Code mort commenté
- `Marlin_main.cpp:9404-9411` - Bloc destination/plan_buffer commenté
- `Marlin_main.cpp:9438-9445` - Code similaire commenté

---

## Métriques de Build Post-Modifications

**Variante** : MK3S_ENGLISH
**Compilation** : Succès

| Métrique | Valeur | Utilisation |
|----------|--------|-------------|
| Flash (Program) | 235990 bytes | 90.0% |
| RAM (Data) | 5701 bytes | 69.6% |

---

## Prochaines Étapes Recommandées

1. [x] Moderniser comparaisons NULL
2. [x] Simplifier comparaisons booléennes
3. [x] Extraire helpers conditions température
4. [x] Factoriser code micrométrage
5. [x] Remplacer C-style casts par static_cast (Option A : casts critiques non-EEPROM)
6. [x] Résoudre FIXMEs dans planner.cpp
7. [~] Nettoyer code mort commenté - **Non nécessaire** : code dans git si besoin, impact nul

---

## Changements Additionnels (Session 2)

### Problème #3 : Helpers conditions température

**Fichier** : `temperature.cpp`

**Ajouts** (lignes 828-836) :
```cpp
// Helper: check if current temperature has reached target (within lower hysteresis bound)
static inline bool temp_is_target_reached(float current, float target, float hysteresis) {
    return current > (target - hysteresis);
}

// Helper: check if current temperature is within target range (± hysteresis)
static inline bool temp_is_within_range(float current, float target, float hysteresis) {
    return current > (target - hysteresis) && current < (target + hysteresis);
}
```

**Modifications** :
- Ligne 915 : `if (temp_is_target_reached(...) && ...)`
- Ligne 931 : `if (temp_is_within_range(...))`

### Problème #4 : Factorisation code micrométrage

**Fichier** : `Marlin_main.cpp`

**Ajouts** (lignes 9322-9373) :
```cpp
// Helper: read 13 digits from micrometer via GPIO clock/data protocol
static void micrometer_read_digits(int digit[13])

// Helper: convert micrometer digit array to float value
static float micrometer_digits_to_value(const int digit[13])

// Combined helper: read micrometer and return float value
static float micrometer_read_value()
```

**Modifications** :
- `bed_check()` : Supprimé ~45 lignes dupliquées, remplacées par `output = micrometer_read_value();`
- `bed_analysis()` : Supprimé ~45 lignes dupliquées, remplacées par `output = micrometer_read_value();`
- Nettoyage variables inutilisées (`digit`, `str`, `mergeOutput`, `t1`, `t_delay`)

**Impact** :
- Réduction nette : ~80 lignes de code
- Flash : 236000 bytes (légère augmentation +10 bytes due aux fonctions helper)
- Maintenabilité : Améliorée significativement

---

## Changements Additionnels (Session 3)

### Problème #5 : C-style casts (Option A)

**Fichiers modifiés** : `eeprom.cpp`, `Marlin_main.cpp`, `xyzcal.cpp`, `SdFatUtil.cpp`, `adc.cpp`, `mmu2_error_converter.cpp`

**15 casts convertis** (non-EEPROM uniquement) :
- `reinterpret_cast<>` pour conversions de pointeurs (void* → uint8_t*, block_buffer, PROGMEM)
- `const_cast<>` pour suppression de volatile (`adc_values`)

**Impact** : Zéro runtime (résolu à la compilation)

### Problème #6 : FIXMEs dans planner.cpp

**Fichier** : `planner.cpp`

**4 FIXMEs résolus** :

| Ligne | Description | Résolution |
|-------|-------------|------------|
| 209 | Overflow uint32 pour nominal_rate_sqr | Documenté : nominal_rate < 65535, pas d'overflow |
| 328 | Routine appelée 15x | Converti en Note (suggestion d'optimisation future) |
| 910 | Pourquoi moves_queued > 1 ? | Répondu : bloc à block_buffer_tail exécuté par ISR |
| 1064 | Même question junction velocity | Même réponse : protection bloc en cours |

**Impact** : Zéro (commentaires uniquement)

### Optimisation performance planner (REPORTÉE)

**Fichier** : `planner.cpp` ligne 328

**Objectif** : Remplacer `ceil()` par une version inline pour éviter l'appel à la libm.

**Analyse** :
- `ceil()` est utilisé à ~10 endroits dans le codebase
- La libm est déjà linkée pour ces autres usages
- Remplacer seulement 2 appels ajoute +54 bytes au lieu d'en économiser

**Décision** : Reportée. Pour être efficace, il faudrait remplacer TOUS les `ceil()` du projet (~10 fichiers), ce qui permettrait au linker d'éliminer la fonction `ceil()` de la libm.
