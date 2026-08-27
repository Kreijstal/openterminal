#include <windows.h>

HRESULT WINAPI D3DCompileFromFile(void *file, void *defines, void *include,
                                  const char *entry, const char *target,
                                  UINT flags1, UINT flags2, void **code,
                                  void **errors)
{
    (void)file; (void)defines; (void)include; (void)entry; (void)target;
    (void)flags1; (void)flags2;
    if (code) *code = NULL;
    if (errors) *errors = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI D3DReflect(const void *data, SIZE_T size, REFIID iid, void **reflector)
{
    (void)data; (void)size; (void)iid;
    if (reflector) *reflector = NULL;
    return E_NOTIMPL;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    (void)instance; (void)reason; (void)reserved;
    return TRUE;
}
