#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#include "nt77vhid_public.h"

static bool NtSuccess(NTSTATUS status)
{
    return status >= 0;
}

static int HexDigit(wchar_t value)
{
    if (value >= L'0' && value <= L'9') {
        return value - L'0';
    }
    if (value >= L'a' && value <= L'f') {
        return value - L'a' + 10;
    }
    if (value >= L'A' && value <= L'F') {
        return value - L'A' + 10;
    }
    return -1;
}

bool ParseHex(const std::wstring& text, std::vector<std::uint8_t>& bytes)
{
    std::wstring compact;

    bytes.clear();
    compact.reserve(text.size());
    for (wchar_t value : text) {
        if (!std::iswspace(value)) {
            compact.push_back(value);
        }
    }
    if ((compact.size() & 1u) != 0u) {
        return false;
    }

    bytes.reserve(compact.size() / 2u);
    for (std::size_t i = 0; i < compact.size(); i += 2u) {
        const int high = HexDigit(compact[i]);
        const int low = HexDigit(compact[i + 1u]);
        if (high < 0 || low < 0) {
            bytes.clear();
            return false;
        }
        bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return true;
}

bool ParseHardwareIdInput(
    const std::wstring& text,
    std::array<std::uint8_t, NT77VHID_HARDWARE_ID_SIZE>& hardwareId)
{
    std::vector<std::uint8_t> bytes;

    hardwareId.fill(0);
    if (!ParseHex(text, bytes) ||
        (bytes.size() != 16u && bytes.size() != hardwareId.size())) {
        return false;
    }
    std::copy(bytes.begin(), bytes.end(), hardwareId.begin());
    return true;
}

bool ComputeMd5(const std::uint8_t* data, std::size_t length,
                std::array<std::uint8_t, 16>& digest)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD resultLength = 0;
    std::vector<std::uint8_t> hashObject;
    bool success = false;

    if (data == nullptr ||
        length > (std::numeric_limits<ULONG>::max)()) {
        return false;
    }
    if (!NtSuccess(BCryptOpenAlgorithmProvider(
            &algorithm, BCRYPT_MD5_ALGORITHM, nullptr, 0))) {
        return false;
    }
    if (!NtSuccess(BCryptGetProperty(
            algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
            &resultLength, 0))) {
        goto Cleanup;
    }
    hashObject.resize(objectLength);
    if (!NtSuccess(BCryptCreateHash(
            algorithm, &hash, hashObject.data(), objectLength,
            nullptr, 0, 0))) {
        goto Cleanup;
    }
    if (!NtSuccess(BCryptHashData(
            hash, const_cast<PUCHAR>(data), static_cast<ULONG>(length), 0))) {
        goto Cleanup;
    }
    if (!NtSuccess(BCryptFinishHash(
            hash, digest.data(), static_cast<ULONG>(digest.size()), 0))) {
        goto Cleanup;
    }
    success = true;

Cleanup:
    if (hash != nullptr) {
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return success;
}

static bool AsciiBytes(const std::wstring& text,
                       std::vector<std::uint8_t>& bytes,
                       std::size_t maximum)
{
    bytes.clear();
    if (text.size() > maximum) {
        return false;
    }
    bytes.reserve(text.size());
    for (wchar_t value : text) {
        if (value > 0x7F) {
            bytes.clear();
            return false;
        }
        bytes.push_back(static_cast<std::uint8_t>(value));
    }
    return true;
}

static bool ComputeLoginDigest(
    const std::wstring& login,
    std::array<std::uint8_t, NT77VHID_LOGIN_DIGEST_SIZE>& digest)
{
    std::array<std::uint8_t, 256> input{};
    std::vector<std::uint8_t> ascii;

    if (!AsciiBytes(login, ascii, 128u)) {
        return false;
    }
    std::copy(ascii.begin(), ascii.end(), input.begin());
    return ComputeMd5(input.data(), input.size(), digest);
}

static bool ParseU16(const std::wstring& text, std::uint16_t& value)
{
    wchar_t* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::wcstoul(text.c_str(), &end, 0);
    if (errno != 0 || end == text.c_str() || *end != L'\0' ||
        parsed > 0xFFFFul) {
        return false;
    }
    value = static_cast<std::uint16_t>(parsed);
    return true;
}

static HANDLE OpenControlDevice()
{
    return CreateFileW(
        NT77VHID_CONTROL_PATH,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
}

static bool SendIoctl(
    DWORD code,
    const void* input,
    DWORD inputSize,
    void* output,
    DWORD outputSize,
    DWORD* bytesReturned)
{
    HANDLE device = OpenControlDevice();
    DWORD localBytes = 0;
    BOOL ok;

    if (device == INVALID_HANDLE_VALUE) {
        std::cerr << "Cannot open Nt77VhidControl, Win32 error "
                  << GetLastError() << "\n";
        return false;
    }
    ok = DeviceIoControl(
        device, code,
        const_cast<void*>(input), inputSize,
        output, outputSize,
        &localBytes, nullptr);
    if (!ok) {
        std::cerr << "DeviceIoControl failed, Win32 error "
                  << GetLastError() << "\n";
    }
    CloseHandle(device);
    if (bytesReturned != nullptr) {
        *bytesReturned = localBytes;
    }
    return ok != FALSE;
}

static void PrintHex(const UCHAR* data, std::size_t length)
{
    const std::ios_base::fmtflags flags = std::wcout.flags();
    const wchar_t fill = std::wcout.fill();
    for (std::size_t i = 0; i < length; ++i) {
        std::wcout << std::hex << std::uppercase << std::setw(2)
                   << std::setfill(L'0') << static_cast<unsigned>(data[i]);
    }
    std::wcout.flags(flags);
    std::wcout.fill(fill);
}

static bool SetSerial(const std::wstring& text)
{
    NT77VHID_SERIAL_CONFIG config{};
    std::vector<std::uint8_t> ascii;
    if (!AsciiBytes(text, ascii, NT77VHID_SERIAL_SIZE)) {
        std::cerr << "Serial must be ASCII and at most 32 bytes.\n";
        return false;
    }
    std::copy(ascii.begin(), ascii.end(), config.Bytes);
    return SendIoctl(IOCTL_NT77_SET_SERIAL,
                     &config, sizeof(config), nullptr, 0, nullptr);
}

static bool SetLoginText(const std::wstring& text)
{
    NT77VHID_LOGIN_CONFIG config{};
    std::array<std::uint8_t, NT77VHID_LOGIN_DIGEST_SIZE> digest{};
    if (!ComputeLoginDigest(text, digest)) {
        std::cerr << "Login text must be ASCII and at most 128 bytes.\n";
        return false;
    }
    std::copy(digest.begin(), digest.end(), config.Digest);
    return SendIoctl(IOCTL_NT77_SET_LOGIN_MD5,
                     &config, sizeof(config), nullptr, 0, nullptr);
}

static bool SetLoginMd5(const std::wstring& text)
{
    NT77VHID_LOGIN_CONFIG config{};
    std::vector<std::uint8_t> bytes;
    if (!ParseHex(text, bytes) || bytes.size() != sizeof(config.Digest)) {
        std::cerr << "Login digest must contain exactly 16 hex bytes.\n";
        return false;
    }
    std::copy(bytes.begin(), bytes.end(), config.Digest);
    return SendIoctl(IOCTL_NT77_SET_LOGIN_MD5,
                     &config, sizeof(config), nullptr, 0, nullptr);
}

static bool SetHardwareId(const std::wstring& text)
{
    NT77VHID_HARDWARE_ID_CONFIG config{};
    std::array<std::uint8_t, NT77VHID_HARDWARE_ID_SIZE> hardwareId{};
    if (!ParseHardwareIdInput(text, hardwareId)) {
        std::cerr << "Hardware ID must contain 16 or 24 hex bytes.\n";
        return false;
    }
    std::copy(hardwareId.begin(), hardwareId.end(), config.Bytes);
    return SendIoctl(IOCTL_NT77_SET_HARDWARE_ID,
                     &config, sizeof(config), nullptr, 0, nullptr);
}

static bool WriteStorage(
    std::uint16_t offset,
    const std::vector<std::uint8_t>& data)
{
    const std::size_t header = offsetof(NT77VHID_STORAGE_WRITE, Data);
    std::vector<std::uint8_t> request(header + data.size());
    NT77VHID_STORAGE_WRITE* write =
        reinterpret_cast<NT77VHID_STORAGE_WRITE*>(request.data());

    if (data.empty() || data.size() > NT77VHID_STORAGE_SIZE ||
        static_cast<std::size_t>(offset) + data.size() >
            NT77VHID_STORAGE_SIZE) {
        std::cerr << "Storage write must be non-empty and stay within 0..1023.\n";
        return false;
    }
    write->Offset = offset;
    write->Length = static_cast<USHORT>(data.size());
    std::copy(data.begin(), data.end(), write->Data);
    return SendIoctl(IOCTL_NT77_WRITE_STORAGE,
                     request.data(), static_cast<DWORD>(request.size()),
                     nullptr, 0, nullptr);
}

static bool ShowStatus()
{
    NT77VHID_STATUS status{};
    DWORD bytes = 0;
    if (!SendIoctl(IOCTL_NT77_QUERY_STATUS,
                   nullptr, 0, &status, sizeof(status), &bytes) ||
        bytes != sizeof(status)) {
        return false;
    }

    std::wcout << L"serialConfigured="
               << static_cast<unsigned>(status.SerialConfigured)
               << L" loginConfigured="
               << static_cast<unsigned>(status.LoginConfigured)
               << L" hardwareIdConfigured="
               << static_cast<unsigned>(status.HardwareIdConfigured)
               << L" loggedIn=" << static_cast<unsigned>(status.LoggedIn)
               << L"\nserial=";
    for (UCHAR value : status.Serial) {
        if (value == 0) {
            break;
        }
        std::wcout << static_cast<wchar_t>(value);
    }
    std::wcout << L"\nhardwareId=";
    PrintHex(status.HardwareId, sizeof(status.HardwareId));
    std::wcout << L"\n";
    return true;
}

static bool SelfTest()
{
    std::vector<std::uint8_t> bytes;
    std::array<std::uint8_t, 16> digest{};
    std::array<std::uint8_t, NT77VHID_HARDWARE_ID_SIZE> hardwareId{};
    static const std::array<std::uint8_t, 16> expected = {
        0x90, 0x01, 0x50, 0x98, 0x3C, 0xD2, 0x4F, 0xB0,
        0xD6, 0x96, 0x3F, 0x7D, 0x28, 0xE1, 0x7F, 0x72};
    static const std::uint8_t abc[] = {'a', 'b', 'c'};

    if (!ParseHex(L"00a1FF", bytes) ||
        bytes != std::vector<std::uint8_t>({0x00, 0xA1, 0xFF})) {
        return false;
    }
    if (ParseHex(L"0", bytes) || ParseHex(L"GG", bytes)) {
        return false;
    }
    if (!ComputeMd5(abc, sizeof(abc), digest) || digest != expected) {
        return false;
    }
    if (!ComputeLoginDigest(L"abc", digest)) {
        return false;
    }
    if (!ParseHardwareIdInput(
            L"b438c8472f3a494ea95e585ca81ee468", hardwareId)) {
        return false;
    }
    static const std::array<std::uint8_t, 16> expectedHardwareId = {
        0xB4, 0x38, 0xC8, 0x47, 0x2F, 0x3A, 0x49, 0x4E,
        0xA9, 0x5E, 0x58, 0x5C, 0xA8, 0x1E, 0xE4, 0x68};
    if (!std::equal(expectedHardwareId.begin(), expectedHardwareId.end(),
                    hardwareId.begin()) ||
        !std::all_of(hardwareId.begin() + expectedHardwareId.size(),
                     hardwareId.end(),
                     [](std::uint8_t value) { return value == 0; })) {
        return false;
    }
    std::wcout << L"nt77ctl self-test: all tests passed\n";
    return true;
}

static void PrintUsage()
{
    std::cout
        << "Usage:\n"
        << "  nt77ctl --self-test\n"
        << "  nt77ctl serial <ASCII, <=32 bytes>\n"
        << "  nt77ctl login <ASCII, <=128 bytes>\n"
        << "  nt77ctl login-md5 <16 hex bytes>\n"
        << "  nt77ctl hardware-id <16 or 24 hex bytes>\n"
        << "  nt77ctl write <offset> <hex data>\n"
        << "  nt77ctl write-file <offset> <path>\n"
        << "  nt77ctl clear-storage\n"
        << "  nt77ctl status\n";
}

static int FinishCommand(bool success, const char* successMessage)
{
    if (success) {
        std::cout << "OK: " << successMessage << "\n";
        return 0;
    }
    PrintUsage();
    return 1;
}

int wmain(int argc, wchar_t** argv)
{
    const std::wstring command = argc > 1 ? argv[1] : L"";

    if (argc == 2 && command == L"--self-test") {
        return SelfTest() ? 0 : 1;
    }
    if (argc == 3 && command == L"serial") {
        return FinishCommand(SetSerial(argv[2]), "serial configured");
    }
    if (argc == 3 && command == L"login") {
        return FinishCommand(SetLoginText(argv[2]), "login configured");
    }
    if (argc == 3 && command == L"login-md5") {
        return FinishCommand(SetLoginMd5(argv[2]), "login digest configured");
    }
    if (argc == 3 && command == L"hardware-id") {
        return FinishCommand(SetHardwareId(argv[2]), "hardware ID configured");
    }
    if (argc == 4 && command == L"write") {
        std::uint16_t offset = 0;
        std::vector<std::uint8_t> data;
        if (!ParseU16(argv[2], offset) || !ParseHex(argv[3], data)) {
            std::cerr << "Invalid offset or hexadecimal data.\n";
            PrintUsage();
            return 1;
        }
        return FinishCommand(WriteStorage(offset, data), "storage written");
    }
    if (argc == 4 && command == L"write-file") {
        std::uint16_t offset = 0;
        if (!ParseU16(argv[2], offset)) {
            std::cerr << "Invalid offset.\n";
            PrintUsage();
            return 1;
        }
        std::ifstream stream(argv[3], std::ios::binary);
        if (!stream) {
            std::cerr << "Cannot read input file.\n";
            PrintUsage();
            return 1;
        }
        std::vector<std::uint8_t> data{
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
        return FinishCommand(WriteStorage(offset, data), "storage written");
    }
    if (argc == 2 && command == L"clear-storage") {
        return FinishCommand(
            SendIoctl(IOCTL_NT77_CLEAR_STORAGE,
                      nullptr, 0, nullptr, 0, nullptr),
            "storage cleared");
    }
    if (argc == 2 && command == L"status") {
        return ShowStatus() ? 0 : 1;
    }

    PrintUsage();
    return 2;
}
