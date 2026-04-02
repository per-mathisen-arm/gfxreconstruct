---
name: check_upstream_merge_commit
description: Look for issues in upstream merge commit
metadata:
  short-description: Check a merge commit that pulls in changes from upstream for possible issues
---

This is for review only - make no fixes to code unless explicitly told to do so.

Motivation: We are Arm and maintain a downstream fork of gfxreconstruct with some special changes
of our own with especially related to our postprocessing-based approach to handling raytracing and
bindless Vulkan and support for Arm extensions. We frequently merge in changes from upstream and
this merge process can introduce bugs and unintended artifacts in the code like duplicated lines of
code, missing code, and conflicting code. We sometimes comment out upstream code that frequently
conflict with ours to make merging easier.

1. Check that we were told which commit to validate. If not, stop and ask.
2. For each commit, first check the git diff of the changes to look for anything suspicious.
3. If there are no findings, keep the summary short.
