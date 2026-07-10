#include "prefs_db.h"
#include "mmu/log.h"

#include "src/common.h"
#include "src/config/config.h"

#include <sql_mm.h>
#include <mysql_mm.h>
#include <sqlite_mm.h>

#include <cctype>
#include <cstdio>

MenuPrefsDB g_MenuPrefsDB;

MenuPrefsDB::~MenuPrefsDB()
{
	Shutdown();
}

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
	m_sql = static_cast<ISQLInterface *>(g_SMAPI->MetaFactory(SQLMM_INTERFACE, nullptr, nullptr));
	if (!m_sql)
	{
		MMU_LOG_WARN("Failed to get ISQLInterface. Is sql_mm loaded?\n");
		return false;
	}

	if (g_MenusConfig.database.type == "mysql")
	{
		m_isSqlite = false;
		m_mysql = m_sql->GetMySQLClient();
		if (!m_mysql)
		{
			MMU_LOG_WARN("Failed to get MySQL client from sql_mm.\n");
			return false;
		}
		MMU_LOG_INFO("Preference database: MySQL.\n");
	}
	else
	{
		m_isSqlite = true;
		m_sqlite = m_sql->GetSQLiteClient();
		if (!m_sqlite)
		{
			MMU_LOG_WARN("Failed to get SQLite client from sql_mm.\n");
			return false;
		}
		MMU_LOG_INFO("Preference database: SQLite.\n");
	}
	return true;
}

void MenuPrefsDB::Connect(std::function<void(bool)> cb)
{
	auto onConnect = [this, cb](bool success)
	{
		m_connected = success;
		if (success)
		{
			MMU_LOG_INFO("Preference database connected.\n");
			if (m_isSqlite)
			{
				Query("PRAGMA journal_mode=WAL", [](ISQLQuery *) {});
			}
			else
			{
				Query("SET NAMES utf8mb4", [](ISQLQuery *) {});
			}
			CreateSchema();
		}
		else
		{
			MMU_LOG_WARN("Preference database connection failed.\n");
		}
		if (cb)
		{
			cb(success);
		}
	};

	if (m_isSqlite)
	{
		if (!m_sqlite)
		{
			if (cb)
			{
				cb(false);
			}
			return;
		}
		SQLiteConnectionInfo info;
		info.database = g_MenusConfig.database.path.c_str();
		m_conn = m_sqlite->CreateSQLiteConnection(info);
	}
	else
	{
		if (!m_mysql || g_MenusConfig.database.host.empty() || g_MenusConfig.database.name.empty())
		{
			MMU_LOG_WARN("Cannot connect: MySQL host/name empty or client missing. Check core.cfg.\n");
			if (cb)
			{
				cb(false);
			}
			return;
		}
		MySQLConnectionInfo info;
		info.host = g_MenusConfig.database.host.c_str();
		info.user = g_MenusConfig.database.user.c_str();
		info.pass = g_MenusConfig.database.pass.c_str();
		info.database = g_MenusConfig.database.name.c_str();
		info.port = g_MenusConfig.database.port;
		m_conn = m_mysql->CreateMySQLConnection(info);
	}

	if (!m_conn)
	{
		MMU_LOG_WARN("Failed to create database connection object.\n");
		if (cb)
		{
			cb(false);
		}
		return;
	}
	m_conn->Connect(onConnect);
}

void MenuPrefsDB::Shutdown()
{
	m_shuttingDown = true;
	if (m_conn)
	{
		m_conn->Destroy();
		m_conn = nullptr;
	}
	m_connected = false;
	m_mysql = nullptr;
	m_sqlite = nullptr;
	m_sql = nullptr;
}

void MenuPrefsDB::Query(const char *query, std::function<void(ISQLQuery *)> cb)
{
	if (m_shuttingDown || !m_conn || !m_connected)
	{
		if (cb)
		{
			cb(nullptr);
		}
		return;
	}
	// sql_mm requires a valid callback, never pass a null std::function.
	m_conn->Query(query, cb ? cb : [](ISQLQuery *) {});
}

std::string MenuPrefsDB::Escape(const char *str)
{
	if (!m_conn)
	{
		return str ? str : "";
	}
	return m_conn->Escape(str);
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
	if (!m_connected)
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
	if (!m_connected)
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
