#include <ntifs.h>
#include <ntddk.h>
#include <ntimage.h>
#include <intrin.h>

typedef int(__fastcall* FChangeWindowTreeProtection)(void* pWnd, unsigned int attributes);
typedef PVOID(__fastcall* FValidateHwnd)(PVOID hwnd);

FChangeWindowTreeProtection g_fnChangeWindowTreeProtection = NULL;
FValidateHwnd g_fnValidateHwnd = NULL;

typedef PVOID HWND;

typedef struct _SYSTEM_MODULE {
    PVOID  Reserved1;
    PVOID  Reserved2;
    PVOID  ImageBaseAddress;
    ULONG  ImageSize;
    ULONG  Flags;
    USHORT Id;
    USHORT Rank;
    USHORT w018;
    USHORT NameOffset;
    unsigned char Name[256];
} SYSTEM_MODULE, * PSYSTEM_MODULE;

typedef struct _SYSTEM_MODULE_INFORMATION {
    ULONG                ulModuleCount;
    SYSTEM_MODULE        Modules[1];
} SYSTEM_MODULE_INFORMATION, * PSYSTEM_MODULE_INFORMATION;

extern "C" {
    NTSYSCALLAPI NTSTATUS NTAPI ZwQuerySystemInformation(ULONG Class, PVOID Info, ULONG Len, PULONG RetLen);
    NTKERNELAPI PVOID NTAPI RtlFindExportedRoutineByName(PVOID ImageBase, PCCH RoutineName);
    NTKERNELAPI PEPROCESS NTAPI IoThreadToProcess(PETHREAD Thread);
    NTKERNELAPI PCHAR NTAPI PsGetProcessImageFileName(PEPROCESS Process);
}

PVOID FindPattern(PVOID base, size_t size, const char* pattern, const char* mask) {
    auto bData = (const unsigned char*)base;
    size_t maskLen = strlen(mask);

    for (size_t i = 0; i < size - maskLen; i++) {
        if ((i % 0x1000) == 0 && !MmIsAddressValid((PVOID)(bData + i))) continue;

        bool match = true;
        for (size_t j = 0; j < maskLen; j++) {
            if (mask[j] == 'x' && bData[i + j] != (unsigned char)pattern[j]) {
                match = false;
                break;
            }
        }
        if (match) return (PVOID)(bData + i);
    }
    return nullptr;
}


PVOID GetModuleBase(const char* moduleName, ULONG* outSize) {
    ULONG size = 0;
    ZwQuerySystemInformation(11, NULL, 0, &size);
    if (!size) return NULL;

    PSYSTEM_MODULE_INFORMATION pMods = (PSYSTEM_MODULE_INFORMATION)ExAllocatePoolWithTag(NonPagedPoolNx, size, 'OECU');
    if (!pMods) return NULL;

    PVOID baseAddr = NULL;
    if (NT_SUCCESS(ZwQuerySystemInformation(11, pMods, size, NULL))) {
        for (ULONG i = 0; i < pMods->ulModuleCount; i++) {
            if (strstr((const char*)pMods->Modules[i].Name, moduleName)) {
                baseAddr = pMods->Modules[i].ImageBaseAddress;
                if (outSize) *outSize = pMods->Modules[i].ImageSize;
                break;
            }
        }
    }
    ExFreePoolWithTag(pMods, 'OECU');
    return baseAddr;
}


bool initialize() {
    ULONG win32kfullSize = 0;
    PVOID win32kfullBase = GetModuleBase("win32kfull.sys", &win32kfullSize);

    if (!win32kfullBase) {
        DbgPrintEx(0, 0, "[!] win32kfull.sys not found.\n");
        return false;
    }

    DbgPrintEx(0, 0, "[+] Scanning win32kfull at %p (Size: 0x%X)\n", win32kfullBase, win32kfullSize);

    /*
    windows10 22h2のSetDisplayAffinityの呼び出しパターン
    8B D7           mov     edx, edi
    48 8B CB        mov     rcx, rbx
    E8 ?? ?? ?? ??  call    ChangeWindowTreeProtection
    8B F0           mov     esi, eax
    85 C0           test    eax, eax
    */
    PVOID callSite = FindPattern(win32kfullBase, win32kfullSize,
        "\x8B\xD7\x48\x8B\xCB\xE8\x00\x00\x00\x00\x8B\xF0\x85\xC0",
        "xxxxxx????xxx");

    if (!callSite) {
        DbgPrintEx(0, 0, "[!] Pattern match failed. \n");
        return false;
    }

    g_fnChangeWindowTreeProtection = (FChangeWindowTreeProtection)((unsigned char*)callSite + 10 + *(long*)((unsigned char*)callSite + 6));

    DbgPrintEx(0, 0, "[+] ChangeWindowTreeProtection resolved at: %p\n", g_fnChangeWindowTreeProtection);

    if (!MmIsAddressValid(g_fnChangeWindowTreeProtection)) return false;

    PVOID win32kbaseBase = GetModuleBase("win32kbase.sys", NULL);
    if (win32kbaseBase) {
        g_fnValidateHwnd = (FValidateHwnd)RtlFindExportedRoutineByName(win32kbaseBase, "ValidateHwnd");
        DbgPrintEx(0, 0, "[+] ValidateHwnd resolved at: %p\n", g_fnValidateHwnd);
    }

    return (g_fnValidateHwnd != NULL && g_fnChangeWindowTreeProtection != NULL);
}


//exeからみて消す方法だと不定期に隠れるようになったから消しました

/*
void ProtectSpecificProcess(const char* targetExeName) {
    if (!g_fnValidateHwnd || !g_fnChangeWindowTreeProtection) return;

    DbgPrintEx(0, 0, "[*] Scanning for EXE: %s\n", targetExeName);
    int matchCount = 0;

    for (uintptr_t i = 0x10000; i < 0x500000; i += 4) {
        PVOID pWnd = g_fnValidateHwnd((PVOID)i);
        if (pWnd && MmIsAddressValid(pWnd)) {

            PVOID pti = *(PVOID*)((unsigned char*)pWnd + 0x10);

            if (pti && MmIsAddressValid(pti)) {
                PETHREAD thread = *(PETHREAD*)((unsigned char*)pti + 0x0);

                if (thread && MmIsAddressValid(thread)) {
                    PEPROCESS process = IoThreadToProcess(thread);

                    if (process) {
                        PCHAR imageName = PsGetProcessImageFileName(process);

                        if (imageName && strstr(imageName, targetExeName)) {
                            g_fnChangeWindowTreeProtection(pWnd, 0x11);
                            matchCount++;
                        }
                    }
                }
            }
        }
    }
    DbgPrintEx(0, 0, "[+] Auto-Stealth Complete. Hidden %d windows of %s\n", matchCount, targetExeName);
}
*/

void ProtectWindowByHandle(HWND targetHwnd) {
    if (!g_fnValidateHwnd || !g_fnChangeWindowTreeProtection) return;

    PVOID pWnd = g_fnValidateHwnd((PVOID)targetHwnd);

    if (pWnd && MmIsAddressValid(pWnd)) {

        g_fnChangeWindowTreeProtection(pWnd, 0x11);
        DbgPrintEx(0, 0, "[+] Protection applied to pWnd: %p\n", pWnd);
    }
    else {
        DbgPrintEx(0, 0, "[!] Invalid window handle or pWnd not found.\n");
    }
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT a1, PUNICODE_STRING a2) {

    DbgPrintEx(0, 0, "[*]  Driver Entry Called.\n");

    if (initialize()) {

        HWND hTarget = (HWND)(ULONG_PTR)0x20F3E;

        PVOID pWnd = g_fnValidateHwnd(hTarget);

        if (pWnd && MmIsAddressValid(pWnd)) {
            g_fnChangeWindowTreeProtection(pWnd, 0x11);
            DbgPrintEx(0, 0, "[+] Successfully applied protection to HWND: %p (pWnd: %p)\n", hTarget, pWnd);
        }
        else {
            DbgPrintEx(0, 0, "[!] Failed to validate HWND or pWnd is invalid.\n");
        }
    }
    else {
        DbgPrintEx(0, 0, "[!] Initialization failed.\n");
    }

    return STATUS_SUCCESS;
}