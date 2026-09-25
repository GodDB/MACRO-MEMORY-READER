#pragma once
#include "DarkEdenCoordinateReader.h"
#include "shared/DriverProtocol.h"

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS ReadDarkEdenInt32(HANDLE darkedenPid, ULONG_PTR address, LONG* value);
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS ReadDarkEdenCoordinates(HANDLE darkedenPid, DarkEdenCoordinateReader::Coordinates* coordinates);
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS ReadAllDarkEdenCoordinateFormats(HANDLE darkedenPid, DarkEdenAllCoordinates* readings);

// GUI entry point, invoked synchronously in the originating user's context.
_IRQL_requires_(PASSIVE_LEVEL)
void ExecuteDarkEdenRequest(const DarkEdenProtocol::Request& request,
                           DarkEdenProtocol::Response& response) noexcept;
