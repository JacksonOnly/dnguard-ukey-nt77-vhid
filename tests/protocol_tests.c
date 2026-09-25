#include "nt77_protocol.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const uint8_t kBootstrapKey[4] = {0x37, 0xB9, 0xEA, 0x15};

static void build_request(
    uint8_t command,
    uint8_t length_or_subcommand,
    uint16_t address,
    const uint8_t *payload,
    size_t payload_length,
    const uint8_t key[4],
    uint8_t output[NT77_PACKET_SIZE])
{
    assert(payload_length <= NT77_PAYLOAD_SIZE);
    memset(output, 0, NT77_PACKET_SIZE);
    output[0] = (command == NT77_COMMAND_READ || command == NT77_COMMAND_WRITE)
                    ? NT77_FRAME_ADDRESSED
                    : NT77_FRAME_SIMPLE;
    output[1] = command;
    output[2] = length_or_subcommand;
    output[3] = (uint8_t)(address >> 8);
    output[4] = (uint8_t)address;
    output[5] = 0x5A;
    output[6] = 0xC3;
    if (payload_length != 0) {
        memcpy(&output[NT77_PAYLOAD_OFFSET], payload, payload_length);
    }
    output[NT77_CHECKSUM_OFFSET] =
        Nt77Checksum(output, NT77_CHECKSUM_OFFSET);
    Nt77Crypt(&output[1], NT77_PACKET_SIZE - 1, key, false);
}

static void decrypt_response(
    const uint8_t encrypted[NT77_PACKET_SIZE],
    const uint8_t key[4],
    uint8_t plain[NT77_PACKET_SIZE])
{
    memcpy(plain, encrypted, NT77_PACKET_SIZE);
    Nt77Crypt(&plain[1], NT77_PACKET_SIZE - 1, key, true);
    assert(plain[NT77_CHECKSUM_OFFSET] ==
           Nt77Checksum(plain, NT77_CHECKSUM_OFFSET));
}

static void handshake(NT77_PROTOCOL_STATE *state)
{
    uint8_t request[NT77_PACKET_SIZE];
    uint8_t response[NT77_PACKET_SIZE];
    uint8_t plain[NT77_PACKET_SIZE];

    build_request(NT77_COMMAND_HANDSHAKE, 0, 0, NULL, 0,
                  kBootstrapKey, request);
    assert(Nt77ProtocolProcess(state, request, response) == NT77_PROCESS_OK);
    decrypt_response(response, kBootstrapKey, plain);
    assert(plain[2] == NT77_STATUS_SUCCESS);
    assert(memcmp(&plain[NT77_PAYLOAD_OFFSET], state->SessionKey, 4) == 0);
}

static void test_checksum(void)
{
    uint8_t frame[NT77_PACKET_SIZE] = {0};
    frame[0] = 0x80;
    frame[1] = NT77_COMMAND_VERSION;
    assert(Nt77Checksum(frame, 47) ==
           (uint8_t)~(uint8_t)(0x80u ^ NT77_COMMAND_VERSION));
}

static void test_cipher_fixed_vector_and_round_trip(void)
{
    uint8_t bytes[4] = {0xF0, 0x00, 0x01, 0x02};
    const uint8_t expected[4] = {0x71, 0xC8, 0x23, 0x34};

    Nt77Crypt(bytes, sizeof(bytes), kBootstrapKey, false);
    assert(memcmp(bytes, expected, sizeof(bytes)) == 0);
    Nt77Crypt(bytes, sizeof(bytes), kBootstrapKey, true);
    assert(bytes[0] == 0xF0 && bytes[1] == 0x00 &&
           bytes[2] == 0x01 && bytes[3] == 0x02);
}

static void test_handshake_and_version(void)
{
    NT77_PROTOCOL_STATE state;
    uint8_t request[NT77_PACKET_SIZE];
    uint8_t response[NT77_PACKET_SIZE];
    uint8_t plain[NT77_PACKET_SIZE];

    Nt77ProtocolInitialize(&state);
    handshake(&state);
    build_request(NT77_COMMAND_VERSION, 0, 0, NULL, 0,
                  state.SessionKey, request);
    assert(Nt77ProtocolProcess(&state, request, response) == NT77_PROCESS_OK);
    decrypt_response(response, state.SessionKey, plain);
    assert(plain[2] == NT77_STATUS_SUCCESS);
    assert(plain[7] == 0x50 && plain[8] == 0 &&
           plain[9] == 0 && plain[10] == 0);
}

static void test_serial_response(void)
{
    NT77_PROTOCOL_STATE state;
    uint8_t request[NT77_PACKET_SIZE];
    uint8_t response[NT77_PACKET_SIZE];
    uint8_t plain[NT77_PACKET_SIZE];
    const char serial[] = "NT77-TEST-0001";

    Nt77ProtocolInitialize(&state);
    memcpy(state.Serial, serial, sizeof(serial) - 1);
    state.SerialConfigured = true;
    handshake(&state);
    build_request(NT77_COMMAND_SERIAL, 0, 0, NULL, 0,
                  state.SessionKey, request);
    assert(Nt77ProtocolProcess(&state, request, response) == NT77_PROCESS_OK);
    decrypt_response(response, state.SessionKey, plain);
    assert(plain[2] == NT77_STATUS_SUCCESS);
    assert(memcmp(&plain[7], serial, sizeof(serial) - 1) == 0);
}

static void login(NT77_PROTOCOL_STATE *state, const uint8_t digest[16],
                  uint8_t *status_out)
{
    uint8_t request[NT77_PACKET_SIZE];
    uint8_t response[NT77_PACKET_SIZE];
    uint8_t plain[NT77_PACKET_SIZE];

    build_request(NT77_COMMAND_LOGIN, 0, 0, digest, 16,
                  state->SessionKey, request);
    assert(Nt77ProtocolProcess(state, request, response) == NT77_PROCESS_OK);
    decrypt_response(response, state->SessionKey, plain);
    *status_out = plain[2];
}

static void test_login_success_and_failure(void)
{
    NT77_PROTOCOL_STATE state;
    uint8_t status;
    uint8_t bad_digest[16];

    Nt77ProtocolInitialize(&state);
    for (size_t i = 0; i < sizeof(state.LoginDigest); ++i) {
        state.LoginDigest[i] = (uint8_t)(i + 1);
    }
    state.LoginConfigured = true;
    handshake(&state);

    memcpy(bad_digest, state.LoginDigest, sizeof(bad_digest));
    bad_digest[0] ^= 0xFF;
    login(&state, bad_digest, &status);
    assert(status != NT77_STATUS_SUCCESS);
    assert(!state.LoggedIn);

    login(&state, state.LoginDigest, &status);
    assert(status == NT77_STATUS_SUCCESS);
    assert(state.LoggedIn);
}

static void test_repeated_handshake_preserves_login(void)
{
    NT77_PROTOCOL_STATE state;
    uint8_t status;

    Nt77ProtocolInitialize(&state);
    for (size_t index = 0; index < sizeof(state.LoginDigest); ++index) {
        state.LoginDigest[index] = (uint8_t)(0x30u + index);
    }
    state.LoginConfigured = true;

    handshake(&state);
    login(&state, state.LoginDigest, &status);
    assert(status == NT77_STATUS_SUCCESS);
    assert(state.LoggedIn);

    handshake(&state);
    assert(state.LoggedIn);
}

static void test_read_write_bounds_and_authentication(void)
{
    NT77_PROTOCOL_STATE state;
    uint8_t request[NT77_PACKET_SIZE];
    uint8_t response[NT77_PACKET_SIZE];
    uint8_t plain[NT77_PACKET_SIZE];
    uint8_t payload[NT77_PAYLOAD_SIZE];

    Nt77ProtocolInitialize(&state);
    state.LoginConfigured = true;
    handshake(&state);
    build_request(NT77_COMMAND_READ, 4, 100, NULL, 0,
                  state.SessionKey, request);
    assert(Nt77ProtocolProcess(&state, request, response) == NT77_PROCESS_OK);
    decrypt_response(response, state.SessionKey, plain);
    assert(plain[2] == NT77_STATUS_NOT_LOGGED_IN);

    state.LoggedIn = true;
    for (size_t i = 0; i < sizeof(payload); ++i) {
        payload[i] = (uint8_t)(0x80u + i);
    }
    build_request(NT77_COMMAND_WRITE, NT77_PAYLOAD_SIZE, 200,
                  payload, sizeof(payload), state.SessionKey, request);
    assert(Nt77ProtocolProcess(&state, request, response) == NT77_PROCESS_OK);
    decrypt_response(response, state.SessionKey, plain);
    assert(plain[2] == NT77_STATUS_SUCCESS);
    assert(memcmp(&state.Storage[200], payload, sizeof(payload)) == 0);

    build_request(NT77_COMMAND_READ, NT77_PAYLOAD_SIZE, 200, NULL, 0,
                  state.SessionKey, request);
    assert(Nt77ProtocolProcess(&state, request, response) == NT77_PROCESS_OK);
    decrypt_response(response, state.SessionKey, plain);
    assert(plain[2] == NT77_STATUS_SUCCESS);
    assert(memcmp(&plain[7], payload, sizeof(payload)) == 0);

    build_request(NT77_COMMAND_READ, 5, 1020, NULL, 0,
                  state.SessionKey, request);
    assert(Nt77ProtocolProcess(&state, request, response) == NT77_PROCESS_OK);
    decrypt_response(response, state.SessionKey, plain);
    assert(plain[2] == NT77_STATUS_OUT_OF_RANGE);
}

static void test_hardware_id_and_logout(void)
{
    NT77_PROTOCOL_STATE state;
    uint8_t request[NT77_PACKET_SIZE];
    uint8_t response[NT77_PACKET_SIZE];
    uint8_t plain[NT77_PACKET_SIZE];

    Nt77ProtocolInitialize(&state);
    for (size_t i = 0; i < sizeof(state.HardwareId); ++i) {
        state.HardwareId[i] = (uint8_t)(0xA0u + i);
    }
    state.HardwareIdConfigured = true;
    state.LoggedIn = true;
    handshake(&state);
    assert(state.LoggedIn);

    build_request(NT77_COMMAND_HARDWARE_ID, 0, 0, NULL, 0,
                  state.SessionKey, request);
    assert(Nt77ProtocolProcess(&state, request, response) == NT77_PROCESS_OK);
    decrypt_response(response, state.SessionKey, plain);
    assert(plain[2] == NT77_STATUS_SUCCESS);
    assert(memcmp(&plain[7], state.HardwareId, sizeof(state.HardwareId)) == 0);

    build_request(NT77_COMMAND_LOGOUT, 0, 0, NULL, 0,
                  state.SessionKey, request);
    assert(Nt77ProtocolProcess(&state, request, response) == NT77_PROCESS_OK);
    decrypt_response(response, state.SessionKey, plain);
    assert(plain[2] == NT77_STATUS_SUCCESS);
    assert(!state.LoggedIn);
}

static void test_hardware_id_does_not_require_login(void)
{
    NT77_PROTOCOL_STATE state;
    uint8_t request[NT77_PACKET_SIZE];
    uint8_t response[NT77_PACKET_SIZE];
    uint8_t plain[NT77_PACKET_SIZE];

    Nt77ProtocolInitialize(&state);
    for (size_t i = 0; i < sizeof(state.HardwareId); ++i) {
        state.HardwareId[i] = (uint8_t)(0x40u + i);
    }
    state.HardwareIdConfigured = true;
    handshake(&state);
    assert(!state.LoggedIn);

    build_request(NT77_COMMAND_HARDWARE_ID, 0, 0, NULL, 0,
                  state.SessionKey, request);
    assert(Nt77ProtocolProcess(&state, request, response) == NT77_PROCESS_OK);
    decrypt_response(response, state.SessionKey, plain);
    assert(plain[2] == NT77_STATUS_SUCCESS);
    assert(memcmp(&plain[NT77_PAYLOAD_OFFSET],
                  state.HardwareId,
                  sizeof(state.HardwareId)) == 0);
    assert(!state.LoggedIn);
}

static void test_bad_checksum_and_unknown_command(void)
{
    NT77_PROTOCOL_STATE state;
    uint8_t request[NT77_PACKET_SIZE];
    uint8_t response[NT77_PACKET_SIZE];
    uint8_t plain[NT77_PACKET_SIZE];

    Nt77ProtocolInitialize(&state);
    handshake(&state);

    build_request(0x44, 0, 0, NULL, 0, state.SessionKey, request);
    assert(Nt77ProtocolProcess(&state, request, response) == NT77_PROCESS_OK);
    decrypt_response(response, state.SessionKey, plain);
    assert(plain[2] == NT77_STATUS_UNKNOWN_COMMAND);

    build_request(NT77_COMMAND_VERSION, 0, 0, NULL, 0,
                  state.SessionKey, request);
    /* Corrupt the encrypted checksum byte itself. A middle ciphertext bit
       affects two adjacent plaintext bytes and can cancel in this XOR checksum. */
    request[NT77_CHECKSUM_OFFSET] ^= 0x01;
    assert(Nt77ProtocolProcess(&state, request, response) ==
           NT77_PROCESS_BAD_FRAME);
}

int main(void)
{
    test_checksum();
    test_cipher_fixed_vector_and_round_trip();
    test_handshake_and_version();
    test_serial_response();
    test_login_success_and_failure();
    test_repeated_handshake_preserves_login();
    test_read_write_bounds_and_authentication();
    test_hardware_id_and_logout();
    test_hardware_id_does_not_require_login();
    test_bad_checksum_and_unknown_command();
    puts("nt77_protocol_tests: all tests passed");
    return 0;
}
