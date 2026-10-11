#include <ntifs.h>
#include "TextRequest.h"
#include <cassert>
#include <cstring>
#include <map>
#include <vector>

struct FakeMemory
{
    std::map<ULONG_PTR, unsigned char> bytes;
    mutable std::vector<ULONG_PTR> reads;
    template <typename T> void Put(ULONG_PTR address, T value)
    {
        const auto* source = reinterpret_cast<const unsigned char*>(&value);
        for (size_t i = 0; i < sizeof(T); ++i) bytes[address + i] = source[i];
    }
    template <typename T> struct Result
    {
        NTSTATUS status = STATUS_PARTIAL_COPY;
        size_t bytesRead = 0;
        T value{};
        bool Succeeded() const { return NT_SUCCESS(status) && bytesRead == sizeof(T); }
    };
    template <typename T> Result<T> Read(ULONG_PTR address) const
    {
        reads.push_back(address);
        Result<T> result;
        auto* target = reinterpret_cast<unsigned char*>(&result.value);
        for (size_t i = 0; i < sizeof(T); ++i)
        {
            const auto found = bytes.find(address + i);
            if (found == bytes.end()) return result;
            target[i] = found->second;
            ++result.bytesRead;
        }
        result.status = STATUS_SUCCESS;
        return result;
    }
};

int main()
{
    using namespace DarkEdenProtocol;
    FakeMemory memory;
    memory.Put<ULONG>(0x1000, 0x2000);
    memory.Put<unsigned char>(0x2000, 't');
    memory.Put<unsigned char>(0x2001, 'e');
    memory.Put<unsigned char>(0x2002, 's');
    memory.Put<unsigned char>(0x2003, 't');
    memory.Put<unsigned char>(0x2004, 0);
    TextResponse response{};
    ReadPointedText(memory, 0x1000, 1, response);
    assert(response.status == STATUS_SUCCESS && response.terminated == 1);
    assert(response.textAddress == 0x2000 && response.bytesRead == 4);
    assert(std::memcmp(response.data, "test", 4) == 0);

    memory.Put<ULONG>(0x1000, 0x3000);
    memory.Put<USHORT>(0x3000, L'A');
    memory.Put<USHORT>(0x3002, L'B');
    memory.Put<USHORT>(0x3004, 0);
    response = {};
    ReadPointedText(memory, 0x1000, 2, response);
    assert(response.status == STATUS_SUCCESS && response.bytesRead == 4 && response.terminated == 1);
    assert(response.data[0] == 'A' && response.data[1] == 0 && response.data[2] == 'B');

    memory.Put<ULONG>(0x1000, 0);
    response = {};
    ReadPointedText(memory, 0x1000, 1, response);
    assert(response.status == STATUS_INVALID_ADDRESS && response.bytesRead == 0);

    memory.Put<ULONG>(0x1000, 0x4000);
    response = {};
    ReadPointedText(memory, 0x1000, 1, response);
    assert(response.status == STATUS_PARTIAL_COPY && response.bytesRead == 0);

    // Reproduce the screenshot's 32-bit chain, ending at the inline string.
    static_assert(OtherCharacterNameOffset == 0x00AB31D0, "Screenshot root offset changed");
    constexpr ULONG_PTR root = 0x00400000 + OtherCharacterNameOffset;
    FakeMemory other;
    other.Put<ULONG>(root, 0x2C74C500);
    other.Put<ULONG>(0x2C74C534, 0x28EF42E0);
    other.Put<ULONG>(0x28EF42E0, 0x276DD650);
    other.Put<ULONG>(0x276DD664, 0x2DD4FEB8);
    other.Put<ULONG>(0x2DD503FC, 0x295E6740);
    other.Put<unsigned char>(0x295E6740, 'A');
    other.Put<unsigned char>(0x295E6741, 'B');
    other.Put<unsigned char>(0x295E6742, 0);
    response = {};
    ReadOtherCharacterText(other, root, 1, response);
    assert(response.status == STATUS_SUCCESS && response.terminated == 1);
    assert(response.textAddress == 0x295E6740 && response.bytesRead == 2);
    assert(std::memcmp(response.data, "AB", 2) == 0);
    assert((std::vector<ULONG_PTR>(other.reads.begin(), other.reads.begin() + 5) ==
            std::vector<ULONG_PTR>{root, 0x2C74C534, 0x28EF42E0, 0x276DD664, 0x2DD503FC}));
    other.Put<ULONG>(0x2DD503FC, 0);
    response = {};
    ReadOtherCharacterText(other, root, 1, response);
    assert(response.status == STATUS_INVALID_ADDRESS && response.bytesRead == 0);
    other.Put<ULONG>(0x2DD503FC, 0x295E6740);
    other.Put<ULONG>(0x276DD664, 0xFFFFFFFE);
    response = {};
    ReadOtherCharacterText(other, root, 1, response);
    assert(response.status == STATUS_INTEGER_OVERFLOW && response.bytesRead == 0);

    TextRequest request{Version, sizeof(TextRequest), 4, OtherCharacterNameOffset, 1, 1};
    assert(ValidTextRequest(request));
    request.reserved = 2;
    assert(!ValidTextRequest(request));
}
