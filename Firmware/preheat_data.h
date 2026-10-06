#pragma once
#include <avr/pgmspace.h>
#include <stdint.h>

enum class NozzleCategory : uint8_t {
    Default = 0,  // tout diametre sans surcharge (0.25, 0.3, 0.35, 0.4, 0.5)
    Dia060  = 1,  // 0.6, 0.7
    Dia080  = 2,  // >= 0.8
    _count
};

enum class MaterialIndex : uint8_t {
    PLA = 0, PETG, ASA, PC, PVB, PA, ABS, HIPS,
    PP, FLEX, VEGETAL, PLAPERL, PLABOIS, CLEAN1, CLEAN2,
    _count
};

static constexpr uint8_t MATERIAL_COUNT = static_cast<uint8_t>(MaterialIndex::_count);
static constexpr uint8_t NOZZLE_CAT_COUNT = static_cast<uint8_t>(NozzleCategory::_count);

// Tables PROGMEM
extern const uint16_t preheat_hotend_temps[MATERIAL_COUNT][NOZZLE_CAT_COUNT] PROGMEM;
extern const uint16_t preheat_bed_temps[MATERIAL_COUNT] PROGMEM;

// Categorie d'un diametre de buse en um (0xFFFF = EEPROM vierge, 0 = inconnu)
inline NozzleCategory nozzle_category_from_um(uint16_t dia_um) {
    if (dia_um == 0xFFFF || dia_um == 0)
        return NozzleCategory::Default;
    if (dia_um >= 800) return NozzleCategory::Dia080;
    if (dia_um >= 600) return NozzleCategory::Dia060;
    return NozzleCategory::Default;
}

// API
NozzleCategory get_nozzle_category();
uint16_t get_preheat_hotend_temp(MaterialIndex material);
uint16_t get_preheat_bed_temp(MaterialIndex material);
