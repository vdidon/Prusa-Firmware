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
#include "temperature.h"
#include <avr/eeprom.h>
#include <avr/pgmspace.h>
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
    static const char version[] PROGMEM = FW_VERSION;
    memset(buffer, 0, 12);
    strncpy_P((char*)buffer, version, 11);
}

//! @brief Bytes that belong to this very board and are never taken from a backup
//! @details A backup from another printer would otherwise duplicate its serial number (only
//! re-read from the USB chip when invalid) and its firmware crash flag.
static bool eeprom_restore_skipped(uint16_t addr) {
    return (addr >= EEPROM_PRUSA_SN && addr < EEPROM_PRUSA_SN + 20) || addr == EEPROM_FW_CRASH_FLAG;
}

//! @brief Write entire EEPROM contents to an open SD file
static EepromBackupResult write_eeprom_to_sd() {
    uint8_t buffer[64];
    for (uint16_t addr = 0; addr < EEPROM_SIZE; addr += 64) {
        manage_heater(); // keeps the watchdog fed
        for (uint8_t i = 0; i < 64; i++) {
            buffer[i] = eeprom_read_byte((uint8_t*)(addr + i));
        }
        if (card.writeFile(buffer, 64) != 64) {
            return EEPROM_BACKUP_ERR_FILE_WRITE;
        }
    }
    return EEPROM_BACKUP_OK;
}

//! @brief Read EEPROM data from open SD file (positioned after the header) and write to EEPROM
//! @details A full write takes ~14 s (3.4 ms per byte), far beyond the 4 s watchdog: feed it
//! and only write the bytes that change. The data CRC is checked again in the same pass.
static EepromBackupResult read_sd_to_eeprom(uint32_t expected_crc) {
    uint32_t crc = 0xFFFFFFFF;
    uint8_t buffer[64];
    for (uint16_t addr = 0; addr < EEPROM_SIZE; addr += 64) {
        manage_heater(); // keeps the watchdog fed
        if (card.readFile(buffer, 64) != 64) {
            return EEPROM_BACKUP_ERR_FILE_READ;
        }
        for (uint8_t i = 0; i < 64; i++) {
            crc = crc32_update(crc, buffer[i]);
            if (!eeprom_restore_skipped(addr + i)) {
                eeprom_update_byte((uint8_t*)(addr + i), buffer[i]);
            }
        }
    }
    return (~crc == expected_crc) ? EEPROM_BACKUP_OK : EEPROM_BACKUP_ERR_CRC_MISMATCH;
}

//! @brief Calculate CRC32 of EEPROM data from open SD file
static EepromBackupResult calculate_sd_data_crc32(uint32_t *out_crc) {
    uint32_t crc = 0xFFFFFFFF;
    uint8_t buffer[64];
    for (uint16_t addr = 0; addr < EEPROM_SIZE; addr += 64) {
        manage_heater(); // keeps the watchdog fed
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

//! @brief Check magic, format version and data CRC of a backup file
static EepromBackupResult verify_backup_file(const char *name, struct EepromBackupHeader *out_header) {
    if (!card.mounted) return EEPROM_BACKUP_ERR_NO_SD;
    if (!card.openFileReadBinary(name)) return EEPROM_BACKUP_ERR_FILE_OPEN;

    struct EepromBackupHeader header;
    EepromBackupResult result;
    uint32_t crc;
    if (card.readFile(&header, sizeof(header)) != sizeof(header)) {
        result = EEPROM_BACKUP_ERR_FILE_READ;
    } else if (header.magic != EEPROM_BACKUP_MAGIC) {
        result = EEPROM_BACKUP_ERR_INVALID_MAGIC;
    } else if (header.version != EEPROM_BACKUP_VERSION) {
        result = EEPROM_BACKUP_ERR_VERSION_MISMATCH;
    } else {
        result = calculate_sd_data_crc32(&crc);
        if (result == EEPROM_BACKUP_OK && crc != header.crc32) result = EEPROM_BACKUP_ERR_CRC_MISMATCH;
    }
    card.closefile();

    if (result == EEPROM_BACKUP_OK && out_header) {
        memcpy(out_header, &header, sizeof(header));
    }
    return result;
}

//! @brief Write the current EEPROM to a backup file, then read it back to verify it
static EepromBackupResult write_backup_file(const char *name) {
    if (!card.mounted) return EEPROM_BACKUP_ERR_NO_SD;

    struct EepromBackupHeader header;
    header.magic = EEPROM_BACKUP_MAGIC;
    header.version = EEPROM_BACKUP_VERSION;
    get_firmware_version_string(header.fw_version);
    header.timestamp = 0;
    header.crc32 = calculate_eeprom_crc32();
    header.printer_type = PRINTER_TYPE;
    memset(header.reserved, 0, sizeof(header.reserved));

    if (!card.openFileWriteBinary(name)) return EEPROM_BACKUP_ERR_FILE_OPEN;

    EepromBackupResult result = EEPROM_BACKUP_ERR_FILE_WRITE;
    if (card.writeFile(&header, sizeof(header)) == sizeof(header)) {
        result = write_eeprom_to_sd();
    }
    if (!card.closeFileBinary() && result == EEPROM_BACKUP_OK) {
        result = EEPROM_BACKUP_ERR_FILE_WRITE;
    }
    if (result == EEPROM_BACKUP_OK) {
        result = verify_backup_file(name, nullptr);
    }
    return result;
}

EepromBackupResult backup_eeprom_to_sd() {
    // Written under another name first: a failed write must not destroy the previous backup
    EepromBackupResult result = write_backup_file(EEPROM_BACKUP_NEW_FILENAME);
    if (result == EEPROM_BACKUP_OK && !card.moveFileBinary(EEPROM_BACKUP_NEW_FILENAME, EEPROM_BACKUP_FILENAME)) {
        result = EEPROM_BACKUP_ERR_FILE_WRITE;
    }
    return result;
}

EepromBackupResult verify_eeprom_backup(struct EepromBackupHeader *out_header) {
    return verify_backup_file(EEPROM_BACKUP_FILENAME, out_header);
}

//! @brief Write a verified backup file to the EEPROM
//! @param snapshot save the current EEPROM to EEPROM.TMP first (kept if it already exists: it
//! then holds the state before an interrupted restore, which must not be overwritten)
static EepromBackupResult restore_from_file(const char *name, bool validate_version, bool snapshot) {
    struct EepromBackupHeader header;
    EepromBackupResult result = verify_backup_file(name, &header);
    if (result != EEPROM_BACKUP_OK) return result;

    if (header.printer_type && header.printer_type != PRINTER_TYPE) {
        return EEPROM_BACKUP_ERR_PRINTER_MISMATCH;
    }

    if (validate_version) {
        uint8_t current_version[12];
        get_firmware_version_string(current_version);
        if (memcmp(current_version, header.fw_version, 12) != 0) {
            return EEPROM_BACKUP_ERR_VERSION_MISMATCH;
        }
    }

    bool created = false;
    if (snapshot && !card.fileExistsBinary(EEPROM_BACKUP_TEMP_FILENAME)) {
        result = write_backup_file(EEPROM_BACKUP_TEMP_FILENAME);
        if (result != EEPROM_BACKUP_OK) return result;
        created = true;
    }

    bool written = false;
    if (!card.openFileReadBinary(name)) {
        result = EEPROM_BACKUP_ERR_FILE_OPEN;
    } else {
        if (card.readFile(&header, sizeof(header)) != sizeof(header)) {
            result = EEPROM_BACKUP_ERR_FILE_READ;
        } else {
            written = true;
            result = read_sd_to_eeprom(header.crc32);
        }
        card.closefile();
    }
    if (created && !written) {
        // EEPROM untouched: a kept snapshot would be mistaken for an interrupted restore later
        card.removeFileBinary(EEPROM_BACKUP_TEMP_FILENAME);
    }

    if (result == EEPROM_BACKUP_OK) {
        // Clear Power Panic recovery flags to prevent false recovery attempts after reboot
        eeprom_update_byte((uint8_t*)EEPROM_UVLO, PowerPanic::NO_PENDING_RECOVERY);
        eeprom_update_byte((uint8_t*)EEPROM_UVLO_Z_LIFTED, 0);
    }
    return result;
}

EepromBackupResult restore_eeprom_from_sd(bool validate_version) {
    EepromBackupResult result = restore_from_file(EEPROM_BACKUP_FILENAME, validate_version, true);
    if (result == EEPROM_BACKUP_OK) {
        // completed: the snapshot becomes the undo point
        card.moveFileBinary(EEPROM_BACKUP_TEMP_FILENAME, EEPROM_BACKUP_UNDO_FILENAME);
    }
    return result;
}

EepromBackupResult undo_eeprom_restore() {
    const bool interrupted = card.fileExistsBinary(EEPROM_BACKUP_TEMP_FILENAME);
    EepromBackupResult result = restore_from_file(
        interrupted ? EEPROM_BACKUP_TEMP_FILENAME : EEPROM_BACKUP_UNDO_FILENAME, false, false);
    if (result == EEPROM_BACKUP_OK && interrupted) {
        card.moveFileBinary(EEPROM_BACKUP_TEMP_FILENAME, EEPROM_BACKUP_UNDO_FILENAME);
    }
    return result;
}

#endif // EEPROM_BACKUP_ENABLE
