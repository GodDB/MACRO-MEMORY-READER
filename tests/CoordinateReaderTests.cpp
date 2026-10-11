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
    // Direct reads never dereference the value or require adjacent Y memory.
    FakeMemory direct;
    const ULONG_PTR directAddress = 0x1409E3868ULL;
    request = {Version, sizeof(Request), 4, directAddress, Axis::X, 0, ValueType::Int32, 0};
    direct.Put(directAddress, LONG{-123});
    DispatchCoordinateRequest(direct, request, response);
    assert(response.status == STATUS_SUCCESS && response.bytesRead == 4);
    assert(response.xBits == static_cast<uint32_t>(-123) && response.yBits == 0);
    assert((direct.reads == std::vector<ULONG_PTR>{directAddress}));
    direct.Put(directAddress, LONG{0});
    DispatchCoordinateRequest(direct, request, response);
    assert(response.status == STATUS_SUCCESS && response.xBits == 0);
    direct.Put(directAddress, 1.25f);
    request.valueType = ValueType::Float32;
    DispatchCoordinateRequest(direct, request, response);
    assert(response.status == STATUS_SUCCESS && response.xBits == 0x3FA00000);
    direct.bytes.erase(directAddress + 2);
    DispatchCoordinateRequest(direct, request, response);
    assert(response.status == STATUS_PARTIAL_COPY && response.bytesRead == 2);
    assert(response.xBits == 0 && response.yBits == 0);
    request.axis = Axis::Y;
    assert(!ValidRequest(request));
    request.axis = Axis::Both;
    assert(!ValidRequest(request));
    request.axis = Axis::X;
    request.pointerBytes = 1;
    assert(!ValidRequest(request));
    FakeMemory single;
    request = {Version, sizeof(Request), 4, Root, Axis::X, 4, ValueType::Int32, 1};
    single.Put(Root, ULONG{0x12340});
    single.Put(0x12340, LONG{39});
    DispatchCoordinateRequest(single, request, response);
    assert(response.status == 0 && response.bytesRead == 4 && response.xBits == 39 && response.yBits == 0);
    assert((single.reads == std::vector<ULONG_PTR>{Root, 0x12340}));
    single.Put(0x12340, 1.25f);
    request.valueType = ValueType::Float32;
    DispatchCoordinateRequest(single, request, response);
    assert(response.status == 0 && response.xBits == 0x3FA00000);
    single.bytes.erase(0x12342);
    DispatchCoordinateRequest(single, request, response);
    assert(response.status == STATUS_PARTIAL_COPY && response.bytesRead == 2 && response.xBits == 0);
    single.Put(Root, ULONG{0});
    DispatchCoordinateRequest(single, request, response);
    assert(response.status == STATUS_INVALID_ADDRESS && response.bytesRead == 0);
    single.bytes.erase(Root + 2);
    DispatchCoordinateRequest(single, request, response);
    assert(response.status == STATUS_PARTIAL_COPY && response.bytesRead == 0);
    request.pointerBytes = 8;
    assert(!ValidRequest(request));
    request.pointerBytes = 4;
    request.reserved = 2;
    assert(!ValidRequest(request));
    // Screenshot path: read32(module + 009FB03C), +4, read32, +A8, read32, +20C.
    const ULONG_PTR xRoot = 0x00400000 + DefaultXOffset;
    FakeMemory xMemory;
    xMemory.Put(xRoot, ULONG{0x155D3880});
    xMemory.Put(0x155D3884, ULONG{0x27B20F98});
    xMemory.Put(0x27B21040, ULONG{0x2D6705A8});
    xMemory.Put(0x2D6707B4, LONG{32});
    request = {Version, sizeof(Request), 4, xRoot, Axis::X, 4, ValueType::Int32, 3};
    DispatchCoordinateRequest(xMemory, request, response);
    assert(response.status == STATUS_SUCCESS && response.bytesRead == 4 && response.xBits == 32 && response.yBits == 0);
    assert((xMemory.reads == std::vector<ULONG_PTR>{xRoot, 0x155D3884, 0x27B21040, 0x2D6707B4}));
    for (auto value : {LONG{0}, LONG{-123}})
    {
        xMemory.Put(0x2D6707B4, value);
        DispatchCoordinateRequest(xMemory, request, response);
        assert(response.status == STATUS_SUCCESS && response.xBits == static_cast<UInt32>(value));
    }
    xMemory.Put(0x2D6707B4, 1.25f);
    request.valueType = ValueType::Float32;
    DispatchCoordinateRequest(xMemory, request, response);
    assert(response.status == STATUS_SUCCESS && response.xBits == 0x3FA00000);
    xMemory.bytes.erase(0x2D6707B6);
    DispatchCoordinateRequest(xMemory, request, response);
    assert(response.status == STATUS_PARTIAL_COPY && response.bytesRead == 2 && response.xBits == 0);
    const ULONG_PTR xSlots[] = {xRoot, 0x155D3884, 0x27B21040};
    for (SIZE_T i = 0; i < 3; ++i)
    {
        for (auto invalidPointer : {ULONG{0}, ULONG{0xFFFFFFFE}})
        {
            auto broken = xMemory;
            broken.reads.clear();
            broken.Put(xSlots[i], invalidPointer);
            DispatchCoordinateRequest(broken, request, response);
            assert(response.status == (invalidPointer == 0 ? STATUS_INVALID_ADDRESS : STATUS_INTEGER_OVERFLOW));
            assert(response.bytesRead == 0 && response.xBits == 0 && response.yBits == 0);
            assert(broken.reads.size() == i + 1);
        }
        auto broken = xMemory;
        broken.reads.clear();
        broken.bytes.erase(xSlots[i] + 2);
        DispatchCoordinateRequest(broken, request, response);
        assert(response.status == STATUS_PARTIAL_COPY && response.bytesRead == 0 && response.xBits == 0);
        assert(broken.reads.size() == i + 1);
    }
    for (auto invalidAxis : {Axis::Y, Axis::Both})
    {
        request.axis = invalidAxis;
        assert(!ValidRequest(request));
    }
    request.axis = Axis::X;
    for (auto invalidWidth : {0u, 8u})
    {
        request.pointerBytes = invalidWidth;
        assert(!ValidRequest(request));
    }
    request.pointerBytes = 4;
    request.reserved = 5;
    assert(!ValidRequest(request));
    FakeMemory yMemory;
    const ULONG_PTR yRoot = 0x00400000 + DefaultYOffset;
    request = {Version, sizeof(Request), 4, yRoot, Axis::Y, 4, ValueType::Int32, 4};
    yMemory.Put(yRoot, ULONG{0x2DCDB490});
    yMemory.Put(0x2DCDB49C, ULONG{0x2762DE54});
    yMemory.Put(0x2762DE98, ULONG{0x194F2780});
    yMemory.Put(0x194F2990, LONG{141});
    DispatchCoordinateRequest(yMemory, request, response);
    assert(response.status == 0 && response.bytesRead == 4 && response.yBits == 141 && response.xBits == 0);
    assert((yMemory.reads == std::vector<ULONG_PTR>{yRoot, 0x2DCDB49C, 0x2762DE98, 0x194F2990}));
    yMemory.Put(0x194F2990, LONG{0});
    DispatchCoordinateRequest(yMemory, request, response);
    assert(response.status == 0 && response.yBits == 0 && response.bytesRead == 4);
    yMemory.Put(0x194F2990, 1.25f);
    request.valueType = ValueType::Float32;
    DispatchCoordinateRequest(yMemory, request, response);
    assert(response.status == 0 && response.yBits == 0x3FA00000);
    yMemory.bytes.erase(0x194F2992);
    DispatchCoordinateRequest(yMemory, request, response);
    assert(response.status == STATUS_PARTIAL_COPY && response.bytesRead == 2 && response.yBits == 0);
    for (auto slot : {yRoot, ULONG_PTR{0x2DCDB49C}, ULONG_PTR{0x2762DE98}})
    {
        auto broken = yMemory;
        broken.Put(slot, ULONG{0});
        DispatchCoordinateRequest(broken, request, response);
        assert(response.status == STATUS_INVALID_ADDRESS && response.bytesRead == 0);
        broken.Put(slot, ULONG{0xFFFFFFFE});
        DispatchCoordinateRequest(broken, request, response);
        assert(response.status == STATUS_INTEGER_OVERFLOW && response.bytesRead == 0);
        broken.bytes.erase(slot + 2);
        DispatchCoordinateRequest(broken, request, response);
        assert(response.status == STATUS_PARTIAL_COPY && response.bytesRead == 0);
    }
    request.axis = Axis::Both;
    assert(!ValidRequest(request));
    FakeMemory other;
    const ULONG_PTR otherRoot = 0x00400000 + OtherCharacterNameOffset;
    other.Put(otherRoot, ULONG{0x2C1E34B0});
    other.Put(0x2C1E34E4, ULONG{0x28BFB280});
    other.Put(0x28BFB280, ULONG{0x186B2708});
    other.Put(0x186B271C, ULONG{0x2F29C9B8});
    other.Put(0x2F29C9C4, ULONG{0x0022003B}); // X=59, Y=34, two 16-bit values.
    request = {Version, sizeof(Request), 4, otherRoot, Axis::Both, 4, ValueType::Int32, 5};
    DispatchCoordinateRequest(other, request, response);
    assert(response.status == STATUS_SUCCESS && response.bytesRead == 4);
    assert(response.xBits == 59 && response.yBits == 34);
    assert((other.reads == std::vector<ULONG_PTR>{otherRoot, 0x2C1E34E4, 0x28BFB280,
                                                  0x186B271C, 0x2F29C9C4}));
    request.axis = Axis::X;
    assert(!ValidRequest(request));
    request.axis = Axis::Both;
    request.valueType = ValueType::Float32;
    assert(!ValidRequest(request));
    request.valueType = ValueType::Int32;
    other.Put(0x186B271C, ULONG{0});
    DispatchCoordinateRequest(other, request, response);
    assert(response.status == STATUS_INVALID_ADDRESS && response.bytesRead == 0);
    std::cout << "Coordinate, direct, X and Y pointer request tests passed.\n";
}
