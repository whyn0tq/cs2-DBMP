#pragma once

#include "api.h"
#include "dispatcher.h"
#include "info.h"
#include "registry.h"

#include <ISmmPlugin.h>
#include <engine2/isource2server.h>
#include <memory>

class Plugin final : public ISmmPlugin, public IMetamodListener
{
public:
    Plugin();

    bool Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late) override;
    bool Unload(char* error, size_t maxlen) override;
    void* OnMetamodQuery(const char* iface, int* ret) override;
    void OnPluginUnload(PluginId id) override;

    const char* GetAuthor() override { return PLUGIN_AUTHOR; }
    const char* GetName() override { return PLUGIN_DISPLAY_NAME; }
    const char* GetDescription() override { return PLUGIN_DESCRIPTION; }
    const char* GetURL() override { return PLUGIN_URL; }
    const char* GetLicense() override { return PLUGIN_LICENSE; }
    const char* GetVersion() override { return PLUGIN_VERSION; }
    const char* GetDate() override { return __DATE__; }
    const char* GetLogTag() override { return PLUGIN_LOG_TAG; }

private:
    KHook::Return<void> OnFrame(IServerGameDLL* server, bool simulating, bool first, bool last);

    KHook::Virtual<IServerGameDLL, void, bool, bool, bool> m_frame;
    IServerGameDLL* m_server = nullptr;
    Dispatcher m_out;
    Registry m_conns;
    std::unique_ptr<Api> m_api;
};

extern Plugin g_Plugin;

PLUGIN_GLOBALVARS();
