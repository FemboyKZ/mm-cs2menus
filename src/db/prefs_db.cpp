#include "prefs_db.h"
#include "mmu/log.h"

#include "src/common.h"
#include "src/config/config.h"

#include <sql_mm.h>

#include <cctype>
#include <cstdio>

MenuPrefsDB g_MenuPrefsDB;

std::string MenuPrefsDB::Table() const
{
	// Prefix comes from server config and is interpolated into SQL unescaped,
	// so restrict it to a safe identifier charset.
	std::string prefix;
	for (char c : g_MenusConfig.database.prefix)
	{
		if (isalnum(static_cast<unsigned char>(c)) || c == '_')
		{
			prefix += c;
		}
	}
	if (prefix.empty())
	{
		prefix = "cs2menus";
	}
	return prefix + "_prefs";
}

bool MenuPrefsDB::Init()
{
	m_isSqlite = (g_MenusConfig.database.type != "mysql");
	if (!m_conn.Init(m_isSqlite ? mmu::sql::DbType::SQLite : mmu::sql::DbType::MySQL))
	{
		return false;
	}
	m_conn.SetSchemaHook([this] { CreateSchema(); });
	return true;
}

void MenuPrefsDB::Connect(std::function<void(bool)> cb)
{
	mmu::sql::ConnectParams p;
	p.path = g_MenusConfig.database.path;
	p.host = g_MenusConfig.database.host;
	p.user = g_MenusConfig.database.user;
	p.pass = g_MenusConfig.database.pass;
	p.database = g_MenusConfig.database.name;
	p.port = g_MenusConfig.database.port;
	m_conn.Connect(p, std::move(cb));
}

void MenuPrefsDB::Shutdown()
{
	m_conn.Shutdown();
}

void MenuPrefsDB::CreateSchema()
{
	std::string table = Table();
	char query[1024];

	if (m_isSqlite)
	{
		snprintf(query, sizeof(query),
				 "CREATE TABLE IF NOT EXISTS %s ("
				 "steamid64 INTEGER PRIMARY KEY, "
				 "menu_type TEXT NOT NULL DEFAULT '', "
				 "key_up TEXT NOT NULL DEFAULT '', "
				 "key_down TEXT NOT NULL DEFAULT '', "
				 "key_select TEXT NOT NULL DEFAULT '', "
				 "key_back TEXT NOT NULL DEFAULT ''"
				 ")",
				 table.c_str());
	}
	else
	{
		snprintf(query, sizeof(query),
				 "CREATE TABLE IF NOT EXISTS `%s` ("
				 "`steamid64` bigint(20) unsigned NOT NULL, "
				 "`menu_type` varchar(8) NOT NULL DEFAULT '', "
				 "`key_up` varchar(16) NOT NULL DEFAULT '', "
				 "`key_down` varchar(16) NOT NULL DEFAULT '', "
				 "`key_select` varchar(16) NOT NULL DEFAULT '', "
				 "`key_back` varchar(16) NOT NULL DEFAULT '', "
				 "PRIMARY KEY (`steamid64`)"
				 ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
				 table.c_str());
	}
	Query(query, [](ISQLQuery *) {});
	MMU_LOG_INFO("Preference schema created/verified.\n");
}

void MenuPrefsDB::LoadPrefs(uint64_t steamId64, std::function<void(bool, const MenuPrefsRow &)> cb)
{
	if (!m_conn.IsConnected())
	{
		if (cb)
		{
			cb(false, MenuPrefsRow {});
		}
		return;
	}

	char query[512];
	snprintf(query, sizeof(query), "SELECT menu_type, key_up, key_down, key_select, key_back FROM %s WHERE steamid64 = %llu", Table().c_str(),
			 static_cast<unsigned long long>(steamId64));

	Query(query,
		  [cb](ISQLQuery *result)
		  {
			  if (!result || !cb)
			  {
				  if (cb)
				  {
					  cb(false, MenuPrefsRow {});
				  }
				  return;
			  }
			  ISQLResult *rs = result->GetResultSet();
			  if (!rs || !rs->MoreRows() || !rs->FetchRow())
			  {
				  cb(false, MenuPrefsRow {});
				  return;
			  }
			  auto col = [rs](unsigned int i) -> std::string
			  {
				  const char *s = rs->GetString(i);
				  return s ? s : "";
			  };
			  MenuPrefsRow row;
			  row.type = col(0);
			  row.keyUp = col(1);
			  row.keyDown = col(2);
			  row.keySelect = col(3);
			  row.keyBack = col(4);
			  cb(true, row);
		  });
}

void MenuPrefsDB::SavePrefs(uint64_t steamId64, const MenuPrefsRow &row)
{
	if (!m_conn.IsConnected())
	{
		return;
	}

	std::string type = Escape(row.type.c_str());
	std::string up = Escape(row.keyUp.c_str());
	std::string down = Escape(row.keyDown.c_str());
	std::string sel = Escape(row.keySelect.c_str());
	std::string back = Escape(row.keyBack.c_str());

	char query[1024];
	if (m_isSqlite)
	{
		snprintf(query, sizeof(query),
				 "INSERT INTO %s (steamid64, menu_type, key_up, key_down, key_select, key_back) "
				 "VALUES (%llu, '%s', '%s', '%s', '%s', '%s') "
				 "ON CONFLICT(steamid64) DO UPDATE SET "
				 "menu_type='%s', key_up='%s', key_down='%s', key_select='%s', key_back='%s'",
				 Table().c_str(), static_cast<unsigned long long>(steamId64), type.c_str(), up.c_str(), down.c_str(), sel.c_str(), back.c_str(),
				 type.c_str(), up.c_str(), down.c_str(), sel.c_str(), back.c_str());
	}
	else
	{
		snprintf(query, sizeof(query),
				 "INSERT INTO `%s` (steamid64, menu_type, key_up, key_down, key_select, key_back) "
				 "VALUES (%llu, '%s', '%s', '%s', '%s', '%s') "
				 "ON DUPLICATE KEY UPDATE "
				 "menu_type='%s', key_up='%s', key_down='%s', key_select='%s', key_back='%s'",
				 Table().c_str(), static_cast<unsigned long long>(steamId64), type.c_str(), up.c_str(), down.c_str(), sel.c_str(), back.c_str(),
				 type.c_str(), up.c_str(), down.c_str(), sel.c_str(), back.c_str());
	}
	Query(query, [](ISQLQuery *) {});
}
