#pragma once

#include <ntifs.h>
#if !defined(_KERNEL_MODE)
#include <type_traits>
#endif

namespace DarkEdenTypes
{
    template <typename A, typename B> struct Same { static constexpr bool value = false; };
    template <typename A> struct Same<A, A> { static constexpr bool value = true; };
    template <typename T> constexpr bool PlainData =
#if defined(_KERNEL_MODE)
        __is_trivial(T) && __is_trivially_copyable(T);
#else
        std::is_trivial<T>::value && std::is_trivially_copyable<T>::value;
#endif
}

template <typename T>
struct MemoryReadResult final
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    SIZE_T bytesRead = 0;
    T value{};

    bool Succeeded() const noexcept
    {
        return NT_SUCCESS(status) && bytesRead == sizeof(T);
    }
};

// Kernel driver only. All calls, including destruction, require PASSIVE_LEVEL.
// Serialize Initialize/destruction against reads; do not use after destruction.
class KernelProcessMemoryReader final
{
public:
    KernelProcessMemoryReader() noexcept = default;
    ~KernelProcessMemoryReader() noexcept;

    KernelProcessMemoryReader(const KernelProcessMemoryReader&) = delete;
    KernelProcessMemoryReader& operator=(const KernelProcessMemoryReader&) = delete;
    KernelProcessMemoryReader(KernelProcessMemoryReader&&) = delete;
    KernelProcessMemoryReader& operator=(KernelProcessMemoryReader&&) = delete;

    // Bind once to a referenced process object, not to a reusable numeric PID.
    // A second initialization fails; construct a new reader to change targets.
    _IRQL_requires_(PASSIVE_LEVEL)
    NTSTATUS Initialize(HANDLE processId) noexcept;

    // Open a private kernel handle with forced caller access checks.
    // Must execute synchronously in the requesting thread's security context.
    _IRQL_requires_(PASSIVE_LEVEL)
    NTSTATUS InitializeForRequest(HANDLE processId) noexcept;

    // Resolve an offset from the referenced process image in kernel mode.
    _IRQL_requires_(PASSIVE_LEVEL)
    NTSTATUS ResolveImageOffset(ULONG_PTR offset, ULONG_PTR* address) const noexcept;

    template <typename T>
    _IRQL_requires_(PASSIVE_LEVEL)
    MemoryReadResult<T> Read(ULONG_PTR address) const noexcept
    {
        static_assert(DarkEdenTypes::PlainData<T>,
                      "Read requires a plain data type.");
        static_assert(sizeof(T) <= MaxReadSize, "Read is limited to 256 bytes.");

        MemoryReadResult<T> result{};
        result.status = CopyValue(address, &result.value, sizeof(T),
                                  &result.bytesRead);
        return result;
    }

private:
    static constexpr SIZE_T MaxReadSize = 256;
    PEPROCESS process_ = nullptr;

    _IRQL_requires_(PASSIVE_LEVEL)
    NTSTATUS CopyValue(ULONG_PTR address, void* output, SIZE_T size,
                       SIZE_T* bytesRead) const noexcept;
};
