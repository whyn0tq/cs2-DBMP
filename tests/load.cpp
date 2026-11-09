#include <cstdio>
#include <dlfcn.h>

using Factory = void* (*)(const char* name, int* code);

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: load <plugin.so>\n");
        return 2;
    }

    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib)
    {
        std::printf("FAIL: dlopen: %s\n", dlerror());
        return 1;
    }

    Dl_info info{};
    auto factory = reinterpret_cast<Factory>(dlsym(lib, "CreateInterface"));
    if (!factory || !dladdr(reinterpret_cast<void*>(factory), &info))
    {
        std::printf("FAIL: CreateInterface not found\n");
        return 1;
    }
    std::printf("CreateInterface from %s\n", info.dli_fname);

    int code = 0;
    void* api = factory("ISmmPlugin", &code);
    std::printf("%s: ISmmPlugin = %p\n", api ? "OK" : "FAIL", api);
    return api ? 0 : 1;
}
