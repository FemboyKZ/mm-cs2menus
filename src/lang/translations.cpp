#include "translations.h"
#include "src/config/kv_parser.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

Translations g_Translations;

static std::string ToLower(const std::string &s)
{
	std::string r = s;
	std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return r;
}

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

// Read a "Root { ... }" KeyValues file and parse its body with `handler`.
static void LoadKvFile(const std::string &path, void *userdata, kv::Handler handler)
{
	std::ifstream file(path);
	if (!file.is_open())
	{
		return;
	}
	kv::Token root = kv::NextToken(file);
	if (root.kind != kv::TokenType::String)
	{
		return;
	}
	kv::Token brace = kv::NextToken(file);
	if (brace.kind != kv::TokenType::OpenBrace)
	{
		return;
	}
	kv::ParseSection(file, root.value, handler, userdata);
}

void Translations::Load(const char *baseDir)
{
	m_phrases.clear();
	m_languageMap.clear();

	namespace fs = std::filesystem;
	std::string dir = std::string(baseDir ? baseDir : "") + "/addons/cs2menus/translations";

	LoadKvFile(dir + "/config.txt", &m_languageMap, LanguageHandler);

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
		LoadKvFile(entry.path().string(), &m_phrases, PhraseHandler);
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
