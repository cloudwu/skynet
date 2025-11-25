#include "dlfcn.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void *dlopen(const char *path, int flag) {
    return LoadLibraryA(path);
}

const char *dlerror() {
    DWORD err = GetLastError();
    HLOCAL LocalAddress = NULL;
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
                  NULL, err, 0, (PTSTR)&LocalAddress, 0, NULL);
    return (LPSTR)LocalAddress;
}

void *dlsym(void *dl, const char *sym) {
    return GetProcAddress(dl, sym);
}

