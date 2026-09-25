#include "nt77_protocol.h"
#include "nt77vhid_fixed_config.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void build_request(uint8_t command, uint8_t length, uint16_t address,
                          const uint8_t *payload, size_t payload_length,
                          const uint8_t key[4], uint8_t output[NT77_PACKET_SIZE])
{
    memset(output, 0, NT77_PACKET_SIZE);
    output[0] = (command == NT77_COMMAND_READ || command == NT77_COMMAND_WRITE)
                    ? NT77_FRAME_ADDRESSED
                    : NT77_FRAME_SIMPLE;
    output[1] = command;
    output[2] = length;
    output[3] = (uint8_t)(address >> 8);
    output[4] = (uint8_t)address;
    if (payload != NULL && payload_length != 0u) {
        memcpy(&output[NT77_PAYLOAD_OFFSET], payload, payload_length);
    }
    output[NT77_CHECKSUM_OFFSET] = Nt77Checksum(output, NT77_CHECKSUM_OFFSET);
    Nt77Crypt(&output[1], NT77_PACKET_SIZE - 1u, key, false);
}

static void decrypt_response(const uint8_t encrypted[NT77_PACKET_SIZE],
                             const uint8_t key[4],
                             uint8_t plain[NT77_PACKET_SIZE])
{
    memcpy(plain, encrypted, NT77_PACKET_SIZE);
    Nt77Crypt(&plain[1], NT77_PACKET_SIZE - 1u, key, true);
    assert(plain[NT77_CHECKSUM_OFFSET] ==
           Nt77Checksum(plain, NT77_CHECKSUM_OFFSET));
}

int main(void)
{
    NT77_PROTOCOL_STATE state;
    uint8_t request[NT77_PACKET_SIZE];
    uint8_t response[NT77_PACKET_SIZE];
    uint8_t plain[NT77_PACKET_SIZE];
    const uint8_t bootstrap[4] = {0x37, 0xB9, 0xEA, 0x15};

    Nt77ProtocolInitialize(&state);
    assert(!Nt77ProtocolAllowsExternalConfiguration());
    assert(state.SerialConfigured && state.LoginConfigured &&
           state.HardwareIdConfigured && !state.LoggedIn);
    assert(memcmp(state.Serial, Nt77FixedSerial, sizeof(state.Serial)) == 0);
    assert(memcmp(state.LoginDigest, Nt77FixedLoginDigest,
                  sizeof(state.LoginDigest)) == 0);
    assert(memcmp(state.HardwareId, Nt77FixedHardwareId,
                  sizeof(state.HardwareId)) == 0);
    for (size_t i = 0; i < sizeof(state.Storage); ++i) {
        assert(state.Storage[i] == 0xFFu);
    }

    build_request(NT77_COMMAND_HANDSHAKE, 0, 0, NULL, 0, bootstrap, request);
    assert(Nt77ProtocolProcess(&state, request, response) == NT77_PROCESS_OK);
    decrypt_response(response, bootstrap, plain);
    assert(plain[2] == NT77_STATUS_SUCCESS);

    build_request(NT77_COMMAND_LOGIN, 0, 0, Nt77FixedLoginDigest,
                  sizeof(Nt77FixedLoginDigest), state.SessionKey, request);
    assert(Nt77ProtocolProcess(&state, request, response) == NT77_PROCESS_OK);
    decrypt_response(response, state.SessionKey, plain);
    assert(plain[2] == NT77_STATUS_SUCCESS && state.LoggedIn);

    build_request(NT77_COMMAND_READ, 16, 128, NULL, 0, state.SessionKey,
                  request);
    assert(Nt77ProtocolProcess(&state, request, response) == NT77_PROCESS_OK);
    decrypt_response(response, state.SessionKey, plain);
    assert(plain[2] == NT77_STATUS_SUCCESS);
    for (size_t i = 0; i < 16u; ++i) {
        assert(plain[NT77_PAYLOAD_OFFSET + i] == 0xFFu);
    }

    puts("nt77_protocol_fixed_tests: all tests passed");
    return 0;
}
