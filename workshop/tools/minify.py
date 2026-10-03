"""Writes a minimal copy of workshop/panorama for a release: no comments, no indentation, no blank lines.

    python minify.py <out>    # <out> gets layout/, styles/ and images/ as workshop/panorama has them

The files in workshop/panorama stay as they are, what gets compiled and shipped is the copy.

Layouts and styles keep one tag or rule to a line, a few hundred characters at most like the sources' own lines:
nothing here was seen to read longer ones. Images go on one line, the game's own icons already have lines of
50000 characters. A style comment opening with "/*!" is kept, for a license notice.
"""

import os
import re
import sys

SOURCE = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "panorama")
)

STRING = r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
TAG = re.compile(r"<(?:\"[^\"]*\"|'[^']*'|[^>\"'])*>")


def squeeze(text):
    return re.sub(r"\s+", " ", text).strip()


def minify_css(text):
    # Strings and kept comments sit out the rest as placeholders, nothing in them is formatting.
    held = []

    def hold(match):
        token = match.group(0)
        if token.startswith("/*") and not token.startswith("/*!"):
            return " "
        held.append(token)
        return ("\x01%d\x01" if token.startswith("/*") else "\x00%d\x00") % (
            len(held) - 1
        )

    text = re.sub(STRING + r"|/\*.*?\*/", hold, text, flags=re.S)
    out = []
    depth = 0
    parts = re.split(r"([{};])", text)
    for chunk, delimiter in zip(parts[0::2], parts[1::2] + [""]):
        chunk = squeeze(chunk)
        if delimiter == "{":
            out.append(re.sub(r"\s*,\s*", ",", chunk) + "{")
            depth += 1
            continue
        if chunk:
            # Every declaration keeps its semicolon, the last of a rule too: the game's parser isn't a browser's.
            out.append(re.sub(r"\s*:\s*", ":", chunk, count=1) + ";")
        if delimiter == "}":
            out.append("}")
            depth -= 1
        if depth == 0 and delimiter:
            out.append("\n")
    if depth != 0:
        raise ValueError("unbalanced braces")
    # A kept comment gets a line of its own.
    text = re.sub(
        r"\x01(\d+)\x01 ?", lambda m: held[int(m.group(1))] + "\n", "".join(out)
    )
    return re.sub(r"\x00(\d+)\x00", lambda m: held[int(m.group(1))], text)


def minify_markup(text, one_line):
    """Layouts and SVG images. `one_line` also squeezes the whitespace inside attribute values, an image's path data."""
    text = re.sub(r"<!--.*?-->", "", text, flags=re.S)
    if TAG.sub("", text).strip():
        raise ValueError("text outside tags")
    tags = []
    for tag in TAG.findall(text):
        pieces = re.split("(" + STRING + ")", tag)
        for i, piece in enumerate(pieces):
            if i % 2 == 0:
                pieces[i] = re.sub(r"\s*=\s*", "=", re.sub(r"\s+", " ", piece))
            elif one_line:
                pieces[i] = piece[0] + squeeze(piece[1:-1]) + piece[0]
        tags.append(re.sub(r"\s*(/?>)$", r"\1", "".join(pieces)))
    return ("" if one_line else "\n").join(tags) + "\n"


MINIFIERS = {
    ".css": minify_css,
    ".xml": lambda text: minify_markup(text, False),
    ".svg": lambda text: minify_markup(text, True),
}


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = os.path.abspath(sys.argv[1])
    if (os.path.normcase(out) + os.sep).startswith(os.path.normcase(SOURCE) + os.sep):
        sys.exit("the copy can't go inside workshop/panorama")
    before = after = files = 0
    for root, _, names in os.walk(SOURCE):
        target_dir = os.path.join(out, os.path.relpath(root, SOURCE))
        os.makedirs(target_dir, exist_ok=True)
        for name in names:
            path = os.path.join(root, name)
            with open(path, "rb") as f:
                data = f.read()
            minify = MINIFIERS.get(os.path.splitext(name)[1].lower())
            small = data
            if minify:
                try:
                    small = minify(data.decode("utf-8")).encode("utf-8")
                except ValueError as e:
                    sys.exit(f"{path}: {e}")
            with open(os.path.join(target_dir, name), "wb") as f:
                f.write(small)
            before += len(data)
            after += len(small)
            files += 1
    print(
        f"{files} files minified, {before} to {after} bytes ({100 * (before - after) / before:.1f}% less)"
    )


if __name__ == "__main__":
    main()
