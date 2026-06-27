#include "translations.h"
#include "src/config/kv_parser.h"
#include "src/utils/str_utils.h"

#include <filesystem>

Translations g_Translations;

using str::ToLower;

// phrase -> lang -> text. Called with section = phrase name, key = language.
static void PhraseHandler(const std::string &section, const std::string &key, const std::string &value, void *userdata)
{
	auto *phrases = static_cast<std::unordered_map<std::string, std::unordered_map<std::string, std::string>> *>(userdata);
	(*phrases)[section][ToLower(key)] = value;
}

// cl_language -> short key. Called with key = cl_language value, value = short key.
static void LanguageHandler(const std::string & /*section*/, const std::string &key, const std::string &value, void *userdata)
{
	auto *map = static_cast<std::unordered_map<std::string, std::string> *>(userdata);
	(*map)[ToLower(key)] = value;
}

void Translations::Load(const char *baseDir)
{
	m_phrases.clear();
	m_languageMap.clear();

	namespace fs = std::filesystem;
	std::string dir = std::string(baseDir ? baseDir : "") + "/addons/cs2menus/translations";

	kv::LoadFile(dir + "/config.txt", LanguageHandler, &m_languageMap);

	std::error_code ec;
	fs::directory_iterator it(dir, ec);
	if (ec)
	{
		return;
	}
	for (const auto &entry : it)
	{
		if (!entry.is_regular_file(ec))
		{
			continue;
		}
		std::string name = entry.path().filename().string();
		static const std::string suffix = ".phrases.txt";
		if (name.size() <= suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
		{
			continue;
		}
		kv::LoadFile(entry.path().string(), PhraseHandler, &m_phrases);
	}
}

void Translations::SetDefaultLanguage(const std::string &lang)
{
	if (!lang.empty())
	{
		m_defaultLang = ToLower(lang);
	}
}

std::string Translations::MapClientLanguage(const char *clLanguage) const
{
	if (!clLanguage || !clLanguage[0])
	{
		return m_defaultLang;
	}
	std::string raw = ToLower(clLanguage);
	auto it = m_languageMap.find(raw);
	return it != m_languageMap.end() ? it->second : raw;
}

std::string Translations::Translate(const std::string &lang, const std::string &phrase) const
{
	auto pit = m_phrases.find(phrase);
	if (pit == m_phrases.end())
	{
		return phrase; // not a known phrase: treat as literal text
	}
	const auto &langs = pit->second;

	std::string want = lang.empty() ? m_defaultLang : ToLower(lang);
	auto lit = langs.find(want);
	if (lit != langs.end())
	{
		return lit->second;
	}
	if (want != m_defaultLang)
	{
		auto dit = langs.find(m_defaultLang);
		if (dit != langs.end())
		{
			return dit->second;
		}
	}
	return phrase; // known phrase but no matching/default language
}
