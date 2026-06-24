#ifndef _INCLUDE_MENU_TRANSLATIONS_H_
#define _INCLUDE_MENU_TRANSLATIONS_H_

#include <string>
#include <unordered_map>

// SourceMod-style phrase tables for the built-in menu chrome labels
// (Exit / page nav / footer hints), resolved per client language.
// Only labels are translated.
// Item text and titles are passed through verbatim, the consumer localizes those itself.
class Translations
{
public:
	// Reload config.txt (cl_language -> short key map)
	// and every *.phrases.txt under <baseDir>/addons/cs2menus/translations.
	void Load(const char *baseDir);

	// Language key used when a client's language is unknown or a phrase lacks it.
	void SetDefaultLanguage(const std::string &lang);

	// Map a raw cl_language value ("english") to a phrase-file key ("en").
	// Unmapped values are returned as-is (lowercased), so Translate can fall back.
	std::string MapClientLanguage(const char *clLanguage) const;

	// Translate a phrase key for a language. Falls back to the default language,
	// then to the key itself, so literal / unknown labels pass through unchanged.
	std::string Translate(const std::string &lang, const std::string &phrase) const;

private:
	std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_phrases; // phrase -> lang -> text
	std::unordered_map<std::string, std::string> m_languageMap;                              // cl_language -> short key
	std::string m_defaultLang = "en";
};

extern Translations g_Translations;

#endif // _INCLUDE_MENU_TRANSLATIONS_H_
