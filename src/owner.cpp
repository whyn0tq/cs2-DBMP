#include "owner.h"
#include "plugin.h"

#include <dlfcn.h>

namespace
{
constexpr PluginId kMaxId = 4096;

void* Module(const void* address)
{
    Dl_info info{};
    if (!address || !dladdr(address, &info) || !info.dli_fname)
        return nullptr;

    void* lib = dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD);
    if (lib)
        dlclose(lib);
    return lib;
}
}

int FindOwner(const void* caller)
{
    void* lib = Module(caller);
    if (!lib)
        return 0;

    auto manager = static_cast<SourceMM::ISmmPluginManager*>(g_SMAPI->MetaFactory(MMIFACE_PLMANAGER, nullptr, nullptr));
    if (!manager)
        return 0;

    for (PluginId id = 1; id < kMaxId; id++)
    {
        void* handle = nullptr;
        if (manager->QueryHandle(id, &handle) && handle == lib)
            return id;
    }
    return 0;
}
