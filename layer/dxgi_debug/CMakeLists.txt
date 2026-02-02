###############################################################################
# Copyright (c) 2026 LunarG, Inc.
# Copyright (c) 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# All rights reserved
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to
# deal in the Software without restriction, including without limitation the
# rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
# sell copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.
#
# Author: LunarG Team
# Author: ARM Tools Team
# Description: CMake script for layer DXGIDebug target
###############################################################################

set(LINK_DXGIDEBUG_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/DXGIDebug_ms.lib)

add_library(DXGIDebug SHARED "")
target_sources(DXGIDebug PRIVATE
                ${CMAKE_CURRENT_LIST_DIR}/dll_main.cpp
                ${CMAKE_CURRENT_LIST_DIR}/hook_dxgi_debug.h
                ${CMAKE_CURRENT_LIST_DIR}/hook_dxgi_debug.cpp
                ${CMAKE_CURRENT_LIST_DIR}/dxgidebug.def)
target_include_directories(DXGIDebug PUBLIC ${CMAKE_BINARY_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(DXGIDebug gfxrecon_encode gfxrecon_util ${LINK_DXGIDEBUG_OUTPUT})
common_build_directives(DXGIDebug)
add_custom_command(TARGET DXGIDebug PRE_BUILD
                    COMMAND lib /def:${CMAKE_CURRENT_LIST_DIR}/dxgidebug_ms.def /out:${LINK_DXGIDEBUG_OUTPUT} /machine:${LINK_ARCH})

install(FILES $<TARGET_FILE_DIR:DXGIDebug>/DXGIDebug.dll DESTINATION ${CMAKE_INSTALL_BINDIR}/d3d12_capture)
