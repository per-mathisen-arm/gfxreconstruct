---
name: check_file_upstream
description: Look for bugs in upstream merge artifacts in given file(s)
metadata:
  short-description: Check against upstream version of files to see if bugs were introduced when we merged in changes from upstream
---

This is for review only - make no fixes to code unless explicitly told to do so.

Motivation: We are Arm and maintain a downstream fork of gfxreconstruct with some special changes
of our own with especially related to our postprocessing-based approach to handling raytracing and
bindless Vulkan and support for Arm extensions. We frequently merge in changes from upstream and
this merge process can introduce bugs and unintended artifacts in the code like duplicated lines of
code, missing code, and conflicting code. We sometimes comment out upstream code that frequently
conflict with ours to make merging easier.

1. Check that we were told which files to validate. If not, stop and ask.
2. For each file, run `git diff origin/upstream/dev -- <name of file>` to compare our version of
   the file against upstream.
3. If there are changes that look suspicious, try `git blame` on the file to see where the change
   comes from. If it comes from an upstream merge commit, it will typically say something like
   `Merge remote-tracking branch 'internal/upstream/dev' into dev-arm`.
4. If there are no findings, keep the summary short.
