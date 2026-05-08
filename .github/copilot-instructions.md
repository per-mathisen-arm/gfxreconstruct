# GFXReconstruct Copilot Instructions

GFXReconstruct captures graphics API calls (Vulkan, DirectX 12, OpenXR) to `.gfxr` files and replays them. The codebase has three supported APIs and a code-generation pipeline for all three.

## Build & Test

```bash
# Full build (runs clang-format check, builds tests)
python scripts/build.py -j 8 --test-apps

# Skip developer checks (CI-friendly)
python scripts/build.py --skip-check-code-style --skip-tests

# Incremental build after CMake configure
cd build/<platform>/<arch>/cmake_output && make -j 8

# Run tests after build
build/<platform>/<arch>/output/test/run-tests.sh

# Windows: format staged changes before committing
git clang-format-14
```

CMake options:
- `-DGFXRECON_ENABLE_OPENXR=OFF` — exclude OpenXR support
- `-DD3D12_SUPPORT=OFF` — exclude DirectX 12 support
- build.py option: `--skip-d3d12-support` — exclude DirectX 12 support
- Windows D3D12 requires Windows SDK `10.0.26100.0`; specify with `-DCMAKE_SYSTEM_VERSION=10.0.26100.0`

## Architecture

### Directory quick map (for AI navigation)
- `framework/encode/` — capture/trace encoding logic for Vulkan, DX12, and OpenXR.
- `framework/decode/` — replay/decode logic and API call consumers.
- `framework/format/` — `.gfxr` block/type definitions (maintain backward compatibility).
- `framework/application/` — replay app/window/surface plumbing.
- `framework/generated/` — generated code and generators; **do not hand-edit generated C++ files**.
- `layer/` — API interception entry points/hooks (Vulkan layer, DX12/DXGI hooks, OpenXR layer pieces).
- `tools/replay/` — replay executable entry points and runtime wiring.
- `tools/optimize/` — optimization passes for capture files (including DX12-specific optimizations).
- `tools/compress/` — compression/decompression utility for `.gfxr` captures.
- `tools/info/` — structured inspection/summarization tool for capture contents.
- `tools/convert/`, `tools/extract/` — conversion and data-extraction utilities.
- `scripts/build.py` — primary cross-platform build/test driver.
- `test/` — installed test runner scripts and test app execution helpers.

### Capture pipeline (encode/trace)

**Vulkan:**
1. `layer/layer_vulkan_entry.cpp` — Vulkan layer entry points intercept app calls
2. `framework/encode/vulkan_capture_manager.cpp` — serializes intercepted calls via `ParameterEncoder`
3. `framework/encode/capture_manager.cpp` — writes serialized blocks to the `.gfxr` file
4. State tracking (`vulkan_state_tracker.cpp`) enables trimmed captures (capture starting mid-run)

**DirectX 12 (Windows-only):**
1. DLL interception (hooking), not a graphics API layer — `layer/d3d12/hook_d3d12.cpp` and `layer/dxgi/hook_dxgi.cpp` use `layer/gfxrecon_interceptor/` to redirect D3D12 and DXGI system DLL calls. Both `d3d12.dll` and `dxgi.dll` are separately hooked.
2. D3D12/DXGI objects are wrapped via COM-style wrappers, all deriving from `IUnknown_Wrapper` (`framework/encode/iunknown_wrapper.h`). Generated wrappers live in `framework/generated/generated_dx12_wrappers.cpp/h`. When the app calls `QueryInterface`, it receives the GFXR wrapper, not the real object.
3. `framework/encode/d3d12_capture_manager.cpp` — coordinates capture; maintains DXGI and D3D12 dispatch tables pointing to the real system DLL functions.
4. `framework/encode/dx12_state_tracker.cpp` — tracks all live D3D12 objects for trim-state reconstruction.
5. **GPU virtual address (GPU VA) annotation** is a D3D12-specific challenge: GPU VAs embedded in resource data (e.g., shader tables, constant buffers) are captured as-is and annotated by `Dx12ResourceValueAnnotator` (`framework/encode/dx12_rv_annotator.h`), which scans upload heap writes to identify embedded VAs, descriptor handles, and shader identifiers.
6. AMD AGS support is compiled in when `GFXRECON_AGS_SUPPORT` is defined; entry points are in `framework/encode/custom_ags_*.cpp` and `layer/ags_capture/`.

**OpenXR:** `layer/` + `framework/encode/openxr_capture_manager.cpp`

### Replay pipeline (decode)

**Common path:**
1. `framework/decode/file_processor.cpp` — reads blocks from `.gfxr`
2. API-specific decoders (`vulkan_decoder_base.cpp`, `dx12_decoder_base.cpp`, etc.) deserialize each block
3. Consumer classes process decoded calls
4. Window/surface management lives in `framework/application/`

Main entry: `tools/replay/desktop_main.cpp` (Linux/Windows), `tools/replay/android_main.cpp`

**DirectX 12 replay specifics:**
- `framework/decode/dx12_replay_consumer_base.cpp` + generated `generated_dx12_replay_consumer.cpp` — re-issues D3D12/DXGI API calls.
- Live object tracking uses `Dx12ObjectInfoTable` (handle → `DxObjectInfo` subclass); all object info types are in `framework/decode/dx12_object_info.h`.
- **GPU VA remapping**: captured GPU VAs must be translated to replay-time addresses. `Dx12ResourceValueMapper` (`framework/decode/dx12_resource_value_mapper.cpp`) handles this at replay using `Dx12GpuVaMap`, `Dx12DescriptorMap`, and `Dx12ShaderIdMap`.
- `Dx12OffscreenSwapchain` (`framework/decode/dx12_offscreen_swapchain.cpp`) provides the equivalent of Vulkan's virtual swapchain for D3D12.
- The optimize pass (`tools/optimize/dx12_file_optimizer.cpp`) removes redundant `InitializeHeap`/`FillMemory` blocks and redundant resource state transitions.

### Code generation
`framework/generated/generated_*.cpp/h` are auto-generated — **never edit them directly**.

To regenerate after editing Python generators or after updating Vulkan-Headers, OpenXR-SDK, or DirectX headers:
```bash
cd framework/generated
python3 generate_vulkan.py   # Vulkan
python3 generate_dx12.py     # DirectX 12
python3 generate_openxr.py   # OpenXR
```
Minimum Python version: 3.10.

### Trimming mechanism

Trimming captures only a range of frames (or queue submits, or draw calls) rather than the full application run. The capture manager operates in one of three modes at any given time, controlled by `CaptureModeFlags` in `framework/encode/capture_manager.h`:

| Mode | Value | Meaning |
|---|---|---|
| `kModeDisabled` | `0x0` | Not capturing or tracking |
| `kModeTrack` | `0x2` | Tracking live object state, not writing to file |
| `kModeWrite` | `0x1` | Writing API calls to file |
| `kModeWriteAndTrack` | `0x3` | Both (used briefly at trim start) |

**Before the trim range** — the manager runs in `kModeTrack`. `VulkanStateTracker` (or `Dx12StateTracker`) silently records every object creation, destruction, and state mutation without writing anything to disk. This is continuous shadow-bookkeeping: every `vkCreateBuffer`, pipeline creation, memory bind, image layout transition, etc. updates the in-memory state tables (`generated_vulkan_state_table.h`, `generated_dx12_state_table.h`).

**At trim start** — on the frame/submit/draw-call boundary that begins the trim range, `CheckStartCaptureForTrackMode()` fires. The manager:
1. Opens the `.gfxr` output file.
2. Calls `VulkanStateTracker::WriteState()` → `VulkanStateWriter` (or `Dx12StateWriter`) to emit a **synthetic state snapshot**: fake API call blocks that recreate every live object exactly as it stands at this moment (allocations, bound memory, pipeline state, image layouts, buffer contents, descriptor sets, etc.). Resource memory contents are read back from the GPU at this point.
3. Transitions to `kModeWrite` and begins recording the app's real calls verbatim.

**During the trim range** — `kModeWrite`; state tracking may also continue (`kModeWriteAndTrack`) to support nested or multi-range captures.

**After the trim range ends** — `CheckContinueCaptureForWriteMode()` closes the file and transitions back to `kModeDisabled` (or `kModeTrack` if further ranges exist).

**Trim boundaries** (`CaptureSettings::TrimBoundary`):
- `kFrames` — frame numbers; set via `GFXRECON_CAPTURE_FRAMES=3-7` (env var)
- `kQueueSubmits` — queue submit index; set via `GFXRECON_CAPTURE_QUEUE_SUBMITS`
- `kDrawCalls` — draw call granularity; set via `GFXRECON_CAPTURE_DRAW_CALLS`
- Hotkey trigger: `GFXRECON_CAPTURE_TRIGGER=<key>` starts/stops capture on key press (with optional `GFXRECON_CAPTURE_TRIGGER_FRAMES` to auto-stop)

**Memory tracking during `kModeTrack`** — the capture manager must still track which regions of mapped host-visible memory have been written, so the state snapshot can read back correct resource data. Three modes (set via `GFXRECON_MEMORY_TRACKING_MODE`):
- `kPageGuard` (default) — installs a shadow allocation + SIGSEGV/guard-page handler to detect dirty pages
- `kAssisted` — relies on the app calling `vkFlushMappedMemoryRanges`; lighter but less reliable
- `kUnassisted` — flushes all mapped memory on every unmap/submit

**State writer key files:**
- `framework/encode/vulkan_state_writer.cpp` — emits synthetic Vulkan calls to reconstruct all live state
- `framework/encode/dx12_state_writer.cpp` — D3D12 equivalent
- `framework/encode/vulkan_state_tracker_initializers.h` — template helpers that extract creation parameters from handle wrappers for later re-emission

### Consumer pattern
To process a capture file, subclass `VulkanConsumerBase` (or `Dx12ConsumerBase`, `OpenXRConsumerBase`) and override `Process_vk*` methods. The generated `generated_vulkan_consumer.h` declares all virtual methods. Register the consumer with `FileProcessor`. Existing tools (`gfxrecon-info`, `gfxrecon-convert`) follow this pattern.

### Capture file format
- Binary blocks; block types defined in `framework/format/format.h`
- **Never modify existing block structs or IDs.** Add new IDs/structs and deprecate old ones (append `_deprecated` to enum/struct names); see `CONTRIBUTING.md` for the deprecation procedure.
- Metadata commands (not app API calls) are stored separately from API call blocks.

## Key Conventions

### Terminology
- Capture = encode = trace
- Replay = decode
- "Trimming" = capturing only a subset of frames, not the full run

### C++ style
- All code lives under the `gfxrecon` namespace; sub-namespaces are `encode`, `decode`, `format`, `util`, `graphics`, `application`
- Use `GFXRECON_BEGIN_NAMESPACE(x)` / `GFXRECON_END_NAMESPACE(x)` macros for namespace declarations
- Use `GFXRECON_ASSERT` (not `assert`)
- Compare pointers explicitly: `if (ptr == nullptr)`, not `if (!ptr)`
- ClangFormat version **14** is required (not 15 or 16); use `.clang-format` at repo root

### Python style
- PEP 8; auto-format with `yapf -i` using `.style.yapf` at repo root

### New files
- Every new file must include the MIT license header and LunarG copyright; see any existing file for the format

### custom_ vs generated_
- `framework/encode/custom_vulkan_*.cpp/h` — hand-written special cases for API calls that require non-trivial encoding logic not expressible by the generators
- `framework/decode/custom_vulkan_*.cpp/h` — hand-written special cases on the decode side
- `framework/generated/generated_vulkan_*.cpp/h` — generated; do not edit

### Capture file backward compatibility (within a major version)
- Existing `.gfxr` files must remain decodable; do not remove or reuse block type IDs
- Existing `VulkanConsumerBase`, `Dx12ConsumerBase`, `MetadataConsumerBase` method signatures are frozen from v1.0.4 onward
- Existing `framework/util/` public function signatures are frozen; see `docs/GFXReconstruct Architectural Principles.md`

### PRs and branches
- All work branches off `dev`; PRs target `dev`, not `main`
- Branch naming: `fix-<issue-number>` for bug fixes, descriptive name for features
- Commit subject ≤50 chars; body wrapped at 72 chars; imperative mood
- Do not mention proprietary application titles in commits

### Code review guidelines and specifications
For design or architecture-impacting changes, read `docs/GFXReconstruct Architectural Principles.md` first and verify the proposal matches those principles.

#### Trigger and scope (strict)

- Use commit-level review only when a commit/PR/diff is explicitly targeted.
- If `code review` has no target, ask for scope first (commit hash, PR, or working tree diff).
- Review mode is findings-first. Do not start with a summary.
- Focus on bugs, regressions, compatibility risks, API spec conformance, and missing tests before style suggestions.
- If the commit hash is not provided, ask for it or infer the latest commit only when the user clearly intends that.

#### Required review workflow

1. Inspect the commit diff and changed files.
2. Prioritize correctness, API/spec conformance, and backward compatibility.
3. Verify cross-platform impact when relevant (Windows/Linux/Android).
4. Check test impact and note missing validation.
5. Report findings with severity and file/line references.

#### Review focus (priority order):

- Correctness and behavioral regressions
- Capture/replay compatibility and file format stability
- Cross-platform impact (Windows/Linux/Android where applicable)
- Test coverage and missing validation
- Maintainability and consistency with existing subsystem patterns

API-specific conformance requirements:
- For Vulkan files, code changes must conform to Vulkan specification requirements.
- For D3D12(dx12) files, code changes must conform to DirectX 12 specifications.
- For OpenXR files, code changes must conform to OpenXR specifications.

Specification conformance checks are mandatory for API-related changes.

External references required for deep API review:

Vulkan review prerequisites:
- Ensure Khronos Vulkan spec repos are available under `external/` (clone them if missing):
```bash
cd external/
git clone https://github.com/KhronosGroup/Vulkan-Registry.git
git clone https://github.com/KhronosGroup/Vulkan-Guide.git
```

D3D12(dx12) review prerequisites:
- Ensure Microsoft DirectX specs are available under `external/` (clone if missing):
```bash
cd external/
git clone https://github.com/microsoft/DirectX-Specs.git
```

#### Review output expectations:

- List findings first, ordered by severity, with file and line references.
- Call out assumptions or unknowns explicitly.
- If no findings are found, state that clearly and note residual risks/testing gaps.

Required output shape:
- `Findings` section first (ordered: Critical, High, Medium, Low).
- `Open questions / assumptions` section second.
- Optional `Change summary` section last.

Notes:
- Do not edit generated files under `framework/generated/` directly.
- Preserve backward compatibility for existing `.gfxr` files and frozen public signatures as documented in `CONTRIBUTING.md`.

### Vulkan extension research
When investigating an unfamiliar extension, fetch its spec:
`https://docs.vulkan.org/refpages/latest/refpages/source/<extension-name>.html`
