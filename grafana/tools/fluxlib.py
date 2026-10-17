"""Shared helpers: expand //@include in a Flux file."""
import os, re

def expand(path):
    out = []
    for line in open(path).read().split("\n"):
        m = re.match(r"\s*//@include\s+(\S+)", line)
        if m:
            out.append(open(os.path.join(os.path.dirname(path), m.group(1) + ".part")).read())
        else:
            out.append(line)
    return "\n".join(out)
