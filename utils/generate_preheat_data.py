#!/usr/bin/env python3
"""
Generate preheat temperature data from PrusaSlicer profiles.

Parses PrusaSlicer .ini files to extract first layer temperatures per material
and nozzle diameter, then generates C++ lookup tables.

Usage:
    uv run python utils/generate_preheat_data.py \\
      --ini ../PrusaSlicer/resources/profiles/PrusaResearch.ini \\
      --output Firmware/preheat_data.cpp

Both printer families are always generated (MK3/MK3S and MK2.5/MK2.5S): generating a single
family left the other builds on the "#error" branch.
"""

import argparse
import re
from pathlib import Path
from typing import Dict, Optional, Tuple
import sys
if sys.version_info < (3, 7):
    sys.exit("Python 3.7+ required (dict insertion order guarantee)")


# All material temperatures, ordered to match firmware enum MaterialIndex.
# Used as fallback when INI is unavailable, and as the canonical source for
# materials not present in PrusaSlicer (PP, VEGETAL, PLAPERL, PLABOIS, CLEAN*).
DEFAULT_TEMPS = {
    'PLA':     {'default': 215, '060': 215, '080': 230, 'bed': 60},
    'PETG':    {'default': 250, '060': 240, '080': 250, 'bed': 85},
    'ASA':     {'default': 260, '060': 260, '080': 265, 'bed': 105},
    'PC':      {'default': 275, '060': 275, '080': 275, 'bed': 110},
    'PVB':     {'default': 215, '060': 215, '080': 225, 'bed': 75},
    'PA':      {'default': 275, '060': 275, '080': 275, 'bed': 90},
    'ABS':     {'default': 255, '060': 255, '080': 265, 'bed': 100},
    'HIPS':    {'default': 220, '060': 220, '080': 240, 'bed': 100},
    'PP':      {'default': 254, '060': 254, '080': 254, 'bed': 100},
    'FLEX':    {'default': 240, '060': 240, '080': 240, 'bed': 50},
    'VEGETAL': {'default': 230, '060': 230, '080': 230, 'bed': 60},
    'PLAPERL': {'default': 205, '060': 205, '080': 205, 'bed': 60},
    'PLABOIS': {'default': 205, '060': 205, '080': 205, 'bed': 60},
    'CLEAN1':  {'default': 260, '060': 260, '080': 260, 'bed': 0},
    'CLEAN2':  {'default': 90,  '060': 90,  '080': 90,  'bed': 0},
}

# MK2.5/MK2.5S differences from the MK3 values (official firmware: PC bed 105 C on the MK52 bed)
MK25_OVERRIDES = {
    'PC': {'bed': 105},
}

# Highest preheat targets accepted (HEATER_0_MAXTEMP 305 / BED_MAXTEMP 125, minus margin)
MAX_HOTEND_TEMP = 290
MAX_BED_TEMP = 120

# Material name patterns for matching slicer profiles
MATERIAL_PATTERNS = {
    'PLA':  [r'\bPLA\b'],
    'PETG': [r'\bPETG?\b'],
    'ASA':  [r'\bASA\b'],
    'ABS':  [r'\bABS\b'],
    'HIPS': [r'\bHIPS\b'],
    'FLEX': [r'\bFLEX\b'],
    'PVB':  [r'\bPVB\b'],
    'PA':   [r'\bPA11\b', r'\bNylon\b'],
    'PC':   [r'\bPC\b'],
}

# Priority prefixes for profile selection (higher index = higher priority)
_PROFILE_PRIORITY = ['Generic', 'Prusa ', 'Prusament']


class IniProfile:
    """Represents a single filament profile from the INI file."""

    def __init__(self, name: str):
        self.name = name
        self.inherits: Optional[str] = None
        self.properties: Dict[str, str] = {}
        self.resolved = False
        self._parent: Optional['IniProfile'] = None

    def get(self, key: str, default: Optional[str] = None) -> Optional[str]:
        """Get property value with inheritance resolution."""
        if key in self.properties:
            return self.properties[key]
        if self._parent:
            return self._parent.get(key, default)
        return default

    def resolve_inheritance(self, profiles: Dict[str, 'IniProfile']):
        """Recursively resolve inheritance chain."""
        if self.resolved or not self.inherits:
            return

        parent_name = self.inherits
        if parent_name in profiles:
            self._parent = profiles[parent_name]
            self._parent.resolve_inheritance(profiles)

        self.resolved = True


def parse_ini_file(ini_path: Path) -> Dict[str, IniProfile]:
    """
    Parse PrusaSlicer INI file into profile objects.

    Handles non-standard INI format with duplicate sections, multi-line values,
    and complex section names containing @, spaces, etc.
    """
    profiles = {}
    current_profile = None
    current_key = None

    with open(ini_path, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.rstrip('\n')

            # Section header: [filament:Name Here @0.6 nozzle]
            if line.startswith('[filament:'):
                match = re.match(r'\[filament:([^\]]+)\]', line)
                if match:
                    name = match.group(1).strip()
                    current_profile = IniProfile(name)
                    profiles[name] = current_profile
                    current_key = None
                continue

            # Skip non-filament sections
            if line.startswith('[') and not current_profile:
                continue

            if not current_profile:
                continue

            # Key = value line
            if '=' in line and not line.startswith((' ', '\t')):
                key, _, value = line.partition('=')
                key = key.strip()
                value = value.strip()

                if key == 'inherits':
                    current_profile.inherits = value
                else:
                    current_profile.properties[key] = value
                    current_key = key

            # Continuation line (multi-line value)
            elif current_key and line.startswith((' ', '\t')):
                current_profile.properties[current_key] += '\n' + line.strip()

    # Resolve all inheritance chains
    for profile in profiles.values():
        profile.resolve_inheritance(profiles)

    return profiles


def is_compatible_variant(profile: IniProfile, variant: str) -> bool:
    """
    Check if profile is compatible with target printer variant.

    Parses compatible_printers_condition for printer_model regex patterns.
    """
    condition = profile.get('compatible_printers_condition', '')
    if not condition:
        return True  # No restriction means compatible

    # Look for printer_model=~/pattern/ expressions
    match = re.search(r'printer_model\s*=~\s*/([^/]+)/', condition)
    if not match:
        return True  # Can't parse, assume compatible

    pattern = match.group(1)

    # Check if our variant matches the regex pattern
    # Common patterns: (MK3S|MK3), (MK2.5S|MK2.5), etc.
    variant_map = {
        'MK3S': ['MK3S', 'MK3'],
        'MK3': ['MK3S', 'MK3'],
        'MK25S': ['MK2.5S', 'MK2.5'],
        'MK25': ['MK2.5S', 'MK2.5'],
    }

    variants_to_check = variant_map.get(variant, [variant])
    for v in variants_to_check:
        if re.search(pattern, v):
            return True

    return False


def extract_temperature(profile: IniProfile, temp_key: str, fallback_key: str) -> Optional[int]:
    """
    Extract temperature value from profile.

    Returns first value if multi-value (e.g., "215; 210; 205" -> 215).
    """
    value = profile.get(temp_key)
    if not value:
        value = profile.get(fallback_key)

    if not value:
        return None

    # Handle multi-value (take first)
    if ';' in value:
        value = value.split(';')[0].strip()

    try:
        return int(float(value))
    except (ValueError, TypeError):
        return None


def _profile_priority(name: str) -> int:
    """Return priority score for profile name (higher = preferred)."""
    for i, prefix in enumerate(_PROFILE_PRIORITY):
        if name.startswith(prefix):
            return i + 1
    return 0


def find_material_profiles(
    profiles: Dict[str, IniProfile],
    material: str,
    variant: str
) -> Tuple[Optional[IniProfile], Optional[IniProfile], Optional[IniProfile]]:
    """
    Find default, @0.6, and @0.8 profiles for a material.

    Skips template profiles (*name*) and prioritizes Prusament > Prusa > Generic.
    Returns: (default_profile, profile_060, profile_080)
    """
    patterns = MATERIAL_PATTERNS.get(material, [])
    if not patterns:
        return None, None, None

    default_profile = None
    default_prio = -1
    profile_060 = None
    prio_060 = -1
    profile_080 = None
    prio_080 = -1

    for name, profile in profiles.items():
        # Skip template profiles (e.g. *PET*, *PC*, *ABSC*)
        if name.startswith('*'):
            continue

        # Check compatibility with target printer
        if not is_compatible_variant(profile, variant):
            continue

        # Check if name matches material pattern
        if not any(re.search(pat, name, re.IGNORECASE) for pat in patterns):
            continue

        prio = _profile_priority(name)

        # Categorize by nozzle size, keeping highest priority match
        if '@0.8' in name or '@ 0.8' in name:
            if prio > prio_080:
                profile_080 = profile
                prio_080 = prio
        elif '@0.6' in name or '@ 0.6' in name:
            if prio > prio_060:
                profile_060 = profile
                prio_060 = prio
        elif '@' not in name:
            if prio > default_prio:
                default_profile = profile
                default_prio = prio

    return default_profile, profile_060, profile_080


def extract_material_temps(
    profiles: Dict[str, IniProfile],
    material: str,
    variant: str
) -> Dict[str, int]:
    """
    Extract temperatures for a material across nozzle sizes.

    Returns dict with keys: 'default', '060', '080', 'bed'
    """
    default_prof, prof_060, prof_080 = find_material_profiles(profiles, material, variant)

    result = {}

    # Extract hotend temps
    if default_prof:
        temp = extract_temperature(default_prof, 'first_layer_temperature', 'temperature')
        if temp:
            result['default'] = temp

    # 0.6mm (use default if no specific profile)
    if prof_060:
        temp = extract_temperature(prof_060, 'first_layer_temperature', 'temperature')
        if temp:
            result['060'] = temp

    if '060' not in result and 'default' in result:
        result['060'] = result['default']

    # 0.8mm
    if prof_080:
        temp = extract_temperature(prof_080, 'first_layer_temperature', 'temperature')
        if temp:
            result['080'] = temp

    if '080' not in result and 'default' in result:
        result['080'] = result['default']

    # Extract bed temp (from default profile)
    if default_prof:
        bed_temp = extract_temperature(default_prof, 'first_layer_bed_temperature', 'bed_temperature')
        if bed_temp:
            result['bed'] = bed_temp

    return result


def generate_material_data(
    ini_path: Optional[Path],
    variant: str
) -> Dict[str, Dict[str, int]]:
    """
    Generate complete material temperature dataset.

    Starts from DEFAULT_TEMPS, then overrides with INI-parsed values
    for materials that have matching slicer profiles.
    """
    result = {mat: temps.copy() for mat, temps in DEFAULT_TEMPS.items()}

    if ini_path and ini_path.exists():
        try:
            profiles = parse_ini_file(ini_path)
            for material in DEFAULT_TEMPS:
                if material in MATERIAL_PATTERNS:
                    temps = extract_material_temps(profiles, material, variant)
                    if temps:
                        result[material].update(temps)
        except Exception as e:
            print(f"Warning: Failed to parse INI file: {e}", file=sys.stderr)
            print("Using default temperatures", file=sys.stderr)
    elif ini_path:
        print(f"Warning: INI file not found: {ini_path}", file=sys.stderr)
        print("Using default temperatures", file=sys.stderr)

    return result


def _table_lines(material_data: Dict[str, Dict[str, int]]) -> list:
    """Hotend and bed PROGMEM tables of one printer family."""
    lines = [
        'const uint16_t preheat_hotend_temps[(uint8_t)MaterialIndex::_count][(uint8_t)NozzleCategory::_count] PROGMEM = {',
        '    // {Default, 0.6mm, 0.8mm}',
    ]
    for material in DEFAULT_TEMPS:
        temps = material_data[material]
        default = temps.get('default', 0)
        temp_060 = temps.get('060', default)
        temp_080 = temps.get('080', default)
        lines.append(f'    {{{default:3d}, {temp_060:3d}, {temp_080:3d}}},  // {material}')
    lines += [
        '};',
        '',
        'const uint16_t preheat_bed_temps[(uint8_t)MaterialIndex::_count] PROGMEM = {',
        '    ' + ', '.join(f"{material_data[m].get('bed', 0):3d}" for m in DEFAULT_TEMPS),
        '};',
        '',
    ]
    return lines


def validate(material_data: Dict[str, Dict[str, int]], family: str):
    """Refuse temperatures beyond what the firmware accepts as a preheat target."""
    for material, temps in material_data.items():
        for key in ('default', '060', '080'):
            if not 0 <= temps.get(key, 0) <= MAX_HOTEND_TEMP:
                sys.exit(f"{family} {material} {key}: {temps[key]} C is beyond {MAX_HOTEND_TEMP} C")
        if not 0 <= temps.get('bed', 0) <= MAX_BED_TEMP:
            sys.exit(f"{family} {material} bed: {temps['bed']} C is beyond {MAX_BED_TEMP} C")


def check_enum_order():
    """DEFAULT_TEMPS must list the materials in the order of the firmware enum MaterialIndex."""
    header = Path(__file__).resolve().parent.parent / 'Firmware' / 'preheat_data.h'
    m = re.search(r'enum class MaterialIndex[^{]*\{([^}]*)\}', header.read_text(encoding='utf-8'))
    names = [n.split('=')[0].strip() for n in m.group(1).split(',')]
    names = [n for n in names if n and n != '_count']
    if names != list(DEFAULT_TEMPS):
        sys.exit(f"DEFAULT_TEMPS order {list(DEFAULT_TEMPS)} differs from MaterialIndex {names}")


def generate_cpp_code(
    mk3_data: Dict[str, Dict[str, int]],
    mk25_data: Dict[str, Dict[str, int]],
) -> str:
    """Generate C++ source code with temperature lookup tables."""

    lines = [
        '#include "preheat_data.h"',
        '#include "eeprom.h"',
        '',
        '// --- GENERATED BY utils/generate_preheat_data.py ---',
        '// Variant: MK3S',
        '// Source: PrusaSlicer INI + DEFAULT_TEMPS fallback',
        '',
        '#if defined(PRINTER_VARIANT_MK3S) || defined(PRINTER_VARIANT_MK3)',
        '',
    ]
    lines += _table_lines(mk3_data)
    lines += [
        '#elif defined(PRINTER_VARIANT_MK25S) || defined(PRINTER_VARIANT_MK25)',
        '',
        '// MK2.5 variants use same temperatures for now, except MK25_OVERRIDES',
        '// (structure ready for future divergence)',
    ]
    lines += _table_lines(mk25_data)
    lines += [
        '#else',
        '#error "No PRINTER_VARIANT_* defined"',
        '#endif',
        '',
    ]

    # Add helper functions
    lines.extend([
        'NozzleCategory get_nozzle_category() {',
        '    return nozzle_category_from_um(eeprom_read_word((uint16_t*)EEPROM_NOZZLE_DIAMETER_uM));',
        '}',
        '',
        'uint16_t get_preheat_hotend_temp(MaterialIndex material) {',
        '    NozzleCategory cat = get_nozzle_category();',
        '    return pgm_read_word(&preheat_hotend_temps[(uint8_t)material][(uint8_t)cat]);',
        '}',
        '',
        'uint16_t get_preheat_bed_temp(MaterialIndex material) {',
        '    return pgm_read_word(&preheat_bed_temps[(uint8_t)material]);',
        '}',
        '',
    ])

    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(
        description='Generate preheat temperature data from PrusaSlicer profiles'
    )
    parser.add_argument(
        '--ini',
        type=Path,
        help='Path to PrusaSlicer INI file (e.g., PrusaResearch.ini)'
    )
    parser.add_argument(
        '--output',
        type=Path,
        help='Output C++ file (default: stdout)'
    )
    args = parser.parse_args()

    check_enum_order()

    # Generate material data
    mk3_data = generate_material_data(args.ini, 'MK3S')
    mk25_data = generate_material_data(args.ini, 'MK25S')
    for material, temps in MK25_OVERRIDES.items():
        mk25_data[material].update(temps)
    validate(mk3_data, 'MK3')
    validate(mk25_data, 'MK2.5')

    # Generate C++ code
    cpp_code = generate_cpp_code(mk3_data, mk25_data)

    # Output
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with open(args.output, 'w', encoding='utf-8', newline='\n') as f:
            f.write(cpp_code)
        print(f"Generated: {args.output}", file=sys.stderr)
    else:
        print(cpp_code)


if __name__ == '__main__':
    main()
