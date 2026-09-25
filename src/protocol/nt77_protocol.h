#ifndef NT77_PROTOCOL_H
#define NT77_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NT77_PACKET_SIZE 48u
#define NT77_PAYLOAD_OFFSET 7u
#define NT77_PAYLOAD_SIZE 40u
#define NT77_CHECKSUM_OFFSET 47u
#define NT77_STORAGE_SIZE 1024u

#define NT77_FRAME_SIMPLE 0x80u
#define NT77_FRAME_ADDRESSED 0x81u

#define NT77_COMMAND_WRITE 0x67u
#define NT77_COMMAND_HARDWARE_ID 0x73u
#define NT77_COMMAND_VERSION 0x75u
#define NT77_COMMAND_SERIAL 0x76u
#define NT77_COMMAND_READ 0x77u
#define NT77_COMMAND_LOGIN 0x80u
#define NT77_COMMAND_LOGOUT 0x92u
#define NT77_COMMAND_HANDSHAKE 0xF0u

#define NT77_STATUS_SUCCESS 0xAAu
#define NT77_STATUS_NOT_CONFIGURED 0xE1u
#define NT77_STATUS_AUTH_FAILED 0xE2u
#define NT77_STATUS_NOT_LOGGED_IN 0xE3u
#define NT77_STATUS_OUT_OF_RANGE 0xE4u
#define NT77_STATUS_BAD_LENGTH 0xE5u
#define NT77_STATUS_UNKNOWN_COMMAND 0xEFu

typedef enum NT77_PROCESS_RESULT {
    NT77_PROCESS_OK = 0,
    NT77_PROCESS_BAD_ARGUMENT = 1,
    NT77_PROCESS_BAD_FRAME = 2
} NT77_PROCESS_RESULT;

typedef struct NT77_PROTOCOL_STATE {
    uint8_t Serial[32];
    uint8_t LoginDigest[16];
    uint8_t HardwareId[24];
    uint8_t Storage[NT77_STORAGE_SIZE];
    uint8_t SessionKey[4];
    bool LoggedIn;
    bool SerialConfigured;
    bool LoginConfigured;
    bool HardwareIdConfigured;
} NT77_PROTOCOL_STATE;

extern const uint8_t Nt77BootstrapKey[4];
extern const uint8_t Nt77DefaultSessionKey[4];

uint8_t Nt77Checksum(const uint8_t *buffer, size_t length);

void Nt77Crypt(
    uint8_t *buffer,
    size_t length,
    const uint8_t key[4],
    bool decrypt);

void Nt77ProtocolInitialize(NT77_PROTOCOL_STATE *state);
void Nt77ProtocolResetSession(NT77_PROTOCOL_STATE *state);

/* Single policy query used by the driver IOCTL dispatcher. */
bool Nt77ProtocolAllowsExternalConfiguration(void);

NT77_PROCESS_RESULT Nt77ProtocolProcess(
    NT77_PROTOCOL_STATE *state,
    const uint8_t encrypted_request[NT77_PACKET_SIZE],
    uint8_t encrypted_response[NT77_PACKET_SIZE]);

#ifdef __cplusplus
}
#endif

#endif
