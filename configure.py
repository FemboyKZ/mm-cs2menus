# vim: set sts=4 ts=8 sw=4 tw=99 et:
import os
import sys

shared = os.path.join(sys.path[0], "mm-utils", "build", "configure.py")
exec(compile(open(shared, encoding="utf-8").read(), shared, "exec"))
