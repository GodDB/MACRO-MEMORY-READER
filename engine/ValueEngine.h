#pragma once

// Public value IDs shared by callers of DE_ReadValue.
enum class DarkEdenValueType : unsigned
{
    X = 1,
    Y = 2,
    CharacterName = 3,
};

extern "C" __declspec(dllexport) int __stdcall DE_ReadValue(
    unsigned processId, unsigned valueType, wchar_t* output, unsigned outputCapacity,
    wchar_t* errorOutput, unsigned errorCapacity);
extern "C" __declspec(dllexport) unsigned __stdcall DE_ListProcesses(
    unsigned* processIds, unsigned capacity);
extern "C" __declspec(dllexport) int __stdcall DE_ListNearbyCharacters(
    unsigned processId, wchar_t* output, unsigned outputCapacity,
    wchar_t* errorOutput, unsigned errorCapacity);
extern "C" __declspec(dllexport) int __stdcall DE_SelectNearbyCharacter(
    unsigned processId, unsigned characterId, wchar_t* errorOutput, unsigned errorCapacity);
