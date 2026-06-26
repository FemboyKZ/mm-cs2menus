#ifndef _INCLUDE_MENU_PREFS_DB_H_
#define _INCLUDE_MENU_PREFS_DB_H_

#include <cstdint>
#include <functional>
#include <string>

// Forward declarations from sql_mm.
class ISQLInterface;
class ISQLConnection;
class ISQLQuery;
class IMySQLClient;
class ISQLiteClient;

// One row of a player's stored menu preferences.
// Empty strings mean "no preference" (fall back to the server config).
// Nav keys are stored as key names, e.g. "w", "shift".
struct MenuPrefsRow
{
	std::string type; // "chat", "html", or "" for none
	std::string keyUp;
	std::string keyDown;
	std::string keySelect;
	std::string keyBack;
};

// sql_mm-backed store for per-player menu preferences. One table, keyed by SteamID64.
// Connects async. Callbacks fire on the main thread (sql_mm drains its queue in its own ServerGamePostSimulate hook),
// so they may touch the menu manager directly.
class MenuPrefsDB
{
public:
	~MenuPrefsDB();

	// Acquire ISQLInterface from MetaFactory and pick the configured client.
	// Call in AllPluginsLoaded or later (sql_mm must load first). Returns false if unavailable.
	bool Init();

	// Connect using the loaded config, then create the schema. cb(success) fires on the main thread.
	void Connect(std::function<void(bool)> cb);

	void Shutdown();

	bool IsConnected() const
	{
		return m_connected;
	}

	bool IsShuttingDown() const
	{
		return m_shuttingDown;
	}

	// Load one player's prefs. cb(found, row) fires on the main thread.
	void LoadPrefs(uint64_t steamId64, std::function<void(bool found, const MenuPrefsRow &row)> cb);

	// Insert-or-update one player's prefs. Empty strings clear that field.
	void SavePrefs(uint64_t steamId64, const MenuPrefsRow &row);

private:
	void CreateSchema();
	void Query(const char *query, std::function<void(ISQLQuery *)> cb);
	std::string Escape(const char *str);
	// "<prefix>_prefs"
	std::string Table() const;

	ISQLInterface *m_sql = nullptr;
	IMySQLClient *m_mysql = nullptr;
	ISQLiteClient *m_sqlite = nullptr;
	ISQLConnection *m_conn = nullptr;
	bool m_isSqlite = true;
	bool m_connected = false;
	bool m_shuttingDown = false;
};

extern MenuPrefsDB g_MenuPrefsDB;

#endif // _INCLUDE_MENU_PREFS_DB_H_
