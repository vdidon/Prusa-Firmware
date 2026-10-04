//! @file
//! @date 2025-01-17
//! @brief EEPROM backup/restore implementation

#include "Configuration.h"

#ifdef EEPROM_BACKUP_ENABLE

#include "eeprom_backup.h"
#include "cardreader.h"
#include "Marlin.h"
#include "eeprom.h"
#include "power_panic.h"
#include <avr/eeprom.h>
#include <string.h>

// External CardReader instance
extern CardReader card;

//! @brief Calculate CRC32 (reflected polynomial 0xEDB88320) with byte-by-byte updates
//! @details Bitwise variant: no 1KB lookup table in flash. Only used on backup/restore/verify
//! (4KB of data), so the extra cycles are irrelevant.
static uint32_t __attribute__((noinline)) crc32_update(uint32_t crc, uint8_t data) {
    crc ^= data;
    for (uint8_t i = 0; i < 8; i++) {
        if (crc & 1) {
            crc = (crc >> 1) ^ 0xEDB88320UL;
        } else {
            crc >>= 1;
        }
    }
    return crc;
}

uint32_t calculate_eeprom_crc32() {
    uint32_t crc = 0xFFFFFFFF;
    for (uint16_t addr = 0; addr < EEPROM_SIZE; addr++) {
        crc = crc32_update(crc, eeprom_read_byte((uint8_t*)addr));
    }
    return ~crc;
}

void get_firmware_version_string(uint8_t *buffer) {
    const char *version = FW_VERSION;
    uint8_t i = 0;
    while (version[i] != '\0' && i < 11) {
        buffer[i] = version[i];
        i++;
    }
    while (i < 12) {
        buffer[i++] = 0;
    }
}

//! @brief Write entire EEPROM contents to an open SD file
static EepromBackupResult write_eeprom_to_sd() {
    uint8_t buffer[64];
    for (uint16_t addr = 0; addr < EEPROM_SIZE; addr += 64) {
        for (uint8_t i = 0; i < 64; i++) {
            buffer[i] = eeprom_read_byte((uint8_t*)(addr + i));
        }
        if (card.writeFile(buffer, 64) != 64) {
            return EEPROM_BACKUP_ERR_FILE_WRITE;
        }
    }
    return EEPROM_BACKUP_OK;
}

//! @brief Read EEPROM data from open SD file and write to EEPROM
static EepromBackupResult read_sd_to_eeprom() {
    uint8_t buffer[64];
    for (uint16_t addr = 0; addr < EEPROM_SIZE; addr += 64) {
        if (card.readFile(buffer, 64) != 64) {
            return EEPROM_BACKUP_ERR_FILE_READ;
        }
        for (uint8_t i = 0; i < 64; i++) {
            eeprom_write_byte((uint8_t*)(addr + i), buffer[i]);
        }
    }
    return EEPROM_BACKUP_OK;
}

//! @brief Calculate CRC32 of EEPROM data from open SD file
static EepromBackupResult calculate_sd_data_crc32(uint32_t *out_crc) {
    uint32_t crc = 0xFFFFFFFF;
    uint8_t buffer[64];
    for (uint16_t addr = 0; addr < EEPROM_SIZE; addr += 64) {
        if (card.readFile(buffer, 64) != 64) {
            return EEPROM_BACKUP_ERR_FILE_READ;
        }
        for (uint8_t i = 0; i < 64; i++) {
            crc = crc32_update(crc, buffer[i]);
        }
    }
    *out_crc = ~crc;
    return EEPROM_BACKUP_OK;
}

EepromBackupResult backup_eeprom_to_sd() {
    if (!card.mounted) return EEPROM_BACKUP_ERR_NO_SD;

    uint32_t crc = calculate_eeprom_crc32();

    struct EepromBackupHeader header;
    header.magic = EEPROM_BACKUP_MAGIC;
    header.version = EEPROM_BACKUP_VERSION;
    get_firmware_version_string(header.fw_version);
    header.timestamp = 0;
    header.crc32 = crc;
    memset(header.reserved, 0, sizeof(header.reserved));

    if (!card.openFileWriteBinary(EEPROM_BACKUP_FILENAME)) return EEPROM_BACKUP_ERR_FILE_OPEN;

    if (card.writeFile(&header, sizeof(header)) != sizeof(header)) {
        card.closefile();
        return EEPROM_BACKUP_ERR_FILE_WRITE;
    }

    EepromBackupResult result = write_eeprom_to_sd();
    card.closefile();
    return result;
}

EepromBackupResult verify_eeprom_backup(struct EepromBackupHeader *out_header) {
    if (!card.mounted) return EEPROM_BACKUP_ERR_NO_SD;
    if (!card.FileExists(EEPROM_BACKUP_FILENAME)) return EEPROM_BACKUP_ERR_FILE_OPEN;

    if (!card.openFileReadBinary(EEPROM_BACKUP_FILENAME)) return EEPROM_BACKUP_ERR_FILE_OPEN;

    struct EepromBackupHeader header;
    if (card.readFile(&header, sizeof(header)) != sizeof(header)) {
        card.closefile();
        return EEPROM_BACKUP_ERR_FILE_READ;
    }

    if (header.magic != EEPROM_BACKUP_MAGIC) {
        card.closefile();
        return EEPROM_BACKUP_ERR_INVALID_MAGIC;
    }

    if (header.version != EEPROM_BACKUP_VERSION) {
        card.closefile();
        return EEPROM_BACKUP_ERR_VERSION_MISMATCH;
    }

    uint32_t crc;
    EepromBackupResult result = calculate_sd_data_crc32(&crc);
    card.closefile();

    if (result != EEPROM_BACKUP_OK) return result;
    if (crc != header.crc32) return EEPROM_BACKUP_ERR_CRC_MISMATCH;

    if (out_header != nullptr) {
        memcpy(out_header, &header, sizeof(header));
    }
    return EEPROM_BACKUP_OK;
}

//! @brief Create temporary backup of current EEPROM
static EepromBackupResult create_temp_eeprom_backup() {
    if (!card.mounted) return EEPROM_BACKUP_ERR_NO_SD;

    struct EepromBackupHeader header;
    header.magic = EEPROM_BACKUP_MAGIC;
    header.version = EEPROM_BACKUP_VERSION;
    get_firmware_version_string(header.fw_version);
    header.timestamp = 0;
    header.crc32 = calculate_eeprom_crc32();
    memset(header.reserved, 0, sizeof(header.reserved));

    if (!card.openFileWriteBinary(EEPROM_BACKUP_TEMP_FILENAME)) return EEPROM_BACKUP_ERR_FILE_OPEN;

    if (card.writeFile(&header, sizeof(header)) != sizeof(header)) {
        card.closefile();
        return EEPROM_BACKUP_ERR_FILE_WRITE;
    }

    EepromBackupResult result = write_eeprom_to_sd();
    card.closefile();
    return result;
}

EepromBackupResult restore_eeprom_from_sd(bool validate_version) {
    struct EepromBackupHeader header;
    EepromBackupResult result = verify_eeprom_backup(&header);
    if (result != EEPROM_BACKUP_OK) return result;

    if (validate_version) {
        uint8_t current_version[12];
        get_firmware_version_string(current_version);
        if (memcmp(current_version, header.fw_version, 12) != 0) {
            return EEPROM_BACKUP_ERR_VERSION_MISMATCH;
        }
    }

    result = create_temp_eeprom_backup();
    if (result != EEPROM_BACKUP_OK) return result;

    if (!card.openFileReadBinary(EEPROM_BACKUP_FILENAME)) return EEPROM_BACKUP_ERR_FILE_OPEN;

    // Skip header
    struct EepromBackupHeader dummy;
    if (card.readFile(&dummy, sizeof(dummy)) != sizeof(dummy)) {
        card.closefile();
        return EEPROM_BACKUP_ERR_FILE_READ;
    }

    result = read_sd_to_eeprom();
    card.closefile();

    if (result == EEPROM_BACKUP_OK) {
        // Clear Power Panic recovery flags to prevent false recovery attempts after reboot
        eeprom_write_byte((uint8_t*)EEPROM_UVLO, PowerPanic::NO_PENDING_RECOVERY);
        eeprom_write_byte((uint8_t*)EEPROM_UVLO_Z_LIFTED, 0);
    }

    return result;
}

#endif // EEPROM_BACKUP_ENABLE
