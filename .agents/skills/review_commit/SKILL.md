---
name: review_commit
description: Look for issues in a commit
metadata:
  short-description: Check a commit for possible issues
---

This is for review only - make no fixes to code unless explicitly told to do so.

Motivation: We are Arm and maintain a fork of LunarG's GFXReconstruct.
We implement new features, options, support for new Vulkan extensions and maintain a different
approach to postprocessing, raytracing, bindless and Arm extensions support.
We want to make sure our commits do not introduce bugs, important performance regressions,
undefined behaviors or incomprehensible code/documentation.

1. Check if we were told which commit to validate. Otherwise, assume HEAD is the commit to validate.
2. First check the git diff of the changes to look for anything suspicious.
3. If there are no findings, keep the summary short.
