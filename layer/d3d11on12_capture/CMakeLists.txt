###############################################################################
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
# Description: CMake script for the d3d11on12_capture target.
#              Builds d3d11on12_capture.dll, an optional plugin that routes D3D11
#              applications through D3D11On12 so that gfxrecon's D3D12 capture
#              layer can record their API calls.
###############################################################################

add_library(d3d11on12_capture SHARED "")
target_sources(d3d11on12_capture PRIVATE
                ${CMAKE_CURRENT_LIST_DIR}/dll_main.cpp
                ${CMAKE_CURRENT_LIST_DIR}/d3d11on12_proxy.h
                ${CMAKE_CURRENT_LIST_DIR}/d3d11on12_proxy.cpp
                ${CMAKE_CURRENT_LIST_DIR}/d3d11on12_capture.def)
target_include_directories(d3d11on12_capture PUBLIC ${CMAKE_BINARY_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(d3d11on12_capture gfxrecon_encode gfxrecon_util d3d11 d3d12 dxguid)
if(MSVC)
    # Treat source files as UTF-8 regardless of the system code page.
    target_compile_options(d3d11on12_capture PRIVATE /utf-8)
endif()
common_build_directives(d3d11on12_capture)

install(FILES $<TARGET_FILE_DIR:d3d11on12_capture>/d3d11on12_capture.dll
        DESTINATION ${CMAKE_INSTALL_BINDIR}/d3d12_capture)
