Vulkan bindless support in gfxreconstruct-arm
=============================================

"Bindless" refers not just to a single thing in Vulkan, but rather refers to a
whole family of concepts and extensions that remove the need to explicitly bind
resources to the graphics context ahead of GPU execution.

There are two main approaches to bindless:
- Opaque descriptors in memory, introduced first in [VK_EXT_descriptor_buffer](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_descriptor_buffer.html),
  these are opaque handles that refer to GPU resources that can be copied around
  in memory, then used on the fly from a shader.
- Device addresses. Using these you can read and write directly into any buffer
  from a shader without first having to bind it. This was introduced first in
  [VK_KHR_buffer_device_address](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_buffer_device_address.html)
  then promoted to core as optional in Vulkan 1.2 and required in Vulkan 1.3.

Tooling support
---------------

The traditional Khronos approach to support tooling is through the use of
`CaptureReplay` or `OpaqueCaptureAddress` type commands that return addresses
that are guaranteed to be restorable on the same location as long as the replay
is done on the same platform with the same driver. This only works for the
debugging use case, while for longer term regression testing it does not work.
For this, we need a portable solution.

LunarG's [answer](https://www.lunarg.com/wp-content/uploads/2025/02/GFXR-Portable-Raytracing-FINAL-PUBLISHED.pdf) to this is to detect and remap addresses and resources on the
fly during replay, including having compute shaders replacing addresses while
a pipeline is running on the GPU. This has performance overhead, but more
importantly, it will not work for the most complex use cases. For these we
believe we need to do analysis during post-processing in order to understand
the usage pattern and find out which actions to take during replay to make
the trace portable.

The downside to this approach is that tracing goes from a two-step process
(trace then replay) to a three step process (trace then post-process then
replay), so it is no longer as convenient. We try to compensate for this
using our [TraceUI](https://github.com/ARM-software/traceui/) capture
automation software.

Post-processing design
----------------------

Our post-processing approach goes through three to five steps using a
tool that parses the trace file but passes nothing to the GPU:
1. We do an initial pass through the trace file looking for device address
   candidates. We do this by registering any addresses fetched through
   `vkGetBufferDeviceAddress()` or `vkGetAccelerationStructureDeviceAddressKHR()`
   and looking for such addresses in all memory updates coming from the
   host side. These will inevitably be a mix of true addresses and false
   positives. Each candidate is tracked together with information about
   their location in the trace file.
2. We run the spirv-simulator whenever a shader is executed (more details
   below). It will look at the address candidates and figure out which of
   them are actually consumed by the shader code. It will also check if
   there are any other unportable data types being consumed such as
   opaque descriptor sizes and offsets, and if any such are detected, we
   inform the tool of these extra candidates, which will have to do one
   more pass.
3. Extra pass if we noticed more candidates during the SPIRV processing.
   Just like in step 1, but only looking for the extra candidates.
4. Extra pass through the SPIRV-simulator just checking the extra
   candidates.
5. Finally, we have a definitive list of verified memory changes that we
   need to do. We push these onto what we call the 'rewrite queue'. For
   our last step, we go through the trace file one more time, this time
   storing a marking at any verified location.

Now our trace file is portable. On replay, the replayer will know as soon
as it reaches a marked command what to do, and remaps the platform-specific
capture platform data with information from the replay platform.

SPIRV processing
----------------

CPU emulation of SPIRV shaders is extremely slow, so we employ a number
of techniques to speed this up:
- We attempt to run each draw or dispatch only as a single shader
  invocation for each shader stage. If we encounter branches that would
  diverge between invocations, we try to evaluate all these branches at
  once. If this fails, we fall back to running all invocations.
  E.g. we try to run only a single invocation of the fragment shader for
  a draw call, treating all pixels the same.
- We ignore and try to optimize away all floating point inputs and outputs.
  These are extremely unlikely to contain pointer values, opaque offsets
  or opaque resource sizes.

Detailed information about the simulator can be found [here](https://github.com/ARM-software/spirv-simulator/blob/main/README.md).

Descriptor buffers
------------------

The design of the `VK_EXT_descriptor_buffer` extension is particularly
bad for portable tracing. As the [official Khronos blog](https://www.khronos.org/blog/vk-ext-descriptor-buffer) put it:

| While this is a ridiculously powerful feature, it’s also an equally ridiculous
| foot-gun. The requirements on debug infrastructure are extreme. Be warned!

The main problem here is that we need to deal with the copy of opaque resources
from CPU to GPU memory, and between different memory on the GPU side, and these
resources not only have no fixed size, their sizes cannot be queried on the GPU
side and have been be supplied from the host side as some variable in memory.
This means we 'burn in' these sizes and any offsets using such sizes into the
trace, breaking these traces if we run on another platform where the sizes
change. Identifying these in the captured memory is even more difficult as they
tend to be small, regular numbers rather than pointer values, so we cannot
simply scan for any value identical to a queried size parameter and assume this
to be a candidate. For offsets, we would not even know what to scan for.

We fix this by issuing searches for such sizes and offsets from the point where
they are consumed, as explained above, now with much more knowledge of about
where to look. If sizes increased, we must also rebind the opaque resources in
memory.

Links
-----
* [Spirv physical storage buffer extension](https://github.com/KhronosGroup/SPIRV-Registry/blob/main/extensions/KHR/SPV_KHR_physical_storage_buffer.asciidoc)
* [Buffer device address extension](https://docs.vulkan.org/samples/latest/samples/extensions/buffer_device_address/README.html)
* [Worst case test for device addresses](https://github.com/ARM-software/tracetooltests/blob/main/src/vulkan_compute_bda_copying_address.cpp)
