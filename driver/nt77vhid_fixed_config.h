#ifndef NT77VHID_FIXED_CONFIG_H
#define NT77VHID_FIXED_CONFIG_H

/* Compile-time identity used by the Fixed|x64 driver build. */
#include <stdint.h>

#define NT77_FIXED_SERIAL_SIZE 32u
#define NT77_FIXED_LOGIN_DIGEST_SIZE 16u
#define NT77_FIXED_HARDWARE_ID_SIZE 24u
#define NT77_FIXED_STORAGE_SIZE 1024u

#define NT77_FIXED_LOGIN_INPUT "e3fa6e1532e309911f636592078f6287"

static const uint8_t Nt77FixedSerial[NT77_FIXED_SERIAL_SIZE] = {
    'D', 'N', 'G', 'u', 'a', 'r', 'd', '.', 'X', 'B', 'y', 't', 'e', 'S',
    'a', 'f', 'e', '.', 'L', 't', 'd', '.',
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const uint8_t Nt77FixedLoginDigest[NT77_FIXED_LOGIN_DIGEST_SIZE] = {
    0x45, 0x93, 0xB4, 0xA5, 0x6F, 0x95, 0x22, 0xC0,
    0x0E, 0xCC, 0x07, 0xBA, 0x8E, 0xBB, 0x66, 0x8A
};

static const uint8_t Nt77FixedHardwareId[NT77_FIXED_HARDWARE_ID_SIZE] = {
    0xB4, 0x38, 0xC8, 0x47, 0x2F, 0x3A, 0x49, 0x4E,
    0xA9, 0x5E, 0x58, 0x5C, 0xA8, 0x1E, 0xE4, 0x68,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

#endif
