//! @file
//! @date 2025-01-17
//! @author Claude Code
//! @brief EEPROM backup/restore implementation

#include "eeprom_backup.h"
#include "cardreader.h"
#include "Configuration.h"
#include "Marlin.h"
#include <avr/eeprom.h>
#include <string.h>

// External CardReader instance
extern CardReader card;

// CRC32 lookup table (generated with polynomial 0xEDB88320)
static const uint32_t crc32_table[256] PROGMEM = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
    0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
    0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
    0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
    0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
    0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
    0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
    0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
    0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
    0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
    0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
    0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
    0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
    0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
    0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
    0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
    0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
    0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
    0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
    0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
    0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

//! @brief Calculate CRC32 with byte-by-byte updates
//! @param crc Current CRC value
//! @param data Data byte
//! @return Updated CRC value
static inline uint32_t crc32_update(uint32_t crc, uint8_t data) {
    uint8_t table_index = (uint8_t)(crc ^ data);
    return pgm_read_dword(&crc32_table[table_index]) ^ (crc >> 8);
}

uint32_t calculate_eeprom_crc32() {
    uint32_t crc = 0xFFFFFFFF;

    // Calculate CRC of entire EEPROM (4096 bytes)
    for (uint16_t addr = 0; addr < EEPROM_SIZE; addr++) {
        uint8_t byte_val = eeprom_read_byte((uint8_t*)addr);
        crc = crc32_update(crc, byte_val);
    }

    return ~crc; // Final XOR
}

void get_firmware_version_string(uint8_t *buffer) {
    // Extract version from FW_VERSION (defined in Configuration.h)
    // Example: FW_VERSION "3.12.0" → buffer "3.12.0\0..."
    const char *version = FW_VERSION;
    uint8_t i = 0;

    // Copy up to 11 characters (leave 1 byte for null terminator)
    while (version[i] != '\0' && i < 11) {
        buffer[i] = version[i];
        i++;
    }

    // Pad with zeros
    while (i < 12) {
        buffer[i] = 0;
        i++;
    }
}

EepromBackupResult backup_eeprom_to_sd() {
    // Check if SD card is mounted
    if (!card.mounted) {
        SERIAL_ERRORLNPGM("EEPROM backup failed: SD not mounted");
        return EEPROM_BACKUP_ERR_NO_SD;
    }

    // Calculate CRC32 of current EEPROM contents
    SERIAL_ECHOLNPGM("Calculating EEPROM CRC32...");
    uint32_t crc = calculate_eeprom_crc32();

    // Prepare backup header
    struct EepromBackupHeader header;
    header.magic = EEPROM_BACKUP_MAGIC;
    header.version = EEPROM_BACKUP_VERSION;
    get_firmware_version_string(header.fw_version);
    header.timestamp = 0; // Optional: could use RTC if available
    header.crc32 = crc;
    memset(header.reserved, 0, sizeof(header.reserved));

    // Open backup file for writing (overwrites existing)
    SERIAL_ECHOLNPGM("Opening " EEPROM_BACKUP_FILENAME " for writing...");
    card.openFileWrite(EEPROM_BACKUP_FILENAME);
    if (!card.isFileOpen()) {
        SERIAL_ERRORLNPGM("Failed to open backup file");
        return EEPROM_BACKUP_ERR_FILE_OPEN;
    }

    // Write header (32 bytes)
    if (card.writeFile(&header, sizeof(header)) != sizeof(header)) {
        SERIAL_ERRORLNPGM("Failed to write header");
        card.closefile();
        return EEPROM_BACKUP_ERR_FILE_WRITE;
    }

    // Write EEPROM contents in 64-byte chunks
    SERIAL_ECHOLNPGM("Writing EEPROM data...");
    uint8_t buffer[64];
    for (uint16_t addr = 0; addr < EEPROM_SIZE; addr += 64) {
        // Read 64 bytes from EEPROM
        for (uint8_t i = 0; i < 64; i++) {
            buffer[i] = eeprom_read_byte((uint8_t*)(addr + i));
        }

        // Write to SD card
        if (card.writeFile(buffer, 64) != 64) {
            SERIAL_ERRORLNPGM("Failed to write EEPROM data");
            card.closefile();
            return EEPROM_BACKUP_ERR_FILE_WRITE;
        }

        // Show progress every 512 bytes (~12%)
        if ((addr % 512) == 0) {
            SERIAL_ECHO(addr);
            SERIAL_ECHOLNPGM(" bytes written");
        }
    }

    card.closefile();

    SERIAL_ECHOLNPGM("EEPROM backup complete!");
    SERIAL_ECHOPGM("File size: ");
    SERIAL_ECHO(EEPROM_BACKUP_FILE_SIZE);
    SERIAL_ECHOLNPGM(" bytes");

    return EEPROM_BACKUP_OK;
}

EepromBackupResult verify_eeprom_backup(struct EepromBackupHeader *out_header) {
    // Check if SD card is mounted
    if (!card.mounted) {
        SERIAL_ERRORLNPGM("Verify failed: SD not mounted");
        return EEPROM_BACKUP_ERR_NO_SD;
    }

    // Check if backup file exists
    if (!card.FileExists(EEPROM_BACKUP_FILENAME)) {
        SERIAL_ERRORLNPGM("Backup file not found");
        return EEPROM_BACKUP_ERR_FILE_OPEN;
    }

    // Open backup file for reading
    card.openFileReadFilteredGcode(EEPROM_BACKUP_FILENAME);
    if (!card.isFileOpen()) {
        SERIAL_ERRORLNPGM("Failed to open backup file");
        return EEPROM_BACKUP_ERR_FILE_OPEN;
    }

    // Read header
    struct EepromBackupHeader header;
    if (card.readFile(&header, sizeof(header)) != sizeof(header)) {
        SERIAL_ERRORLNPGM("Failed to read header");
        card.closefile();
        return EEPROM_BACKUP_ERR_FILE_READ;
    }

    // Verify magic bytes
    if (header.magic != EEPROM_BACKUP_MAGIC) {
        SERIAL_ERRORLNPGM("Invalid magic bytes");
        card.closefile();
        return EEPROM_BACKUP_ERR_INVALID_MAGIC;
    }

    // Check version
    if (header.version != EEPROM_BACKUP_VERSION) {
        SERIAL_ERRORLNPGM("Version mismatch");
        card.closefile();
        return EEPROM_BACKUP_ERR_VERSION_MISMATCH;
    }

    // Calculate CRC32 of backup data
    SERIAL_ECHOLNPGM("Verifying CRC32...");
    uint32_t crc = 0xFFFFFFFF;
    uint8_t buffer[64];

    for (uint16_t addr = 0; addr < EEPROM_SIZE; addr += 64) {
        if (card.readFile(buffer, 64) != 64) {
            SERIAL_ERRORLNPGM("Failed to read backup data");
            card.closefile();
            return EEPROM_BACKUP_ERR_FILE_READ;
        }

        // Update CRC
        for (uint8_t i = 0; i < 64; i++) {
            crc = crc32_update(crc, buffer[i]);
        }
    }
    crc = ~crc;

    card.closefile();

    // Verify CRC matches header
    if (crc != header.crc32) {
        SERIAL_ERRORLNPGM("CRC mismatch - backup corrupted!");
        return EEPROM_BACKUP_ERR_CRC_MISMATCH;
    }

    SERIAL_ECHOLNPGM("Backup verification OK!");
    SERIAL_ECHOPGM("Firmware version: ");
    for (uint8_t i = 0; i < 12 && header.fw_version[i] != 0; i++) {
        SERIAL_ECHO((char)header.fw_version[i]);
    }
    SERIAL_ECHOLN("");

    // Copy header to output if requested
    if (out_header != nullptr) {
        memcpy(out_header, &header, sizeof(header));
    }

    return EEPROM_BACKUP_OK;
}

//! @brief Create temporary backup of current EEPROM to .TMP file
//! @details Used before restore to allow manual rollback if needed
//! @return EepromBackupResult status code
static EepromBackupResult create_temp_eeprom_backup() {
    // Check if SD card is mounted
    if (!card.mounted) {
        SERIAL_ERRORLNPGM("Temp backup failed: SD not mounted");
        return EEPROM_BACKUP_ERR_NO_SD;
    }

    // Calculate CRC32 of current EEPROM
    uint32_t crc = calculate_eeprom_crc32();

    // Prepare temporary backup header
    struct EepromBackupHeader header;
    header.magic = EEPROM_BACKUP_MAGIC;
    header.version = EEPROM_BACKUP_VERSION;
    get_firmware_version_string(header.fw_version);
    header.timestamp = 0;
    header.crc32 = crc;
    memset(header.reserved, 0, sizeof(header.reserved));

    // Open temporary backup file
    SERIAL_ECHOLNPGM("Creating " EEPROM_BACKUP_TEMP_FILENAME "...");
    card.openFileWrite(EEPROM_BACKUP_TEMP_FILENAME);
    if (!card.isFileOpen()) {
        SERIAL_ERRORLNPGM("Failed to open temp file");
        return EEPROM_BACKUP_ERR_FILE_OPEN;
    }

    // Write header
    if (card.writeFile(&header, sizeof(header)) != sizeof(header)) {
        SERIAL_ERRORLNPGM("Failed to write temp header");
        card.closefile();
        return EEPROM_BACKUP_ERR_FILE_WRITE;
    }

    // Write EEPROM contents in 64-byte chunks
    uint8_t buffer[64];
    for (uint16_t addr = 0; addr < EEPROM_SIZE; addr += 64) {
        // Read 64 bytes from EEPROM
        for (uint8_t i = 0; i < 64; i++) {
            buffer[i] = eeprom_read_byte((uint8_t*)(addr + i));
        }

        // Write to SD card
        if (card.writeFile(buffer, 64) != 64) {
            SERIAL_ERRORLNPGM("Failed to write temp data");
            card.closefile();
            return EEPROM_BACKUP_ERR_FILE_WRITE;
        }
    }

    card.closefile();
    SERIAL_ECHOLNPGM("Temporary backup created");
    return EEPROM_BACKUP_OK;
}

EepromBackupResult restore_eeprom_from_sd(bool validate_version) {
    // Step 1: Verify backup integrity
    SERIAL_ECHOLNPGM("Verifying backup integrity...");
    struct EepromBackupHeader header;
    EepromBackupResult result = verify_eeprom_backup(&header);
    if (result != EEPROM_BACKUP_OK) {
        SERIAL_ERRORLNPGM("Backup verification failed - aborting restore");
        return result;
    }

    // Step 2: Check firmware version compatibility
    if (validate_version) {
        uint8_t current_version[12];
        get_firmware_version_string(current_version);

        if (memcmp(current_version, header.fw_version, 12) != 0) {
            SERIAL_ERRORLNPGM("Warning: Firmware version mismatch!");
            SERIAL_ECHOPGM("Current: ");
            for (uint8_t i = 0; i < 12 && current_version[i] != 0; i++) {
                SERIAL_ECHO((char)current_version[i]);
            }
            SERIAL_ECHOPGM(", Backup: ");
            for (uint8_t i = 0; i < 12 && header.fw_version[i] != 0; i++) {
                SERIAL_ECHO((char)header.fw_version[i]);
            }
            SERIAL_ECHOLN("");

            // Return error if strict version validation requested
            return EEPROM_BACKUP_ERR_VERSION_MISMATCH;
        }
    }

    // Step 3: Create temporary backup before modification
    result = create_temp_eeprom_backup();
    if (result != EEPROM_BACKUP_OK) {
        SERIAL_ERRORLNPGM("Failed to create temp backup - aborting restore");
        return result;
    }

    // Step 4: Open backup file and restore EEPROM
    SERIAL_ECHOLNPGM("Restoring EEPROM from backup...");
    card.openFileReadFilteredGcode(EEPROM_BACKUP_FILENAME);
    if (!card.isFileOpen()) {
        SERIAL_ERRORLNPGM("Failed to open backup file");
        return EEPROM_BACKUP_ERR_FILE_OPEN;
    }

    // Skip header
    struct EepromBackupHeader dummy_header;
    if (card.readFile(&dummy_header, sizeof(dummy_header)) != sizeof(dummy_header)) {
        SERIAL_ERRORLNPGM("Failed to read header");
        card.closefile();
        return EEPROM_BACKUP_ERR_FILE_READ;
    }

    // Write EEPROM data from backup
    uint8_t buffer[64];
    for (uint16_t addr = 0; addr < EEPROM_SIZE; addr += 64) {
        // Read 64 bytes from SD
        if (card.readFile(buffer, 64) != 64) {
            SERIAL_ERRORLNPGM("Failed to read backup data");
            card.closefile();
            return EEPROM_BACKUP_ERR_FILE_READ;
        }

        // Write to EEPROM byte by byte
        for (uint8_t i = 0; i < 64; i++) {
            eeprom_write_byte((uint8_t*)(addr + i), buffer[i]);
        }

        // Show progress every 512 bytes
        if ((addr % 512) == 0) {
            SERIAL_ECHO(addr);
            SERIAL_ECHOLNPGM(" bytes restored");
        }
    }

    card.closefile();

    SERIAL_ECHOLNPGM("EEPROM restore complete!");
    SERIAL_ECHOLNPGM("Please restart the printer for changes to take effect.");

    return EEPROM_BACKUP_OK;
}
