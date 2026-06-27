#ifndef _INCLUDE_MENU_KV_PARSER_H_
#define _INCLUDE_MENU_KV_PARSER_H_

// Minimal Valve KeyValues1 tokenizer and section parser.

#include <fstream>
#include <istream>
#include <string>

namespace kv
{

	enum class TokenType
	{
		String,
		OpenBrace,
		CloseBrace,
		EndOfFile
	};

	struct Token
	{
		TokenType kind;
		std::string value;
	};

	inline Token NextToken(std::istream &in)
	{
		Token tok;
		while (in.good())
		{
			int ch = in.get();
			if (ch == EOF)
			{
				tok.kind = TokenType::EndOfFile;
				return tok;
			}

			if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
			{
				continue;
			}

			if (ch == '/')
			{
				int next = in.peek();
				if (next == '/')
				{
					while (in.good() && in.get() != '\n')
						;
					continue;
				}
			}

			if (ch == '{')
			{
				tok.kind = TokenType::OpenBrace;
				return tok;
			}
			if (ch == '}')
			{
				tok.kind = TokenType::CloseBrace;
				return tok;
			}

			if (ch == '"')
			{
				tok.kind = TokenType::String;
				tok.value.clear();
				while (in.good())
				{
					ch = in.get();
					if (ch == '"' || ch == EOF)
					{
						break;
					}
					if (ch == '\\')
					{
						int esc = in.get();
						if (esc == '"')
						{
							tok.value += '"';
						}
						else if (esc == '\\')
						{
							tok.value += '\\';
						}
						else if (esc == 'n')
						{
							tok.value += '\n';
						}
						else if (esc == 't')
						{
							tok.value += '\t';
						}
						else
						{
							tok.value += '\\';
							tok.value += static_cast<char>(esc);
						}
					}
					else
					{
						tok.value += static_cast<char>(ch);
					}
				}
				return tok;
			}

			tok.kind = TokenType::String;
			tok.value.clear();
			tok.value += static_cast<char>(ch);
			while (in.good())
			{
				int next = in.peek();
				if (next == ' ' || next == '\t' || next == '\r' || next == '\n' || next == '{' || next == '}' || next == '"' || next == EOF)
				{
					break;
				}
				tok.value += static_cast<char>(in.get());
			}
			return tok;
		}

		tok.kind = TokenType::EndOfFile;
		return tok;
	}

	typedef void (*Handler)(const std::string &section, const std::string &key, const std::string &value, void *userdata);

	inline bool ParseSection(std::istream &in, const std::string &sectionName, Handler handler, void *userdata)
	{
		while (true)
		{
			Token tok = NextToken(in);
			if (tok.kind == TokenType::CloseBrace || tok.kind == TokenType::EndOfFile)
			{
				return true;
			}

			if (tok.kind != TokenType::String)
			{
				continue;
			}

			std::string key = tok.value;
			Token next = NextToken(in);

			if (next.kind == TokenType::OpenBrace)
			{
				ParseSection(in, key, handler, userdata);
			}
			else if (next.kind == TokenType::String)
			{
				handler(sectionName, key, next.value, userdata);
			}
		}
	}

	// Open `path` and parse its top-level "Root { ... }" body with `handler`.
	// Returns false if the file is missing or isn't a braced root section.
	inline bool LoadFile(const std::string &path, Handler handler, void *userdata)
	{
		std::ifstream file(path);
		if (!file.is_open())
		{
			return false;
		}
		Token root = NextToken(file);
		if (root.kind != TokenType::String)
		{
			return false;
		}
		Token brace = NextToken(file);
		if (brace.kind != TokenType::OpenBrace)
		{
			return false;
		}
		ParseSection(file, root.value, handler, userdata);
		return true;
	}

} // namespace kv

#endif // _INCLUDE_MENU_KV_PARSER_H_
