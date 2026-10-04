//! @file
//! @date 2025-01-17
//! @author Claude Code
//! @brief EEPROM backup/restore to/from SD card

#ifndef EEPROM_BACKUP_H
#define EEPROM_BACKUP_H

#include "Configuration.h"

#ifdef EEPROM_BACKUP_ENABLE

#include <stdint.h>
#include <stdbool.h>

// Magic bytes for EEPROM backup file validation
#define EEPROM_BACKUP_MAGIC 0xEE50524DUL  // "EEPRM" in hex

// Backup file version for compatibility checks
#define EEPROM_BACKUP_VERSION 1

// EEPROM size on ATmega2560
#define EEPROM_SIZE 4096

// Backup files, in the root directory of the SD card
#define EEPROM_BACKUP_FILENAME "EEPROM.BAK"      // user backup
#define EEPROM_BACKUP_NEW_FILENAME "EEPROM.NEW"  // backup being written, renamed to .BAK once verified
#define EEPROM_BACKUP_TEMP_FILENAME "EEPROM.TMP" // EEPROM before a restore, exists while it runs
#define EEPROM_BACKUP_UNDO_FILENAME "EEPROM.UND" // EEPROM before the last completed restore

// Backup file header structure (34 bytes)
struct EepromBackupHeader {
    uint32_t magic;           // Magic bytes for validation (0xEE50524D)
    uint8_t version;          // Backup format version
    uint8_t fw_version[12];   // Firmware version string (e.g., "3.12.0")
    uint32_t timestamp;       // Backup timestamp (optional, can be 0)
    uint32_t crc32;           // CRC32 of EEPROM data (4096 bytes)
    uint16_t printer_type;    // PRINTER_TYPE of the source printer, 0 in older backups (not checked)
    uint8_t reserved[7];      // Reserved for future use
} __attribute__((packed));

// Total backup file size: header + EEPROM data
#define EEPROM_BACKUP_FILE_SIZE (sizeof(struct EepromBackupHeader) + EEPROM_SIZE)

// Return codes for backup/restore operations
enum EepromBackupResult : uint8_t {
    EEPROM_BACKUP_OK = 0,              // Operation successful
    EEPROM_BACKUP_ERR_NO_SD,           // SD card not mounted
    EEPROM_BACKUP_ERR_FILE_OPEN,       // Failed to open file
    EEPROM_BACKUP_ERR_FILE_WRITE,      // Failed to write to file
    EEPROM_BACKUP_ERR_FILE_READ,       // Failed to read from file
    EEPROM_BACKUP_ERR_INVALID_MAGIC,   // Invalid magic bytes in backup
    EEPROM_BACKUP_ERR_VERSION_MISMATCH,// Backup version incompatible
    EEPROM_BACKUP_ERR_CRC_MISMATCH,    // CRC validation failed
    EEPROM_BACKUP_ERR_ROLLBACK_FAILED, // Rollback after failed restore
    EEPROM_BACKUP_ERR_PRINTER_MISMATCH,// Backup made on another printer type
};

//! @brief Backup EEPROM contents to SD card
//! @details Creates/overwrites /EEPROM.BAK on SD card with full EEPROM dump
//! @return EepromBackupResult status code
EepromBackupResult backup_eeprom_to_sd();

//! @brief Restore EEPROM contents from SD card backup
//! @details Reads /EEPROM.BAK from SD and writes to EEPROM with validation. The serial number and
//! the firmware crash flag are never restored. A backup from another printer type is refused.
//! @note Saves the current EEPROM first (EEPROM.TMP, then EEPROM.UND once completed)
//! @param validate_version If true, reject backups from different firmware versions
//! @return EepromBackupResult status code
EepromBackupResult restore_eeprom_from_sd(bool validate_version = false);

//! @brief Undo the last restore
//! @details Writes back the EEPROM saved before the last restore (EEPROM.TMP if that restore was
//! interrupted, EEPROM.UND otherwise)
//! @return EepromBackupResult status code
EepromBackupResult undo_eeprom_restore();

//! @brief Verify integrity of EEPROM backup file on SD
//! @details Checks magic bytes, version, and CRC32 without modifying EEPROM
//! @param out_header Optional pointer to receive backup header info
//! @return EepromBackupResult status code
EepromBackupResult verify_eeprom_backup(struct EepromBackupHeader *out_header = nullptr);

//! @brief Calculate CRC32 of EEPROM contents
//! @details Used internally for backup validation
//! @return CRC32 checksum of full EEPROM (4096 bytes)
uint32_t calculate_eeprom_crc32();

//! @brief Get firmware version string for backup header
//! @details Extracts version from FW_VERSION define
//! @param buffer Output buffer (must be at least 12 bytes)
void get_firmware_version_string(uint8_t *buffer);

#endif // EEPROM_BACKUP_ENABLE

#endif // EEPROM_BACKUP_H
