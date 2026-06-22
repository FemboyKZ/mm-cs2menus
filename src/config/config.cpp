#include "config.h"
#include "kv_parser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>

MenusConfig g_MenusConfig;

static std::string ToLower(const std::string &s)
{
	std::string r = s;
	std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return r;
}

static void ConfigHandler(const std::string &section, const std::string &key, const std::string &value, void *userdata)
{
	MenusConfig *cfg = static_cast<MenusConfig *>(userdata);
	std::string sec = ToLower(section);
	std::string k = ToLower(key);

	if (sec == "general")
	{
		if (k == "commandprefix")
		{
			cfg->general.commandPrefix = value;
		}
		else if (k == "silentcommandprefix")
		{
			cfg->general.silentCommandPrefix = value;
		}
	}
	else if (sec == "menu")
	{
		if (k == "defaulttype")
		{
			cfg->menu.defaultType = ToLower(value);
		}
		else if (k == "itemsperpage")
		{
			cfg->menu.itemsPerPage = std::atoi(value.c_str());
		}
		else if (k == "exitbutton")
		{
			cfg->menu.exitButton = (value != "0");
		}
		else if (k == "htmlvisibleitems")
		{
			cfg->menu.htmlVisibleItems = std::atoi(value.c_str());
		}
		else if (k == "htmlnavcolor")
		{
			cfg->menu.htmlNavColor = value;
		}
		else if (k == "htmlfootercolor")
		{
			cfg->menu.htmlFooterColor = value;
		}
		else if (k == "htmldisabledcolor")
		{
			cfg->menu.htmlDisabledColor = value;
		}
		else if (k == "htmlfixflashing")
		{
			cfg->menu.htmlFixFlashing = (value != "0");
		}
		else if (k == "navup")
		{
			cfg->menu.navUp = ToLower(value);
		}
		else if (k == "navdown")
		{
			cfg->menu.navDown = ToLower(value);
		}
		else if (k == "navselect")
		{
			cfg->menu.navSelect = ToLower(value);
		}
		else if (k == "navback")
		{
			cfg->menu.navBack = ToLower(value);
		}
	}
}

bool MENU_LoadConfig(const char *path, MenusConfig &config)
{
	std::ifstream file(path);
	if (!file.is_open())
	{
		return false;
	}

	// "cs2menus" { ... }
	kv::Token root = kv::NextToken(file);
	if (root.kind != kv::TokenType::String)
	{
		return false;
	}

	kv::Token brace = kv::NextToken(file);
	if (brace.kind != kv::TokenType::OpenBrace)
	{
		return false;
	}

	kv::ParseSection(file, root.value, ConfigHandler, &config);
	return true;
}
