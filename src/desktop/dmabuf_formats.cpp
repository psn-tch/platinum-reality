#include "graphics_integration.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <QLibraryInfo>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <dlfcn.h>
#include <libdrm/drm_fourcc.h>
#include <vector>

// Qt 6.12's linux-dmabuf-v1 compositor integration advertises every format EGL
// can import (supportedDrmFormats), but its scene graph only draws the formats
// its formatFromDrmFormat() maps. For the rest it binds an empty shader and the
// window stays blank. Mesa clients take the first advertised format they
// support; RADV's Vulkan WSI sometimes chose R16G16B16A16_SFLOAT or _UNORM.
// The plugin resolves eglQueryDmaBufFormatsEXT through eglGetProcAddress, which
// this executable exports (CMake: --export-dynamic-symbol), so the compositor
// can advertise only what it can draw. No Qt source or system file changes.
// Audited against qtwayland v6.12.0; see docs/vulkan-qualification.md.

namespace
{
std::atomic_bool filtering{false};
std::atomic<PFNEGLQUERYDMABUFFORMATSEXTPROC> eglFormats{nullptr};

// Exactly the cases of formatFromDrmFormat() in qtwayland v6.12.0
// src/hardwareintegration/compositor/linux-dmabuf-v1/linuxdmabufclientbufferintegration.cpp.
bool drawable(EGLint format)
{
    switch (uint32_t(format))
    {
    case DRM_FORMAT_RGB332: case DRM_FORMAT_BGR233:
    case DRM_FORMAT_XRGB4444: case DRM_FORMAT_XBGR4444: case DRM_FORMAT_RGBX4444: case DRM_FORMAT_BGRX4444:
    case DRM_FORMAT_XRGB1555: case DRM_FORMAT_XBGR1555: case DRM_FORMAT_RGBX5551: case DRM_FORMAT_BGRX5551:
    case DRM_FORMAT_RGB565: case DRM_FORMAT_BGR565: case DRM_FORMAT_RGB888: case DRM_FORMAT_BGR888:
    case DRM_FORMAT_XRGB8888: case DRM_FORMAT_XBGR8888: case DRM_FORMAT_RGBX8888: case DRM_FORMAT_BGRX8888:
    case DRM_FORMAT_XRGB2101010: case DRM_FORMAT_XBGR2101010: case DRM_FORMAT_RGBX1010102: case DRM_FORMAT_BGRX1010102:
    case DRM_FORMAT_ARGB4444: case DRM_FORMAT_ABGR4444: case DRM_FORMAT_RGBA4444: case DRM_FORMAT_BGRA4444:
    case DRM_FORMAT_ARGB1555: case DRM_FORMAT_ABGR1555: case DRM_FORMAT_RGBA5551: case DRM_FORMAT_BGRA5551:
    case DRM_FORMAT_ARGB8888: case DRM_FORMAT_ABGR8888: case DRM_FORMAT_RGBA8888: case DRM_FORMAT_BGRA8888:
    case DRM_FORMAT_ARGB2101010: case DRM_FORMAT_ABGR2101010: case DRM_FORMAT_RGBA1010102: case DRM_FORMAT_BGRA1010102:
    case DRM_FORMAT_YUYV:
        return true;
    default:
        return false;
    }
}

EGLBoolean EGLAPIENTRY drawableDmabufFormats(EGLDisplay display, EGLint maximum, EGLint *formats,
                                             EGLint *count)
{
    const auto query = eglFormats.load();
    EGLint total = 0;
    if (!query || !count || !query(display, 0, nullptr, &total))
        return EGL_FALSE;
    std::vector<EGLint> all(std::max<EGLint>(total, 0));
    if (total > 0 && !query(display, total, all.data(), &total))
        return EGL_FALSE;
    all.resize(std::max<EGLint>(total, 0));
    all.erase(std::remove_if(all.begin(), all.end(), [](EGLint format) { return !drawable(format); }),
              all.end());
    const auto kept = EGLint(all.size());
    // EGL_EXT_image_dma_buf_import_modifiers: max_formats 0 asks for the count.
    if (maximum <= 0 || !formats)
    {
        *count = kept;
        return EGL_TRUE;
    }
    *count = std::min(kept, maximum);
    std::copy_n(all.begin(), *count, formats);
    return EGL_TRUE;
}
} // namespace

extern "C" __eglMustCastToProperFunctionPointerType EGLAPIENTRY eglGetProcAddress(const char *name)
{
    using Lookup = __eglMustCastToProperFunctionPointerType(EGLAPIENTRYP)(const char *);
    static const auto next = reinterpret_cast<Lookup>(dlsym(RTLD_NEXT, "eglGetProcAddress"));
    const auto function = next ? next(name) : nullptr;
    if (!function || !filtering.load() || !name || std::strcmp(name, "eglQueryDmaBufFormatsEXT") != 0)
        return function;
    eglFormats.store(reinterpret_cast<PFNEGLQUERYDMABUFFORMATSEXTPROC>(function));
    return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(&drawableDmabufFormats);
}

void advertiseOnlyDrawableDmabufFormats()
{
    const auto version = QLibraryInfo::version();
    if (version.majorVersion() == 6 && version.minorVersion() == 12)
    {
        filtering.store(true);
        return;
    }
    // A later integration may draw more formats, or still none of these. The
    // native Vulkan format rows decide; audit before extending this gate.
    qWarning("Qt %s: client buffer formats are not filtered (audited for Qt 6.12 only)", qVersion());
}
