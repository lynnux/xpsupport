#include <windows.h>



#ifdef __cplusplus
extern "C"{
#endif
void dllmain()
{
    static bool inited = false;
    if(!inited)
    {
        inited = true;
        
    }
}
#ifdef __cplusplus
}
#endif

#if defined(_MSC_VER)
#ifdef __cplusplus
extern "C"
{
#endif

    BOOL __stdcall _DllMainCRTStartup(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
    {
        dllmain();
        
        return TRUE;
    }
#ifdef __cplusplus
}
#endif
#else
#endif
