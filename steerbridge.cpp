// steerbridge.cpp  -  ASI untuk GTA San Andreas 1.0 US (versi yang dipakai SA-MP)
//
// Fungsinya: menggantikan nilai setir yang dibaca game (CPad::GetSteeringLeftRight) dengan nilai dari
// skrip Lua (firstperson_race.lua) selama mode setir mouse aktif. Dengan begitu game TIDAK lagi membaca
// tombol A/D atau stick untuk setir, nilainya linear langsung dari mouse (tanpa lompatan di tengah).
// Saat mode mouse mati / skrip berhenti, fungsi asli game dipanggil seperti biasa.
//
// Alamat (dari plugin-sdk, plugin_sa/game_sa/CPad.cpp, GTA SA 1.0 US):
//   CPad::GetPad(int)            cdecl    0x53FB70
//   CPad::GetSteeringLeftRight() thiscall 0x53FB80  -> short
//
// BUILD (Visual Studio, "x86 Native Tools Command Prompt"):
//   cl /LD /O2 /EHsc /MT steerbridge.cpp /Fe:steerbridge.asi
// Hasilnya steerbridge.asi: taruh di folder GTA SA (sebelah gta_sa.exe, atau folder scripts/ kalau ASI loader-mu pakai itu).
// Harus 32-bit (x86), bukan x64.

#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const DWORD ADDR_STEER  = 0x53FB80;
static const DWORD ADDR_GETPAD = 0x53FB70;

typedef short (__fastcall *SteerFn)(void* pad, void* edx);
typedef void* (__cdecl *GetPadFn)(int);

static BYTE g_orig[5];
static BYTE g_jmp[5];
static volatile LONG  g_installed = 0;
static volatile LONG  g_enabled   = 0;
static volatile float g_value     = 0.0f;
static volatile DWORD g_lastTick  = 0;   // detak terakhir dari Lua (pengaman kalau skrip mati)

static void Log(const char* fmt, ...)
{
    char path[MAX_PATH];
    if (!GetModuleFileNameA(NULL, path, MAX_PATH)) return;
    char* slash = strrchr(path, '\\');
    if (!slash) return;
    strcpy(slash + 1, "steerbridge.log");
    FILE* f = fopen(path, "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static void PatchBytes(const BYTE* src)
{
    memcpy((void*)(uintptr_t)ADDR_STEER, src, 5);
    FlushInstructionCache(GetCurrentProcess(), (void*)(uintptr_t)ADDR_STEER, 5);
}

// panggil fungsi asli: kembalikan 5 byte awal sebentar, panggil, pasang lagi hook-nya
static short CallOriginal(void* pad)
{
    PatchBytes(g_orig);
    short r = ((SteerFn)(uintptr_t)ADDR_STEER)(pad, NULL);
    PatchBytes(g_jmp);
    return r;
}

static short __fastcall HookSteer(void* pad, void* edx)
{
    (void)edx;
    if (g_enabled && (GetTickCount() - g_lastTick) < 250) {
        GetPadFn getPad = (GetPadFn)(uintptr_t)ADDR_GETPAD;
        if (pad == getPad(0)) {                       // hanya pad pemain 1
            float v = g_value;
            if (!(v == v)) v = 0.0f;                  // tolak NaN
            if (v >  1.0f) v =  1.0f;
            if (v < -1.0f) v = -1.0f;
            return (short)floorf(v * 128.0f + 0.5f);
        }
    }
    return CallOriginal(pad);
}

static bool Install()
{
    if (GetModuleHandleA(NULL) != (HMODULE)(uintptr_t)0x400000) {
        Log("gagal: base gta_sa.exe bukan 0x400000 (bukan GTA SA 1.0 US?)");
        return false;
    }
    DWORD old;
    if (!VirtualProtect((void*)(uintptr_t)ADDR_STEER, 16, PAGE_EXECUTE_READWRITE, &old)) {
        Log("gagal: VirtualProtect");
        return false;
    }
    memcpy(g_orig, (void*)(uintptr_t)ADDR_STEER, 5);
    const BYTE* b = (const BYTE*)(uintptr_t)ADDR_STEER;
    Log("byte awal di 0x53FB80: %02X %02X %02X %02X %02X %02X %02X %02X",
        b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);

    g_jmp[0] = 0xE9;
    *(DWORD*)(g_jmp + 1) = (DWORD)(uintptr_t)&HookSteer - (ADDR_STEER + 5);
    PatchBytes(g_jmp);
    InterlockedExchange(&g_installed, 1);
    Log("hook terpasang");
    return true;
}

static DWORD WINAPI InstallThread(LPVOID)
{
    Sleep(2500);   // tunggu game / samp.dll selesai inisialisasi
    Install();
    return 0;
}

// ---------------- fungsi yang dipanggil dari Lua (FFI) ----------------
extern "C" {

__declspec(dllexport) void __cdecl SteerSetEnabled(int on)
{
    g_enabled = on ? 1 : 0;
    g_lastTick = GetTickCount();
}

__declspec(dllexport) void __cdecl SteerSetValue(float v)
{
    g_value = v;
    g_lastTick = GetTickCount();
}

__declspec(dllexport) int __cdecl SteerHookOk(void)
{
    return g_installed ? 1 : 0;
}

}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, InstallThread, NULL, 0, NULL);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_installed) PatchBytes(g_orig);
    }
    return TRUE;
}
