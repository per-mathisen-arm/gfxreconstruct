---
name: validate_extension
description: Validate the implementation and support in gfxreconstruct for a given Vulkan extension
metadata:
  short-description: Validate gfxreconstruct's support for a Vulkan extension
---

This is for review only - make no fixes to code unless explicitly told to do so.

1. Check that we were told which Vulkan extension to validate. If not, ask.
2. Download the extension text from `https://docs.vulkan.org/refpages/latest/refpages/source/<extension name>.html` and read it. Focus on the "New Commands", "New Structures", "New Enum Constants", "Promotion to", and feature/property language.
3. For each new Vulkan command defined by the extension, inspect the capture path first:
   - Look for the generated entry point in `framework/generated/generated_vulkan_api_call_encoders.h` and `framework/generated/generated_vulkan_api_call_encoders.cpp`.
   - If the command is not handled purely by generated code, check `framework/encode/custom_vulkan_api_call_encoders.h`, `framework/encode/custom_vulkan_api_call_encoders.cpp`, `framework/encode/vulkan_capture_manager.h`, `framework/encode/vulkan_capture_manager.cpp`, and `layer/layer_vulkan_entry.cpp`.
   - If the command is missing from generated code entirely, check whether it is intentionally excluded or overridden in `framework/generated/khronos_generators/vulkan_generators/blacklists.json`, `framework/generated/khronos_generators/vulkan_generators/capture_overrides.json`, and `framework/generated/khronos_generators/vulkan_generators/replay_overrides.json`.
4. For each new Vulkan command, inspect the decode/replay path and compare it to capture:
   - Check decode dispatch in `framework/generated/generated_vulkan_decoder.cpp`.
   - Check replay consumers in `framework/generated/generated_vulkan_replay_consumer.h` and `framework/generated/generated_vulkan_replay_consumer.cpp`.
   - If the command has custom replay handling, inspect `framework/decode/vulkan_replay_consumer_base.cpp` and any relevant specialized consumers such as `framework/decode/vulkan_pre_process_consumer.h`, `framework/decode/vulkan_resource_tracking_consumer.h`, and `framework/decode/vulkan_resource_tracking_consumer.cpp`.
   - Verify that parameter order, counts, handle IDs, pNext data, return values, and create/destroy behavior are consistent between capture and decode/replay.
5. For each new Vulkan structure or pNext type defined by the extension:
   - Check generated encoding in `framework/generated/generated_vulkan_struct_encoders.h` and `framework/generated/generated_vulkan_struct_encoders.cpp`.
   - Check custom encoding in `framework/encode/custom_vulkan_struct_encoders.h` and `framework/encode/custom_vulkan_struct_encoders.cpp`.
   - Check generated decoding in `framework/generated/generated_vulkan_struct_decoders.h` and `framework/generated/generated_vulkan_struct_decoders.cpp`.
   - Check custom decoding in `framework/decode/custom_vulkan_struct_decoders.h` and `framework/decode/custom_vulkan_struct_decoders.cpp`.
   - Check pNext dispatch in `framework/generated/generated_vulkan_pnext_struct_encoder.cpp` and `framework/generated/generated_vulkan_pnext_struct_decoder.cpp`.
6. If a new structure contains Vulkan handles or nested structs with handles, also verify handle wrapping and remapping support:
   - Check `framework/generated/generated_vulkan_struct_handle_wrappers.h` and `framework/generated/generated_vulkan_struct_handle_wrappers.cpp`.
   - Check `framework/generated/generated_vulkan_struct_handle_mappers.cpp`.
   - Check `framework/encode/custom_vulkan_struct_handle_wrappers.h`, `framework/encode/custom_vulkan_struct_handle_wrappers.cpp`, `framework/decode/custom_vulkan_struct_handle_mappers.h`, and `framework/decode/custom_vulkan_struct_handle_mappers.cpp` when the generated path is not enough.
7. If the extension defines new handle/object types, or adds important metadata to object creation:
   - Check capture-side wrappers and state tracking in `framework/encode/vulkan_handle_wrappers.h`, `framework/generated/generated_vulkan_state_table.h`, `framework/encode/vulkan_state_tracker.h`, `framework/encode/vulkan_state_tracker.cpp`, `framework/generated/generated_vulkan_struct_trackers.h`, and `framework/generated/generated_vulkan_struct_trackers.cpp`.
   - Check replay-side object tracking in `framework/generated/generated_vulkan_object_info_table_base2.h`, `framework/decode/vulkan_tracked_object_info.h`, `framework/decode/vulkan_tracked_object_info_table.h`, `framework/decode/vulkan_resource_tracking_consumer.h`, and `framework/decode/vulkan_resource_tracking_consumer.cpp`.
   - Verify that any metadata needed for state restore, trimming, resource tracking, or replay remapping is present.
8. If the extension introduces feature/property/query structs that affect replay capability handling, check `framework/generated/generated_vulkan_feature_util.cpp`, `framework/decode/vulkan_feature_tracker_consumer_base.h`, and `framework/decode/vulkan_feature_tracker_consumer_base.cpp` for corresponding tracking or compatibility logic.
9. If the extension has been promoted to core, verify that both the extension-suffixed and core variants are handled consistently across capture, decode, replay, and any custom/manual code.
   - Warn if one path exists without the other.
   - Ignore issues that might be caused by the user explicitly removing an extension during replay that was used during capture.
   - Promotion-safety issues are minor and only worth a brief mention.
