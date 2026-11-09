#pragma once

#include <memory>
#include <sql.h>
#include <string>
#include <string_view>

class IBackend
{
public:
    virtual ~IBackend() = default;

    virtual void Attach() {}
    virtual void Detach() {}
    virtual bool Ready(std::string& error) = 0;
    virtual sql::Result Run(const std::string& text, const sql::Args& args) = 0;
    virtual sql::Result Begin() = 0;
    virtual sql::Result Commit() = 0;
    virtual sql::Result Rollback() = 0;
    virtual std::string Escape(std::string_view s) const = 0;
    virtual void Close() = 0;
};

std::unique_ptr<IBackend> MakeBackend(const sql::Config& cfg);
