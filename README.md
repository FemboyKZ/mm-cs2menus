# CS2 Menu API

![Downloads](https://img.shields.io/github/downloads/FemboyKZ/mm-cs2menus/total?style=flat-square) ![Last commit](https://img.shields.io/github/last-commit/FemboyKZ/mm-cs2menus?style=flat-square) ![Open issues](https://img.shields.io/github/issues/FemboyKZ/mm-cs2menus?style=flat-square) ![Closed issues](https://img.shields.io/github/issues-closed/FemboyKZ/mm-cs2menus?style=flat-square) ![Size](https://img.shields.io/github/repo-size/FemboyKZ/mm-cs2menus?style=flat-square) ![GitHub Workflow Status](https://img.shields.io/github/actions/workflow/status/FemboyKZ/mm-cs2menus/build.yml?style=flat-square)

CS2 Menu API Plugin using Metamod: Source

## Usage

### Requirements

* CS2 Dedicated Server
* [Metamod: Source 2.0](https://www.metamodsource.net/downloads.php?branch=dev)

## Build

### Prerequisites

* This repository is cloned recursively (ie. has submodules)
* [python3](https://www.python.org/)
* [ambuild](https://github.com/alliedmodders/ambuild), make sure ``ambuild`` command is available via the ``PATH`` environment variable;
* MSVC (VS build tools)/Clang installed for Windows/Linux.

### AMBuild

```bash
mkdir -p build && cd build
python3 ../configure.py --enable-optimize
ambuild
```

## Credits

* [zer0.k's MetaMod Sample plugin fork](https://github.com/zer0k-z/mm_misc_plugins)
* [cs2kz-metamod](https://github.com/KZGlobalTeam/cs2kz-metamod)
