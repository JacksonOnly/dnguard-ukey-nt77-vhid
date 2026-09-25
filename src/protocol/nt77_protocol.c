#include "nt77_protocol.h"

#include <string.h>

#include "../../driver/nt77vhid_fixed_config.h"

const uint8_t Nt77BootstrapKey[4] = {0x37, 0xB9, 0xEA, 0x15};
const uint8_t Nt77DefaultSessionKey[4] = {0x6D, 0x39, 0xA7, 0xC2};

uint8_t Nt77Checksum(const uint8_t *buffer, size_t length)
{
    uint8_t value = 0;
    size_t index;

    if (buffer == NULL || length < 2u) {
        return 0;
    }

    for (index = 0; index < length; ++index) {
        value ^= buffer[index];
    }
    return (uint8_t)~value;
}

void Nt77Crypt(
    uint8_t *buffer,
    size_t length,
    const uint8_t key[4],
    bool decrypt)
{
    uint8_t previous = 0xB6;
    size_t index;

    if (buffer == NULL || key == NULL || length < 2u) {
        return;
    }

    for (index = 0; index < length; ++index) {
        const uint8_t current = buffer[index];
        buffer[index] = (uint8_t)(previous ^ current ^ key[index & 3u]);
        previous = decrypt ? current : buffer[index];
    }
}

void Nt77ProtocolResetSession(NT77_PROTOCOL_STATE *state)
{
    if (state == NULL) {
        return;
    }
    memcpy(state->SessionKey, Nt77BootstrapKey, sizeof(state->SessionKey));
    state->LoggedIn = false;
}

void Nt77ProtocolInitialize(NT77_PROTOCOL_STATE *state)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    Nt77ProtocolResetSession(state);
#if defined(NT77VHID_FIXED_CONFIG)
    memcpy(state->Serial, Nt77FixedSerial, sizeof(state->Serial));
    memcpy(state->LoginDigest, Nt77FixedLoginDigest,
           sizeof(state->LoginDigest));
    memcpy(state->HardwareId, Nt77FixedHardwareId,
           sizeof(state->HardwareId));
    memset(state->Storage, 0xFF, sizeof(state->Storage));
    state->SerialConfigured = true;
    state->LoginConfigured = true;
    state->HardwareIdConfigured = true;
#endif
}

bool Nt77ProtocolAllowsExternalConfiguration(void)
{
#if defined(NT77VHID_FIXED_CONFIG)
    return false;
#else
    return true;
#endif
}

static bool nt77_decrypt_and_validate(
    const uint8_t encrypted[NT77_PACKET_SIZE],
    const uint8_t key[4],
    uint8_t plain[NT77_PACKET_SIZE])
{
    memcpy(plain, encrypted, NT77_PACKET_SIZE);
    Nt77Crypt(&plain[1], NT77_PACKET_SIZE - 1u, key, true);
    return plain[NT77_CHECKSUM_OFFSET] ==
               Nt77Checksum(plain, NT77_CHECKSUM_OFFSET) &&
           (plain[0] == NT77_FRAME_SIMPLE ||
            plain[0] == NT77_FRAME_ADDRESSED);
}

static bool nt77_keys_equal(const uint8_t left[4], const uint8_t right[4])
{
    return memcmp(left, right, 4u) == 0;
}

static void nt77_set_status(uint8_t response[NT77_PACKET_SIZE], uint8_t status)
{
    response[2] = status;
}

NT77_PROCESS_RESULT Nt77ProtocolProcess(
    NT77_PROTOCOL_STATE *state,
    const uint8_t encrypted_request[NT77_PACKET_SIZE],
    uint8_t encrypted_response[NT77_PACKET_SIZE])
{
    uint8_t request[NT77_PACKET_SIZE];
    uint8_t response[NT77_PACKET_SIZE];
    uint8_t key_used[4];
    uint8_t request_length;
    uint16_t address;
    bool handshake_completed = false;

    if (state == NULL || encrypted_request == NULL || encrypted_response == NULL) {
        return NT77_PROCESS_BAD_ARGUMENT;
    }

    memcpy(key_used, state->SessionKey, sizeof(key_used));
    if (!nt77_decrypt_and_validate(encrypted_request, key_used, request)) {
        if (nt77_keys_equal(key_used, Nt77BootstrapKey) ||
            !nt77_decrypt_and_validate(
                encrypted_request, Nt77BootstrapKey, request) ||
            request[1] != NT77_COMMAND_HANDSHAKE) {
            return NT77_PROCESS_BAD_FRAME;
        }
        memcpy(key_used, Nt77BootstrapKey, sizeof(key_used));
    }

    request_length = request[2];
    address = (uint16_t)(((uint16_t)request[3] << 8) | request[4]);

    memcpy(response, request, sizeof(response));
    memset(&response[NT77_PAYLOAD_OFFSET], 0, NT77_PAYLOAD_SIZE);
    nt77_set_status(response, NT77_STATUS_SUCCESS);

    switch (request[1]) {
    case NT77_COMMAND_HANDSHAKE:
        memcpy(&response[NT77_PAYLOAD_OFFSET], Nt77DefaultSessionKey, 4u);
        handshake_completed = true;
        break;

    case NT77_COMMAND_VERSION:
        response[NT77_PAYLOAD_OFFSET] = 0x50;
        break;

    case NT77_COMMAND_SERIAL:
        if (!state->SerialConfigured) {
            nt77_set_status(response, NT77_STATUS_NOT_CONFIGURED);
        } else {
            memcpy(&response[NT77_PAYLOAD_OFFSET],
                   state->Serial, sizeof(state->Serial));
        }
        break;

    case NT77_COMMAND_LOGIN:
        if (!state->LoginConfigured) {
            nt77_set_status(response, NT77_STATUS_NOT_CONFIGURED);
            state->LoggedIn = false;
        } else if (memcmp(&request[NT77_PAYLOAD_OFFSET],
                          state->LoginDigest,
                          sizeof(state->LoginDigest)) != 0) {
            nt77_set_status(response, NT77_STATUS_AUTH_FAILED);
            state->LoggedIn = false;
        } else {
            state->LoggedIn = true;
        }
        break;

    case NT77_COMMAND_READ:
        if (!state->LoggedIn) {
            nt77_set_status(response, NT77_STATUS_NOT_LOGGED_IN);
        } else if (request_length > NT77_PAYLOAD_SIZE) {
            nt77_set_status(response, NT77_STATUS_BAD_LENGTH);
        } else if ((size_t)address + request_length > NT77_STORAGE_SIZE) {
            nt77_set_status(response, NT77_STATUS_OUT_OF_RANGE);
        } else {
            memcpy(&response[NT77_PAYLOAD_OFFSET],
                   &state->Storage[address], request_length);
        }
        break;

    case NT77_COMMAND_WRITE:
        if (!state->LoggedIn) {
            nt77_set_status(response, NT77_STATUS_NOT_LOGGED_IN);
        } else if (request_length > NT77_PAYLOAD_SIZE) {
            nt77_set_status(response, NT77_STATUS_BAD_LENGTH);
        } else if ((size_t)address + request_length > NT77_STORAGE_SIZE) {
            nt77_set_status(response, NT77_STATUS_OUT_OF_RANGE);
        } else {
            memcpy(&state->Storage[address],
                   &request[NT77_PAYLOAD_OFFSET], request_length);
        }
        break;

    case NT77_COMMAND_HARDWARE_ID:
        if (!state->HardwareIdConfigured) {
            nt77_set_status(response, NT77_STATUS_NOT_CONFIGURED);
        } else {
            memcpy(&response[NT77_PAYLOAD_OFFSET],
                   state->HardwareId, sizeof(state->HardwareId));
        }
        break;

    case NT77_COMMAND_LOGOUT:
        state->LoggedIn = false;
        break;

    default:
        nt77_set_status(response, NT77_STATUS_UNKNOWN_COMMAND);
        break;
    }

    response[NT77_CHECKSUM_OFFSET] =
        Nt77Checksum(response, NT77_CHECKSUM_OFFSET);
    Nt77Crypt(&response[1], NT77_PACKET_SIZE - 1u, key_used, false);
    memcpy(encrypted_response, response, NT77_PACKET_SIZE);

    if (handshake_completed) {
        memcpy(state->SessionKey,
               Nt77DefaultSessionKey,
               sizeof(state->SessionKey));
    }

    return NT77_PROCESS_OK;
}
