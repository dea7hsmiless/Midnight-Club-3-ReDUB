#!/usr/bin/env python3
"""Every required GL entry point must exist in OpenGL ES, or be shimmed.

Why this exists
---------------
0.2.6 added ambient occlusion. It needed a depth-only framebuffer, so it called
glDrawBuffer and added that name to the *required* table in gl_api.h.
glDrawBuffer is desktop GL; OpenGL ES only has the plural glDrawBuffers, so the
name cannot resolve at any ES version. nv2a_gl_load() counts one unresolved
required name as fatal, so the renderer stopped after creating the window and
before drawing anything: the Android and iOS builds played audio with a black
screen for every player, on a release every check had passed.

Nothing here tested that table. This does, statically, so the next desktop-only
name added to it fails the release pre-flight instead of a player's phone.

What it checks
--------------
For each name in NV2A_GL_FUNCS (the required table):
  - it is in the ES allowlist below, or
  - gl_api.c maps it onto an ES equivalent when nv2a_gl_es is set.

The allowlist is the set of names that exist in OpenGL ES 3.0/3.1 -- what the
renderer actually gets on a phone (0.2.6.1 logged "OpenGL ES 3.1" on the
emulator; many Android drivers top out there). A name outside it needs a shim,
and a shim needs an entry in SHIMMED below so the pairing is explicit rather
than implied by a strcmp buried in a macro.

Exit: 0 clean, 1 a problem, 2 the files could not be read.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "xboxrecomp" / "src" / "nv2a_gl" / "gl_api.h"
SOURCE = ROOT / "xboxrecomp" / "src" / "nv2a_gl" / "gl_api.c"

# OpenGL ES 3.1 core and the ES 3.0 names the renderer needs, i.e. everything a
# phone can be expected to export. Deliberately a literal list rather than a
# guess from the name: a checker that "looks about right" is how this shipped.
# Adding a name here is a claim that ES provides it; do that only with the ES
# spec (or the ES3 header) open.
ES_PROVIDED = {
    "glActiveTexture", "glAttachShader", "glBindAttribLocation", "glBindBuffer",
    "glBindFramebuffer", "glBindRenderbuffer", "glBindTexture", "glBindVertexArray",
    "glBlendColor", "glBlendEquation", "glBlendFunc", "glBlitFramebuffer",
    "glBufferData", "glBufferSubData", "glCheckFramebufferStatus", "glClear",
    "glClearColor", "glClearStencil", "glColorMask", "glCompileShader",
    "glCompressedTexImage2D", "glCopyTexSubImage2D", "glCreateProgram",
    "glCreateShader", "glCullFace", "glDeleteBuffers", "glDeleteFramebuffers",
    "glDeleteProgram", "glDeleteRenderbuffers", "glDeleteShader",
    "glDeleteTextures", "glDepthFunc", "glDepthMask", "glDisable",
    "glDisableVertexAttribArray", "glDrawArrays", "glDrawElements", "glEnable",
    "glEnableVertexAttribArray", "glFinish", "glFramebufferRenderbuffer",
    "glFramebufferTexture2D", "glFrontFace", "glGenBuffers", "glGenFramebuffers",
    "glGenRenderbuffers", "glGenTextures", "glGenVertexArrays", "glGetError",
    "glGetIntegerv", "glGetProgramInfoLog", "glGetProgramiv",
    "glGetShaderInfoLog", "glGetShaderiv", "glGetString", "glGetUniformLocation",
    "glLinkProgram", "glMapBufferRange", "glPixelStorei", "glReadBuffer",
    "glReadPixels", "glRenderbufferStorage", "glScissor", "glShaderSource",
    "glStencilFunc", "glStencilMask", "glStencilOp", "glTexImage2D",
    "glTexParameteri", "glTexSubImage2D", "glUniform1f", "glUniform1i",
    "glUniform1iv", "glUniform2fv", "glUniform4fv", "glUniformMatrix4fv",
    "glUnmapBuffer", "glUseProgram", "glVertexAttrib2f", "glVertexAttrib4f",
    "glVertexAttribPointer", "glViewport",
}

# Required names that are NOT in ES, each with the ES call its shim uses.
# gl_api.c must map every key here when nv2a_gl_es is set; the check compares
# the two so neither side can drift on its own.
SHIMMED = {
    "glClearDepth": "glClearDepthf",
    "glDepthRange": "glDepthRangef",
    "glDrawBuffer": "glDrawBuffers",
}


def required_names(text: str) -> list[str]:
    start = text.index("#define NV2A_GL_FUNCS")
    end = text.index("#define NV2A_GL_OPT_FUNCS")
    return re.findall(r"X\([^,]+,\s*(gl\w+)", text[start:end])


def shimmed_names(source: str) -> set[str]:
    """Names gl_api.c maps for ES, from its strcmp(#name, "glX") guards."""
    return set(re.findall(r'strcmp\(#name,\s*"(gl\w+)"\)', source))


def main() -> int:
    try:
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")
    except OSError as e:
        print(f"cannot read the GL sources: {e}", file=sys.stderr)
        return 2

    names = required_names(header)
    if not names:
        print("no required entry points found in gl_api.h", file=sys.stderr)
        return 2
    if len(names) != len(set(names)):
        dupes = sorted({n for n in names if names.count(n) > 1})
        print(f"FAIL  duplicate names in NV2A_GL_FUNCS: {', '.join(dupes)}")
        return 1

    mapped = shimmed_names(source)
    problems = []

    for name in names:
        if name in ES_PROVIDED:
            if name in mapped:
                problems.append(
                    f"{name} is provided by ES and also shimmed in gl_api.c; "
                    f"drop one (a stray shim hides a real ES name)")
            continue
        if name in SHIMMED:
            if name not in mapped:
                problems.append(
                    f"{name} is not in ES and needs a shim ({SHIMMED[name]}), but "
                    f"gl_api.c has no nv2a_gl_es guard for it: the renderer would "
                    f"count it missing and stop")
            continue
        if name in mapped:
            problems.append(
                f"{name} has a gl_api.c shim but is not listed in this checker's "
                f"SHIMMED table; add it with the ES call it uses")
            continue
        problems.append(
            f"{name} is required, is not in OpenGL ES, and has no ES shim. On "
            f"Android and iOS nv2a_gl_load() would count it missing and the "
            f"renderer would exit before drawing (this is how 0.2.6 shipped a "
            f"black screen). Either map it in gl_api.c like glDrawBuffer, or "
            f"move it to NV2A_GL_OPT_FUNCS if the renderer can run without it.")

    for name in sorted(mapped - set(SHIMMED) - ES_PROVIDED):
        if not any(name in p for p in problems):
            problems.append(
                f"{name} is shimmed in gl_api.c but is not in this checker's "
                f"tables; declare it as ES_PROVIDED or SHIMMED")

    for name in sorted(set(SHIMMED) - mapped):
        if not any(name in p for p in problems):
            problems.append(
                f"{name} is listed as SHIMMED here but gl_api.c has no guard for "
                f"it; the two must agree")

    if problems:
        for p in problems:
            print(f"FAIL  {p}")
        return 1

    print(f"PASS  {len(names)} required entry points: "
          f"{len(ES_PROVIDED & set(names))} provided by OpenGL ES, "
          f"{len(SHIMMED)} shimmed, 0 unresolved")
    return 0


if __name__ == "__main__":
    sys.exit(main())
