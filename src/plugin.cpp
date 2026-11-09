#include "plugin.h"
#include "owner.h"

#include <mysql.h>

Plugin g_Plugin;
PLUGIN_EXPOSE(Plugin, g_Plugin);

Plugin::Plugin() : m_frame(&IServerGameDLL::GameFrame, this, nullptr, &Plugin::OnFrame) {}

bool Plugin::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
    PLUGIN_SAVEVARS();

    GET_V_IFACE_ANY(GetServerFactory, m_server, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL);

    if (mysql_library_init(0, nullptr, nullptr) != 0)
    {
        ismm->Format(error, maxlen, "Failed to initialize MySQL client library");
        return false;
    }

    m_api = std::make_unique<Api>(m_conns, m_out, ismm->GetBaseDir(), FindOwner);

    g_SMAPI->AddListener(this, this);
    m_frame.Add(m_server);
    return true;
}

bool Plugin::Unload(char* error, size_t maxlen)
{
    m_frame.Remove(m_server);
    m_conns.Shutdown();
    m_out.Clear();
    m_api.reset();
    mysql_library_end();
    return true;
}

void* Plugin::OnMetamodQuery(const char* iface, int* ret)
{
    if (m_api && strcmp(iface, SQL_INTERFACE) == 0)
    {
        if (ret)
            *ret = META_IFACE_OK;
        return static_cast<sql::IApi*>(m_api.get());
    }

    if (m_api && strcmp(iface, SQL_INTERFACE_V2) == 0)
    {
        if (ret)
            *ret = META_IFACE_OK;
        return static_cast<sql::IApiV2*>(m_api.get());
    }

    if (ret)
        *ret = META_IFACE_FAILED;
    return nullptr;
}

void Plugin::OnPluginUnload(PluginId id)
{
    m_conns.Drop(id);
}

KHook::Return<void> Plugin::OnFrame(IServerGameDLL* server, bool simulating, bool first, bool last)
{
    m_out.Run();
    m_conns.Reap();
    return {KHook::Action::Ignore};
}
