# Repository Guidelines

## Project Structure & Modules
- Core source lives in `framework/` (application, decode, encode, format, util, generated) and the Vulkan capture layer is in `layer/`.
- Tools are in `tools/` (`replay`, `compress`, `convert`, `extract`, `info`), with helper scripts under `scripts/`.
- Build configuration files live in `cmake/` and `CMakeLists.txt`; platform specifics under `android/` and `docs/` for usage notes.
- Tests and sample workloads reside in `test/` (see `test/test_apps` and launcher code); third-party code is in `external/`.

## Build, Test, and Development Commands
- `python scripts/build.py -j 8 --test-apps` – configure and build for desktop (runs clang-format check and builds tests).

## Coding Style & Naming Conventions
- C++ follows the Google C++ Style Guide with the repo’s `.clang-format` (ClangFormat 14). Use `GFXRECON_ASSERT`, compare pointers to `nullptr`, and do not modify `framework/generated` directly.
- Python follows PEP 8; use `yapf -i` with `.style.yapf`.
- Keep interfaces STL-like when intended; prefer explicit, descriptive names for branches/features (e.g., `fix-1234`, `add-openxr-capture-flag`).

## Testing Guidelines
- After a build, run the test script `build/<platform>/<arch>/output/test/run-tests.sh`.

## Commit & Pull Request Guidelines
- Commits: 50-char subject, blank line, 72-char wrapped body; imperative tone (“Add…”, “Fix…”), avoid proprietary titles, and keep each commit buildable/tested.
- Branch from `dev`; push work to your fork and open PRs from there.
- PRs should describe scope, link issues, list platforms/tests run, and include any relevant screenshots or logs for tool changes. Keep changes focused; avoid unrelated refactors.
