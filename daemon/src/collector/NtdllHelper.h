#pragma once

#include <windows.h>
#include <winternl.h>

namespace pulsedb {

    typedef NTSTATUS(WINAPI* NtQuerySystemInformationFn)(
        ULONG  SystemInformationClass,
        PVOID  SystemInformation,
        ULONG  SystemInformationLength,
        PULONG ReturnLength
    );

    class NtdllHelper {
    public:
        static NtdllHelper& get() {
            static NtdllHelper instance;
            return instance;
        }

        NtQuerySystemInformationFn query_fn() const {
            return m_query_fn;
        }

        bool is_loaded() const {
            return m_query_fn != nullptr;
        }

        NtdllHelper(const NtdllHelper&) = delete;
        NtdllHelper& operator=(const NtdllHelper&) = delete;
        NtdllHelper(NtdllHelper&&) = delete;
        NtdllHelper& operator=(NtdllHelper&&) = delete;

    private:
        NtQuerySystemInformationFn m_query_fn{ nullptr };

        NtdllHelper() {
            HMODULE handle = GetModuleHandleW(L"ntdll.dll");
            if (handle) {
                m_query_fn = reinterpret_cast<NtQuerySystemInformationFn>(GetProcAddress(handle, "NtQuerySystemInformation"));
            }
        }
    };
}