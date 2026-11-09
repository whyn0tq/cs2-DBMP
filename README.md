## Installation

Requires [Metamod:Source 1483+](https://www.metamodsource.net/downloads.php?branch=dev).

Extract the archive to `game/csgo/`.

## Database configuration

Add database entries to `game/csgo/addons/configs/databases.cfg`:

```text
"Databases"
{
    "mysql_main"
    {
        "driver"   "mysql"
        "host"     "127.0.0.1"
        "database" "game"
        "user"     "user"
        "pass"     "password"
        "port"     "3306"
    }

    "pgsql_main"
    {
        "driver"   "pgsql"
        "host"     "127.0.0.1"
        "database" "game"
        "user"     "user"
        "pass"     "password"
        "port"     "5432"
    }
}
```

## API usage

Add `public/` to the include paths and include `sql.h`. Get the `SqlApi002` interface:

```cpp
int ret = 0;
auto* api = static_cast<sql::IApiV2*>(
    g_SMAPI->MetaFactory(SQL_INTERFACE_V2, &ret, nullptr));
if (!api || ret != META_IFACE_OK)
    return;
```

### MySQL

Pass the section name from `databases.cfg` to `Open()`:

```cpp
auto* db = api->Open("mysql_main");
if (!db) return;

db->Connect([db](bool ok, const std::string& error) {
    if (!ok) return;

    db->Query("SELECT id FROM players WHERE name = ?", {std::string("Alex")},
        [](sql::Result& result) {
            if (!result.Ok()) return;
            while (result.Next()) {
                auto id = result.Int("id");
            }
        });
});
```

### PostgreSQL

Use the PostgreSQL section name in the same way:

```cpp
auto* db = api->Open("pgsql_main");
if (!db) return;

db->Connect([db](bool ok, const std::string& error) {
    if (!ok) return;

    db->Query("SELECT id FROM players WHERE name = ?", {std::string("Alex")},
        [](sql::Result& result) {
            if (!result.Ok()) return;
            while (result.Next()) {
                auto id = result.Int("id");
            }
        });
});
```

Connections and queries are asynchronous. Call `db->Close()` when unloading your plugin.
