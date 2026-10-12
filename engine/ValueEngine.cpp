#include "../gui/DriverClient.h"
#include "ValueEngine.h"
#include <algorithm>
#include <cwchar>
#include <set>
#include <vector>
#include <psapi.h>

namespace
{
bool CopyOutput(const std::wstring& source, wchar_t* destination, unsigned capacity)
{
    if (!destination || !capacity || source.size() >= capacity) return false;
    std::copy(source.begin(), source.end(), destination);
    destination[source.size()] = L'\0';
    return true;
}

std::wstring DecodeText(const DarkEdenProtocol::TextResponse& response, unsigned characterBytes,
                        bool strict = false)
{
    if (characterBytes == 2)
    {
        std::wstring result;
        for (unsigned i = 0; i < response.bytesRead; i += 2)
            result.push_back(static_cast<wchar_t>(response.data[i] | (response.data[i + 1] << 8)));
        return result;
    }
    const auto* bytes = reinterpret_cast<const char*>(response.data);
    const int length = static_cast<int>(response.bytesRead);
    if (!length) return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, length, nullptr, 0);
    UINT codepage = CP_UTF8;
    if (!count) { codepage = 949; count = MultiByteToWideChar(codepage, strict ? MB_ERR_INVALID_CHARS : 0, bytes, length, nullptr, 0); }
    if (!count) return {};
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(codepage, codepage == CP_UTF8 || strict ? MB_ERR_INVALID_CHARS : 0,
                        bytes, length, &result[0], count);
    return result;
}

bool FindImageBase(unsigned processId, uintptr_t& imageBase, std::wstring& error)
{
    DarkEdenClient::Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId));
    if (!process)
    { error = L"게임 프로세스의 이미지 주소를 조회할 수 없습니다."; return false; }
    uintptr_t address = 0x10000;
    while (address < 0x100000000ULL)
    {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQueryEx(process.get(), reinterpret_cast<LPCVOID>(address),
                            &region, sizeof(region))) break;
        if (region.State == MEM_COMMIT && region.Type == MEM_IMAGE &&
            region.BaseAddress == region.AllocationBase)
        {
            wchar_t path[1024]{};
            if (GetMappedFileNameW(process.get(), region.BaseAddress, path, 1024))
            {
                const wchar_t* separator = wcsrchr(path, L'\\');
                if (separator && _wcsicmp(separator + 1, L"darkeden.exe") == 0)
                { imageBase = reinterpret_cast<uintptr_t>(region.AllocationBase); return true; }
            }
        }
        const auto next = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
        if (next <= address) break;
        address = next;
    }
    error = L"darkeden.exe의 이미지 주소를 찾지 못했습니다.";
    return false;
}

bool ReadDword(unsigned processId, uintptr_t imageBase, uintptr_t address,
               uint32_t& value, std::wstring& error)
{
    using namespace DarkEdenProtocol;
    if (address <= imageBase || address > 0xFFFFFFFFu)
    { error = L"상대방 포인터 주소가 유효하지 않습니다."; return false; }
    Request request{Version, sizeof(Request), processId, address - imageBase,
                    Axis::X, 0, ValueType::Int32, 0};
    Response response{};
    if (!DarkEdenClient::Read(processId, request, response, error)) return false;
    value = response.xBits;
    return true;
}

bool ReadOwnXFromYPath(unsigned processId, uint32_t& value, std::wstring& error)
{
    using namespace DarkEdenProtocol;
    uintptr_t imageBase = 0;
    if (!FindImageBase(processId, imageBase, error)) return false;
    uint32_t first = 0, second = 0, third = 0;
    if (!ReadDword(processId, imageBase, imageBase + DefaultYOffset, first, error)) return false;
    if (!first) { error = L"Y 좌표 경로가 비어 있습니다."; return false; }
    if (!ReadDword(processId, imageBase, static_cast<uintptr_t>(first) + 0x0C, second, error)) return false;
    if (!second) { error = L"Y 좌표 경로가 비어 있습니다."; return false; }
    if (!ReadDword(processId, imageBase, static_cast<uintptr_t>(second) + 0x44, third, error)) return false;
    if (!third) { error = L"Y 좌표 경로가 비어 있습니다."; return false; }
    // Y is at third+0x210; the adjacent 32-bit X value is at third+0x20C.
    return ReadDword(processId, imageBase, static_cast<uintptr_t>(third) + 0x20C, value, error);
}

std::wstring JsonString(const std::wstring& value)
{
    std::wstring result = L"\"";
    const wchar_t* digits = L"0123456789ABCDEF";
    for (wchar_t ch : value)
    {
        if (ch == L'"' || ch == L'\\') { result += L'\\'; result += ch; }
        else if (ch < 0x20)
        {
            result += L"\\u00";
            result += digits[(ch >> 4) & 15];
            result += digits[ch & 15];
        }
        else result += ch;
    }
    return result + L'"';
}

bool ReadNearbyCharacters(unsigned processId, std::wstring& json, std::wstring& error)
{
    using namespace DarkEdenProtocol;
    uintptr_t imageBase = 0;
    if (!FindImageBase(processId, imageBase, error)) return false;
    uint32_t manager = 0, header = 0, root = 0;
    if (!ReadDword(processId, imageBase, imageBase + OtherCharacterNameOffset, manager, error)) return false;
    if (!manager) { error = L"주변 캐릭터 목록이 아직 준비되지 않았습니다."; return false; }
    if (!ReadDword(processId, imageBase, static_cast<uintptr_t>(manager) + 0x34, header, error)) return false;
    if (!header) { error = L"주변 캐릭터 목록이 아직 준비되지 않았습니다."; return false; }
    if (!ReadDword(processId, imageBase, static_cast<uintptr_t>(header) + 4, root, error)) return false;
    if (!root)
    {
        error = L"주변 캐릭터 목록이 아직 준비되지 않았습니다.";
        return false;
    }

    std::wstring ownName;
    TextRequest ownRequest{Version, sizeof(TextRequest), processId, DefaultTextOffset, 1, 0};
    TextResponse ownResponse{};
    std::wstring ignored;
    if (DarkEdenClient::ReadText(processId, ownRequest, ownResponse, ignored))
        ownName = DecodeText(ownResponse, 1);

    struct Character { std::wstring name, race, life; uint32_t id, x, y; };
    std::vector<Character> characters;
    std::vector<uint32_t> pending{root};
    std::set<uint32_t> seenNodes, seenActors;
    while (!pending.empty())
    {
        const uint32_t node = pending.back();
        pending.pop_back();
        if (!node || node == header || !seenNodes.insert(node).second) continue;
        if (seenNodes.size() > 2048)
        { error = L"주변 캐릭터 목록의 크기가 비정상적입니다."; return false; }
        uint32_t left = 0, right = 0, key = 0, actor = 0;
        if (!ReadDword(processId, imageBase, node, left, error) ||
            !ReadDword(processId, imageBase, static_cast<uintptr_t>(node) + 8, right, error) ||
            !ReadDword(processId, imageBase, static_cast<uintptr_t>(node) + 0x10, key, error) ||
            !ReadDword(processId, imageBase, static_cast<uintptr_t>(node) + 0x14, actor, error))
            return false;
        for (uint32_t child : {right, left})
        {
            if (!child || child == header || seenNodes.count(child)) continue;
            uint32_t parent = 0;
            std::wstring childError;
            if (ReadDword(processId, imageBase, static_cast<uintptr_t>(child) + 4, parent, childError) &&
                parent == node) pending.push_back(child);
        }
        if (!actor || seenActors.count(actor)) continue;
        uint32_t id = 0, packed = 0;
        std::wstring actorError;
        if (!ReadDword(processId, imageBase, static_cast<uintptr_t>(actor) + 8, id, actorError) ||
            !ReadDword(processId, imageBase, static_cast<uintptr_t>(actor) + 0x0C, packed, actorError))
            continue;
        const uint32_t x = packed & 0xFFFFu, y = packed >> 16;
        if (!id || id != key || x >= 5000 || y >= 5000) continue;
        seenActors.insert(actor);
        std::wstring race = L"미확인";
        uint32_t raceFlag = 0, raceKind = 0;
        std::wstring raceError;
        if (ReadDword(processId, imageBase, static_cast<uintptr_t>(actor) + 0x558, raceFlag, raceError) &&
            ReadDword(processId, imageBase, static_cast<uintptr_t>(actor) + 0x610, raceKind, raceError))
        {
            if (raceFlag == 0x42 && raceKind == 0) race = L"Ousters";
            else if (raceFlag == 0 && raceKind == 2) race = L"Vampire";
            else if (raceFlag == 0x63 && raceKind == 0) race = L"Slayer";
        }
        std::wstring life = L"미확인";
        uint32_t lifeFlag = 0;
        std::wstring lifeError;
        if (ReadDword(processId, imageBase, static_cast<uintptr_t>(actor) + 0x548,
                      lifeFlag, lifeError))
        {
            if (lifeFlag == 1) life = L"생존";
            else if (lifeFlag == 0) life = L"사망";
        }
        std::wstring name;
        TextRequest nameRequest{Version, sizeof(TextRequest), processId,
                                static_cast<uintptr_t>(actor) + 0x544 - imageBase, 1, 0};
        TextResponse nameResponse{};
        if (DarkEdenClient::ReadText(processId, nameRequest, nameResponse, ignored) &&
            nameResponse.terminated && nameResponse.bytesRead > 0)
            name = DecodeText(nameResponse, 1, true);
        characters.push_back({name, race, life, id, x, y});
    }
    if (characters.empty())
    { error = L"주변 캐릭터를 찾지 못했습니다. 게임 화면에 접속한 뒤 다시 시도하세요."; return false; }
    const auto missing = (std::count_if)(characters.begin(), characters.end(),
                                         [](const Character& character) { return character.name.empty(); });
    if (missing == 1 && !ownName.empty())
        for (auto& character : characters) if (character.name.empty()) character.name = ownName;
    for (auto& character : characters)
        if (character.name.empty()) character.name = L"ID_" + DarkEdenClient::Hex(character.id);
    json = L"[";
    for (const auto& character : characters)
    {
        if (json.size() > 60000)
        { error = L"주변 캐릭터 목록이 출력 한도를 초과했습니다."; return false; }
        if (json.size() > 1) json += L",";
        json += L"{\"name\":" + JsonString(character.name) + L",\"race\":" +
                JsonString(character.race) + L",\"life\":" + JsonString(character.life) + L",\"id\":" +
                std::to_wstring(character.id) + L",\"x\":" + std::to_wstring(character.x) +
                L",\"y\":" + std::to_wstring(character.y) + L"}";
    }
    json += L"]";
    return true;
}
}

// Stable C ABI: add new value kinds without changing Python's call signature.
extern "C" __declspec(dllexport) int __stdcall DE_ReadValue(
    unsigned processId, unsigned valueType, wchar_t* output, unsigned outputCapacity,
    wchar_t* errorOutput, unsigned errorCapacity)
{
    using namespace DarkEdenProtocol;
    using namespace DarkEdenClient;
    if (output && outputCapacity) output[0] = L'\0';
    if (errorOutput && errorCapacity) errorOutput[0] = L'\0';
    if (!processId || !output || !outputCapacity || !errorOutput || !errorCapacity)
        return 1;
    try
    {
        std::wstring error;
        std::wstring value;
        if (valueType == static_cast<unsigned>(DarkEdenValueType::X) ||
            valueType == static_cast<unsigned>(DarkEdenValueType::Y))
        {
            const bool x = valueType == static_cast<unsigned>(DarkEdenValueType::X);
            if (x)
            {
                uint32_t coordinate = 0;
                if (!ReadOwnXFromYPath(processId, coordinate, error))
                { CopyOutput(error, errorOutput, errorCapacity); return 2; }
                value = std::to_wstring(static_cast<int32_t>(coordinate));
            }
            else
            {
                Request request{Version, sizeof(Request), processId,
                    DefaultYOffset, Axis::Y, 4, ValueType::Int32, 4u};
                Response response{};
                if (!Read(processId, request, response, error))
                { CopyOutput(error, errorOutput, errorCapacity); return 2; }
                value = FormatValue(response.yBits, ValueType::Int32);
            }
        }
        else if (valueType == static_cast<unsigned>(DarkEdenValueType::CharacterName))
        {
            TextRequest request{Version, sizeof(TextRequest), processId,
                DefaultTextOffset, 1, 0};
            TextResponse response{};
            if (!DarkEdenClient::ReadText(processId, request, response, error))
            { CopyOutput(error, errorOutput, errorCapacity); return 2; }
            value = DecodeText(response, 1);
            // A single narrow character can be the first byte of UTF-16LE text.
            if (response.bytesRead == 1 && response.terminated)
            {
                request.characterBytes = 2;
                TextResponse wide{};
                std::wstring ignored;
                if (DarkEdenClient::ReadText(processId, request, wide, ignored) &&
                    wide.terminated && wide.bytesRead >= 4)
                {
                    const auto decoded = DecodeText(wide, 2);
                    if (!decoded.empty()) value = decoded;
                }
            }
        }
        else { CopyOutput(L"지원하지 않는 값 타입입니다.", errorOutput, errorCapacity); return 1; }
        if (!CopyOutput(value, output, outputCapacity))
        { CopyOutput(L"결과 버퍼가 너무 작습니다.", errorOutput, errorCapacity); return 3; }
        return 0;
    }
    catch (const std::exception&)
    { CopyOutput(L"C++ 엔진에서 예외가 발생했습니다.", errorOutput, errorCapacity); return 4; }
    catch (...)
    { CopyOutput(L"알 수 없는 C++ 엔진 오류입니다.", errorOutput, errorCapacity); return 4; }
}

extern "C" __declspec(dllexport) unsigned __stdcall DE_ListProcesses(
    unsigned* processIds, unsigned capacity)
{
    if (!processIds || !capacity) return 0;
    try
    {
        const auto processes = DarkEdenClient::FindProcesses();
        const auto count = (std::min)(processes.size(), static_cast<size_t>(capacity));
        for (size_t i = 0; i < count; ++i) processIds[i] = processes[i];
        return static_cast<unsigned>(count);
    }
    catch (...) { return 0; }
}

extern "C" __declspec(dllexport) int __stdcall DE_ListNearbyCharacters(
    unsigned processId, wchar_t* output, unsigned outputCapacity,
    wchar_t* errorOutput, unsigned errorCapacity)
{
    if (output && outputCapacity) output[0] = L'\0';
    if (errorOutput && errorCapacity) errorOutput[0] = L'\0';
    if (!processId || !output || !outputCapacity || !errorOutput || !errorCapacity) return 1;
    try
    {
        std::wstring json, error;
        if (!ReadNearbyCharacters(processId, json, error))
        { CopyOutput(error, errorOutput, errorCapacity); return 2; }
        if (!CopyOutput(json, output, outputCapacity))
        { CopyOutput(L"결과 버퍼가 너무 작습니다.", errorOutput, errorCapacity); return 3; }
        return 0;
    }
    catch (...)
    { CopyOutput(L"주변 캐릭터 목록을 읽는 중 오류가 발생했습니다.", errorOutput, errorCapacity); return 4; }
}

extern "C" __declspec(dllexport) int __stdcall DE_SelectNearbyCharacter(
    unsigned processId, unsigned characterId, wchar_t* errorOutput, unsigned errorCapacity)
{
    if (errorOutput && errorCapacity) errorOutput[0] = L'\0';
    if (!processId || !characterId || characterId == 0xFFFFFFFFu ||
        !errorOutput || !errorCapacity) return 1;
    try
    {
        std::wstring error;
        if (!DarkEdenClient::SelectNearbyTarget(processId, characterId, error))
        { CopyOutput(error, errorOutput, errorCapacity); return 2; }
        return 0;
    }
    catch (...)
    { CopyOutput(L"캐릭터 선택 중 오류가 발생했습니다.", errorOutput, errorCapacity); return 3; }
}
