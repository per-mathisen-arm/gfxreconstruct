# DX12 Resource Aliasing

## Spec references

| Topic | Reference |
|---|---|
| Placed resources | [Placed Resources — MSDN](https://learn.microsoft.com/en-us/windows/win32/direct3d12/placed-resources) |
| Memory aliasing and data inheritance | [Memory Aliasing and Data Inheritance — MSDN](https://learn.microsoft.com/en-us/windows/win32/direct3d12/memory-aliasing-and-data-inheritance) |
| Resource aliasing barrier | [D3D12_RESOURCE_ALIASING_BARRIER — MSDN](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_resource_aliasing_barrier) |
| Resource aliasing (DirectX Specs) | [Resource Aliasing — DirectX-Specs](https://microsoft.github.io/DirectX-Specs/d3d/ResourceBinding.html#resource-aliasing) |
| Why aliasing barriers are expensive | [Aliasing Barriers Are Very Expensive — DirectX-Specs](https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html#aliasing-barriers-are-very-expensive) |

---

## Scope

This document covers the GFXReconstruct pipeline for handling **D3D12 placed-resource aliasing** during
optimize (`gfxrecon-optimize`) and replay (`gfxrecon-replay --memory-translation rebind`).

Covered API paths:

1. `CreatePlacedResource`
2. `CreatePlacedResource1`
3. `CreatePlacedResource2`
4. `OverrideCreatePlacedResource*` accepts `max_aliasing_size` (default `0`) and forwards it to allocator paths.

**Out of scope:** Enhanced Barrier API (`ID3D12GraphicsCommandList7::Barrier`). Enhanced barriers have no
native `D3D12_RESOURCE_BARRIER_TYPE_ALIASING` type; aliasing is expressed implicitly via per-resource
`D3D12_BARRIER_ACCESS_NO_ACCESS` transitions with `D3D12_TEXTURE_BARRIER_FLAG_DISCARD`. Those are not
intercepted by this feature (see Known Limitations).

Aliasing means two or more placed resources share overlapping byte ranges on the same `ID3D12Heap`.
Only one of them is "active" at any time; switching between them requires an aliasing barrier.

---

## Problem

The rebind memory allocator (`Dx12RebindAllocator`) re-creates placed resources using D3D12MA instead of
`ID3D12Heap`. It does not have the original heap, so it cannot simply call `CreatePlacedResource` at the
captured offset. Aliasing groups must be identified by the optimizer and communicated to the replayer via
a custom metadata block in the `.gfxr` file, so the rebind allocator can provide a single large backing
allocation shared across all resources in the group.

---

## Solution overview

```
gfxrecon-optimize                       optimized .gfxr
  ┌─────────────────────────────┐           │
  │ Dx12ResourceAliasingModifier│           │  kDx12ResourceAliasingCommand
  │  - intercept GetResourceAllocInfo*      │  metadata blocks (one per
  │  - track all placed resources│          │  aliasing component)
  │  - detect range+lifetime overlap        │
  │  - union-find components    │  ─────►   │
  │  - inject metadata blocks   │           │
  └─────────────────────────────┘           │
                                            ▼
                               gfxrecon-replay (rebind mode)
                                ┌────────────────────────────────┐
                                │ ProcessDx12ResourceAliasingCmd │
                                │  - compute max_allocation_size │
                                │  - populate aliasing_resource_ │
                                │    sizes_[resource_id]         │
                                │                                │
                                │ OverrideCreatePlacedResource*  │
                                │  - pass max_aliasing_size to   │
                                │    Dx12RebindAllocator         │
                                │                                │
                                │ PreCall_ResourceBarrier        │
                                │  - aliasing barriers: convert  │
                                │    non-aliasing to global      │
                                │    (null, null)                │
                                └────────────────────────────────┘
```

---

## Example (spec Figure 1 — asymmetric aliasing)

Heap H (all values in MB):

```
heap_offset →  0        8        12       16       20       24       28       32
               |        |        |        |        |        |        |        |
OldTex1 @0    [==========================]                              (0..24MB)
NewTex1 @0    [===================]                                     (0..20MB)
NewBuf1 @8MB           [========]                                       (8..16MB)
OldBuf2 @12MB                   [===================]                   (12..28MB)
NewTex2 @20MB                                [==============]           (20..32MB)
OldBuf1 @24MB                                        [========]         (24..32MB)
```

All six resources are in one connected component. The optimizer emits one
`kDx12ResourceAliasingCommand` block per resource (six blocks), each listing all six members.

Replay rebind allocator:
- **Buffers** (NewBuf1, OldBuf2, OldBuf1): share `offset_map[0]` → one large allocation
- **Textures** (OldTex1, NewTex1): both at `HeapOffset=0` → share `offset_map[0]` → same allocation as buffers
- **NewTex2** at `HeapOffset=20MB` → `offset_map[20MB]` → separate allocation

Note: OldTex1 and NewTex1 at offset 0 share an allocation with the buffers; aliasing barriers
between them are valid. NewTex2 is in a different allocation; aliasing barriers involving it are
converted to global barriers (see barrier handling above).

---

## Aliasing detection — detection rules reference

Aliasing detection happens in `Dx12ResourceAliasingModifier` during the optimize pass and has two
independent sources.

### Source 1: Explicit aliasing barrier

Any resource that appears in a `D3D12_RESOURCE_BARRIER_TYPE_ALIASING` barrier
(either `pResourceBefore` or `pResourceAfter`) is unconditionally marked as aliasing.
Offset, size and lifetime are **not** checked for this path.

Per D3D12 spec:

> "An aliasing barrier must be issued between the usage of two resources that share the
> same physical memory, even if data inheritance is not desired."

The optimizer's `Process_ID3D12GraphicsCommandList_ResourceBarrier` records resources that appear in
`D3D12_RESOURCE_BARRIER_TYPE_ALIASING` barriers during the analysis pass, inserting both
`pResourceBefore` and `pResourceAfter` into `aliasing_resource_ids_`. These are combined with
range-overlap resources before `BuildAliasingComponents`.

### Source 2: Implicit detection (`DetectImplicitAliasingResources`)

Sweep-line over placed resources sorted by `(heap_id, heap_offset)`. Two resources are detected as
aliasing if:

1. Same heap
2. Byte-range overlap (using `estimated_size`; falls back to same-offset if size unknown)
3. Lifetime overlap (`creation_index` / `destruction_index` ranges intersect)
4. `can_alias_pair`: skips pairs where either is a texture **and** offsets differ

### Case table

| Resource A | Resource B | offsets | can_alias_pair | Notes |
|---|---|---|---|---|
| Buffer | Buffer | same | ✓ | standard case |
| Buffer | Buffer | differ | ✓ | overlap detected from estimated_size |
| Texture | Texture | same | ✓ | both placed at same offset |
| Texture | Texture | differ | ✗ | skipped; each gets its own allocation |
| Buffer | Texture | same | ✓ | mixed group, same offset |
| Buffer | Texture | differ | ✗ | skipped |

**Summary:**
- **Same offset + overlapping range + overlapping lifetime** → detected as aliasing
- **Same offset + overlapping range + non-overlapping lifetime** → not detected (time-sharing heap range)
- **Different offset + overlapping range + overlapping lifetime** → detected as aliasing
- **Different offset + overlapping range + non-overlapping lifetime** → not detected (time-sharing heap range)

---

#### Lifetime overlap

Resources A and B coexist in the capture stream iff:

```
A.creation_index < B.destruction_index  AND  B.creation_index < A.destruction_index
```

`destruction_index == UINT64_MAX` means the resource was never released.

---

## Barrier suppression for fallback resources

Resources **not** in `aliasing_resource_sizes_` (texture-only groups where size could not be
determined) are created as committed resources. They cannot be valid aliasing-barrier operands.

The `PreCall_ID3D12GraphicsCommandList_ResourceBarrier` handler converts **all** aliasing barriers
unconditionally to `(nullptr, nullptr)` in rebind mode, which covers both:
- Resources not in `aliasing_resource_sizes_` (committed fallback)
- Resources in `aliasing_resource_sizes_` but in different D3D12MA allocations (which would also
  fail D3D12 validation)

This prevents `ID3D12GraphicsCommandList::Close()` returning `E_INVALIDARG`.

---

## Known limitations

### Texture-only aliasing groups get committed fallback

When `ProcessDx12ResourceAliasingCommand` runs, `active_devices_` is empty (the metadata block
precedes any `CreateDevice` replay). Textures contribute `0` to `max_allocation_size`. If the group
has no buffers, `max_allocation_size == 0` and the group is ignored — all resources in it fall back
to individual committed resources. The warning message is:

```
Failed to determine maximum allocation size for aliased resources. Aliasing metadata will be ignored.
```

### Different-offset texture aliasing not supported

Textures at different heap offsets on the same heap are deliberately excluded from the same aliasing
component (`can_alias_pair` returns false). Each such texture gets its own D3D12MA allocation with
`aliasing_offset = 0`. Aliasing barriers between them are converted to global barriers.

### Acceleration structure buffer aliasing not supported

In rebind mode, DXR acceleration-structure buffers are treated as a special case and are not considered
safe for normal placed-resource aliasing/rebind flow, because the rebind allocator forces AS resources to
committed resources instead of placed resources.They do not share a backing allocation with other placed
resources. Any aliasing barrier that references such resources is converted to a global barrier
(`nullptr`, `nullptr`) to avoid replay validation failures.

### Same-offset texture and buffer aliasing not supported

Mixed texture+buffer aliasing at the same captured heap offset is not guaranteed in rebind mode.

The allocator uses different aliasing-allocation keys for textures and buffers:
- Textures are keyed by `HeapOffset`
- Buffers are keyed by `0` and use the entire heap as aliasing sub-offset

Because of this, a same-offset texture and buffer can end up in different D3D12MA allocations
(except the special case where `HeapOffset == 0`). When that happens, the pair is not considered a
valid aliasing pair during replay, and aliasing barriers between them are converted to global
barriers.

### Enhanced barriers aliasing not suppressed

`PreCall_ID3D12GraphicsCommandList7_Barrier` (enhanced barriers, `D3D12_BARRIER_TYPE_*`) does not
currently handle aliasing. Enhanced barrier aliasing is a separate type; if a capture uses it with
placed resources in rebind mode, no conversion is performed.

### Replay device sizes vs. capture device sizes

`ProcessDx12ResourceAliasingCommand` queries the replay device's `GetResourceAllocationInfo2` for
texture sizing. If the replay GPU returns larger sizes → safe (over-allocated). If smaller → the
backing allocation may be too small. This is acceptable for the primary use case (same-machine
optimize + replay).

---
