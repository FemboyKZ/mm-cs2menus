#include "config.h"
#include "kv_parser.h"
#include "src/utils/str_utils.h"

#include <cstdlib>
#include <string>

MenusConfig g_MenusConfig;

using str::ToLower;

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
		else if (k == "defaultlanguage")
		{
			cfg->menu.defaultLanguage = ToLower(value);
		}
		else if (k == "exitbutton")
		{
			cfg->menu.exitButton = (value != "0");
		}
		else if (k == "itemsperpage")
		{
			cfg->menu.itemsPerPage = std::atoi(value.c_str());
		}
		else if (k == "chattitlecolor")
		{
			cfg->menu.chatTitleColor = ToLower(value);
		}
		else if (k == "chatpagecolor")
		{
			cfg->menu.chatPageColor = ToLower(value);
		}
		else if (k == "chatitemcolor")
		{
			cfg->menu.chatItemColor = ToLower(value);
		}
		else if (k == "chatdisabledcolor")
		{
			cfg->menu.chatDisabledColor = ToLower(value);
		}
		else if (k == "chatarrowcolor")
		{
			cfg->menu.chatArrowColor = ToLower(value);
		}
		else if (k == "chatheadercolor")
		{
			cfg->menu.chatHeaderColor = ToLower(value);
		}
		else if (k == "chattitleprefix")
		{
			cfg->menu.chatTitlePrefix = value;
		}
		else if (k == "chattitlesuffix")
		{
			cfg->menu.chatTitleSuffix = value;
		}
		else if (k == "chatnumberprefix")
		{
			cfg->menu.chatNumberPrefix = value;
		}
		else if (k == "chatnumbersuffix")
		{
			cfg->menu.chatNumberSuffix = value;
		}
		else if (k == "chatdisabledprefix")
		{
			cfg->menu.chatDisabledPrefix = value;
		}
		else if (k == "chatarrow")
		{
			cfg->menu.chatArrow = value;
		}
		else if (k == "chatpageprefix")
		{
			cfg->menu.chatPagePrefix = value;
		}
		else if (k == "chatpagesuffix")
		{
			cfg->menu.chatPageSuffix = value;
		}
		else if (k == "chatshowpage")
		{
			cfg->menu.chatShowPage = (value != "0");
		}
		else if (k == "chatheader")
		{
			cfg->menu.chatHeader = value;
		}
		else if (k == "htmlvisibleitems")
		{
			cfg->menu.htmlVisibleItems = std::atoi(value.c_str());
		}
		else if (k == "htmlexititem")
		{
			cfg->menu.htmlExitItem = (value != "0");
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
		else if (k == "htmltitlecolor")
		{
			cfg->menu.htmlTitleColor = value;
		}
		else if (k == "htmlitemcolor")
		{
			cfg->menu.htmlItemColor = value;
		}
		else if (k == "htmlfontface")
		{
			cfg->menu.htmlFontFace = value;
		}
		else if (k == "htmlmarker")
		{
			cfg->menu.htmlMarker = value;
		}
		else if (k == "htmlcountercolor")
		{
			cfg->menu.htmlCounterColor = value;
		}
		else if (k == "htmlfootersize")
		{
			cfg->menu.htmlFooterSize = ToLower(value);
		}
		else if (k == "htmlshowcounter")
		{
			cfg->menu.htmlShowCounter = (value != "0");
		}
		else if (k == "htmlshowfooter")
		{
			cfg->menu.htmlShowFooter = (value != "0");
		}
		else if (k == "htmlsubmenusuffix")
		{
			cfg->menu.htmlSubmenuSuffix = value;
		}
		else if (k == "htmlfooterseparator")
		{
			cfg->menu.htmlFooterSeparator = value;
		}
		else if (k == "htmlcounterprefix")
		{
			cfg->menu.htmlCounterPrefix = value;
		}
		else if (k == "htmlcountersuffix")
		{
			cfg->menu.htmlCounterSuffix = value;
		}
		else if (k == "htmlhighlighttext")
		{
			cfg->menu.htmlHighlightText = (value != "0");
		}
		else if (k == "htmldurationsecs")
		{
			cfg->menu.htmlDurationSecs = std::atoi(value.c_str());
		}
		else if (k == "htmlrefreshinterval")
		{
			cfg->menu.htmlRefreshInterval = static_cast<float>(std::atof(value.c_str()));
		}
		else if (k == "htmlkeepalive")
		{
			cfg->menu.htmlKeepAlive = static_cast<float>(std::atof(value.c_str()));
		}
		else if (k == "htmltitlesize")
		{
			cfg->menu.htmlTitleSize = ToLower(value);
		}
		else if (k == "htmlitemsize")
		{
			cfg->menu.htmlItemSize = ToLower(value);
		}
		else if (k == "htmlalign")
		{
			cfg->menu.htmlAlign = ToLower(value);
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
	else if (sec == "database")
	{
		if (k == "enabled")
		{
			cfg->database.enabled = (value != "0");
		}
		else if (k == "type")
		{
			cfg->database.type = ToLower(value);
		}
		else if (k == "prefix")
		{
			cfg->database.prefix = value;
		}
		else if (k == "path")
		{
			cfg->database.path = value;
		}
		else if (k == "host")
		{
			cfg->database.host = value;
		}
		else if (k == "user")
		{
			cfg->database.user = value;
		}
		else if (k == "pass" || k == "password")
		{
			cfg->database.pass = value;
		}
		else if (k == "name" || k == "database")
		{
			cfg->database.name = value;
		}
		else if (k == "port")
		{
			cfg->database.port = std::atoi(value.c_str());
		}
	}
}

bool MENU_LoadConfig(const char *path, MenusConfig &config)
{
	return kv::LoadFile(path ? path : "", ConfigHandler, &config);
}
