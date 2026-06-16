#pragma once

#include <windows.h>
#include <winternl.h>

namespace pulsedb {

    // function pointer type for NtQuerySystemInformation which is not in any public SDK header so i declare it myself
    typedef NTSTATUS(WINAPI* NtQuerySystemInformationFn)(
        ULONG  SystemInformationClass,
        PVOID  SystemInformation,
        ULONG  SystemInformationLength,
        PULONG ReturnLength
    );

    // "singleton" that loads NtQuerySystemInformation from ntdll.dll at startup and is used by CPU, process, and system metrics collectors
    class NtdllHelper {
    public:
        // singleton constructed on first call, can keep reading safely after
        static NtdllHelper& get() {
            static NtdllHelper instance;
            return instance;
        }

        // returns the function pointer to be null if ntdll failed to load which basically will never happen but
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
            // ntdll.dll is always loaded in every Windows process so GetModuleHandle is fine here (no need for LoadLibrary which i originally thought)
            HMODULE handle = GetModuleHandleW(L"ntdll.dll");
            if (handle) {
                // GetProcAddress returns void* so it is casted correctly 
                m_query_fn = reinterpret_cast<NtQuerySystemInformationFn>(GetProcAddress(handle, "NtQuerySystemInformation"));
            }
        }
    };
}