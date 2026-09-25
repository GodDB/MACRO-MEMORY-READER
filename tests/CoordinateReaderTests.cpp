#include "CoordinateRequest.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <vector>

struct FakeMemory
{
    std::map<ULONG_PTR, unsigned char> bytes;
    mutable std::vector<ULONG_PTR> reads;

    template <typename T>
    void Put(ULONG_PTR address, T value)
    {
        const auto* data = reinterpret_cast<const unsigned char*>(&value);
        for (SIZE_T i = 0; i < sizeof(T); ++i) bytes[address + i] = data[i];
    }

    template <typename T>
    MemoryReadResult<T> Read(ULONG_PTR address) const noexcept
    {
        reads.push_back(address);
        MemoryReadResult<T> result{};
        unsigned char data[sizeof(T)]{};
        for (SIZE_T i = 0; i < sizeof(T); ++i)
        {
            const auto it = bytes.find(address + i);
            if (it == bytes.end())
            {
                result.status = STATUS_PARTIAL_COPY;
                return result;
            }
            data[i] = it->second;
            ++result.bytesRead;
        }
        std::memcpy(&result.value, data, sizeof(T));
        result.status = STATUS_SUCCESS;
        return result;
    }
};

constexpr ULONG_PTR Root = 0x009CB97C;

template <typename Pointer = ULONG>
FakeMemory MakeMemory(ULONG_PTR root = Root)
{
    FakeMemory memory;
    memory.Put(root, Pointer{0x10000});
    memory.Put(0x1000C, Pointer{0x20000});
    memory.Put(0x20044, Pointer{0x30000});
    memory.Put(0x30214, LONG{-123});
    memory.Put(0x30218, LONG{456});
    return memory;
}

int main()
{
    using Reader = BasicDarkEdenCoordinateReader<FakeMemory>;
    auto memory = MakeMemory();
    Reader reader(memory);
    auto xy = reader.Read(Root);
    assert(xy.Succeeded() && xy.value.x == -123 && xy.value.y == 456);
    assert((memory.reads == std::vector<ULONG_PTR>{Root, 0x1000C, 0x20044, 0x30214}));
    assert(reader.ReadX(Root).value == -123);
    assert(reader.ReadY(Root).value == 456);
    memory.Put(0x30214, LONG{0});
    const auto zero = reader.ReadX(Root);
    assert(zero.Succeeded() && zero.value == 0);

    auto relocated = MakeMemory(Root + 0x00400000);
    const auto relocatedResult = Reader(relocated).ReadFromModule(0x00400000);
    assert(relocatedResult.Succeeded() && relocatedResult.value.y == 456);
    assert(reader.ReadFromModule(0).status == STATUS_INVALID_ADDRESS);
    assert(reader.ReadFromModule(0xFFFFFFF0).status == STATUS_INTEGER_OVERFLOW);
    assert(reader.Read(0).status == STATUS_INVALID_ADDRESS);

    // Missing pointer data at each of the three dereferences must stop traversal.
    const ULONG_PTR slots[] = {Root, 0x1000C, 0x20044};
    for (SIZE_T i = 0; i < 3; ++i)
    {
        auto broken = MakeMemory();
        broken.bytes.erase(slots[i]);
        const auto result = Reader(broken).Read(Root);
        assert(!result.Succeeded() && result.status == STATUS_PARTIAL_COPY);
        assert(result.bytesRead == 0 && result.value.x == 0 && result.value.y == 0);
        assert(broken.reads.size() == i + 1);
    }
    auto nullPointer = MakeMemory();
    nullPointer.Put(0x1000C, ULONG{0});
    assert(Reader(nullPointer).Read(Root).status == STATUS_INVALID_ADDRESS);
    auto overflow = MakeMemory();
    overflow.Put(0x20044, ULONG{0xFFFFFFF0});
    assert(Reader(overflow).Read(Root).status == STATUS_INTEGER_OVERFLOW);

    auto partial = MakeMemory();
    partial.bytes.erase(0x30218);
    const auto partialResult = Reader(partial).Read(Root);
    assert(!partialResult.Succeeded() && partialResult.bytesRead == 4);
    assert(partialResult.value.x == 0 && partialResult.value.y == 0);

    auto floats = MakeMemory();
    floats.Put(0x30214, 1.25f);
    floats.Put(0x30218, -2.5f);
    using FloatReader = BasicDarkEdenCoordinateReader<FakeMemory, ULONG, float>;
    const auto floatResult = FloatReader(floats).Read(Root);
    assert(floatResult.Succeeded() && floatResult.value.x == 1.25f &&
           floatResult.value.y == -2.5f);

    auto wide = MakeMemory<ULONGLONG>();
    wide.Put(Root, ULONGLONG{0x100010000});
    wide.Put(0x10001000C, ULONGLONG{0x20000});
    wide.bytes.erase(0x1000C); // No valid chain at the truncated 32-bit address.
    using WideReader = BasicDarkEdenCoordinateReader<FakeMemory, ULONGLONG>;
    const auto wideResult = WideReader(wide).Read(Root);
    assert(wideResult.Succeeded() && wideResult.value.x == -123);

    assert(reader.Read().Succeeded()); // Confirmed default absolute address.
    const auto all = ReadAllDarkEdenCoordinates(floats);
    assert(all.pointer32Int.Succeeded() && all.pointer32Float.Succeeded());
    assert(all.pointer32Int.value.x == 1067450368); // Raw bits of 1.25f.
    assert(all.pointer32Float.value.x == 1.25f && all.pointer32Float.value.y == -2.5f);
    assert(!all.pointer64Int.Succeeded() && !all.pointer64Float.Succeeded());
    assert(all.pointer64Int.status == all.pointer64Float.status);
    const auto allWide = ReadAllDarkEdenCoordinates(wide);
    assert(!allWide.pointer32Int.Succeeded() && allWide.pointer64Int.Succeeded());
    const auto allPartial = ReadAllDarkEdenCoordinates(partial);
    assert(!allPartial.pointer32Float.Succeeded());
    assert(allPartial.pointer32Float.bytesRead == 4);
    assert(allPartial.pointer32Float.value.x == 0.0f);
    // Exercise the exact dispatch used by Example.cpp/IOCTL, including isolated axes.
    using namespace DarkEdenProtocol;
    Request request{Version, sizeof(Request), 4, Root, Axis::X, 4, ValueType::Int32, 0};
    Response response{};
    auto requestMemory = MakeMemory();
    requestMemory.bytes.erase(0x30218); // X must succeed even if Y cannot be read.
    DispatchCoordinateRequest(requestMemory, request, response);
    assert(response.status == 0 && response.bytesRead == 4);
    assert(response.xBits == static_cast<uint32_t>(-123) && response.yBits == 0);
    request.axis = Axis::Y;
    DispatchCoordinateRequest(requestMemory, request, response);
    assert(response.status == STATUS_PARTIAL_COPY && response.xBits == 0 && response.yBits == 0);
    request.axis = Axis::Both;
    DispatchCoordinateRequest(requestMemory, request, response);
    assert(response.status == STATUS_PARTIAL_COPY && response.bytesRead == 4);
    assert(response.xBits == 0 && response.yBits == 0);
    requestMemory = MakeMemory();
    DispatchCoordinateRequest(requestMemory, request, response);
    assert(response.status == 0 && response.bytesRead == 8 && response.yBits == 456);
    request.axis = Axis::Y;
    requestMemory.bytes.erase(0x30214); // Y must not request X's address.
    DispatchCoordinateRequest(requestMemory, request, response);
    assert(response.status == 0 && response.xBits == 0 && response.yBits == 456);
    request.axis = Axis::Both;
    request.valueType = ValueType::Float32;
    DispatchCoordinateRequest(floats, request, response);
    assert(response.status == 0 && response.xBits == 1067450368 && response.yBits == 0xC0200000);
    request.pointerBytes = 8;
    request.valueType = ValueType::Int32;
    DispatchCoordinateRequest(wide, request, response);
    assert(response.status == 0 && response.bytesRead == 8 && response.yBits == 456);
    auto wideFloats = MakeMemory<ULONGLONG>();
    wideFloats.Put(0x30214, 1.25f); wideFloats.Put(0x30218, -2.5f);
    request.valueType = ValueType::Float32;
    DispatchCoordinateRequest(wideFloats, request, response);
    assert(response.status == 0 && response.xBits == 1067450368 && response.yBits == 0xC0200000);
    request.version = 1;
    DispatchCoordinateRequest(wide, request, response);
    assert(response.status == STATUS_INVALID_PARAMETER && response.xBits == 0 && response.bytesRead == 0);
    request.version = Version;
    request.processId = UINT64_MAX;
    assert(!ValidRequest(request));
    request.processId = 0;
    assert(!ValidRequest(request));
    request.processId = 0x100000000ULL;
    assert(!ValidRequest(request));
    request.processId = 4;
    request.axis = static_cast<Axis>(9);
    assert(!ValidRequest(request));
    request.axis = Axis::Both;
    request.pointerBytes = 3;
    assert(!ValidRequest(request));
    request.pointerBytes = 4;
    request.reserved = 1;
    assert(!ValidRequest(request));
    std::cout << "Coordinate reader and kernel request dispatch tests passed.\n";
}
