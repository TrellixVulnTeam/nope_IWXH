//
// Copyright (c) 2014 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// DisplayD3D.cpp: D3D implementation of egl::Display

#include "libANGLE/renderer/d3d/DisplayD3D.h"

#include "libANGLE/Context.h"
#include "libANGLE/Config.h"
#include "libANGLE/Display.h"
#include "libANGLE/Surface.h"
#include "libANGLE/renderer/d3d/RendererD3D.h"
#include "libANGLE/renderer/d3d/SurfaceD3D.h"
#include "libANGLE/renderer/d3d/SwapChainD3D.h"
#include "platform/Platform.h"

#include <EGL/eglext.h>

#if defined (ANGLE_ENABLE_D3D9)
#   include "libANGLE/renderer/d3d/d3d9/Renderer9.h"
#endif // ANGLE_ENABLE_D3D9

#if defined (ANGLE_ENABLE_D3D11)
#   include "libANGLE/renderer/d3d/d3d11/Renderer11.h"
#endif // ANGLE_ENABLE_D3D11

#if defined (ANGLE_TEST_CONFIG)
#   define ANGLE_DEFAULT_D3D11 1
#endif

#if !defined(ANGLE_DEFAULT_D3D11)
// Enables use of the Direct3D 11 API for a default display, when available
#   define ANGLE_DEFAULT_D3D11 0
#endif

namespace rx
{

typedef RendererD3D *(*CreateRendererD3DFunction)(egl::Display*);

template <typename RendererType>
static RendererD3D *CreateTypedRendererD3D(egl::Display *display)
{
    return new RendererType(display);
}

egl::Error CreateRendererD3D(egl::Display *display, RendererD3D **outRenderer)
{
    ASSERT(outRenderer != nullptr);

    std::vector<CreateRendererD3DFunction> rendererCreationFunctions;

    const auto &attribMap = display->getAttributeMap();
    EGLNativeDisplayType nativeDisplay = display->getNativeDisplayId();

    EGLint requestedDisplayType = attribMap.get(EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_DEFAULT_ANGLE);

#   if defined(ANGLE_ENABLE_D3D11)
        if (nativeDisplay == EGL_D3D11_ELSE_D3D9_DISPLAY_ANGLE ||
            nativeDisplay == EGL_D3D11_ONLY_DISPLAY_ANGLE ||
            requestedDisplayType == EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE)
        {
            rendererCreationFunctions.push_back(CreateTypedRendererD3D<Renderer11>);
        }
#   endif

#   if defined(ANGLE_ENABLE_D3D9)
        if (nativeDisplay == EGL_D3D11_ELSE_D3D9_DISPLAY_ANGLE ||
            requestedDisplayType == EGL_PLATFORM_ANGLE_TYPE_D3D9_ANGLE)
        {
            rendererCreationFunctions.push_back(CreateTypedRendererD3D<Renderer9>);
        }
#   endif

    if (nativeDisplay != EGL_D3D11_ELSE_D3D9_DISPLAY_ANGLE &&
        nativeDisplay != EGL_D3D11_ONLY_DISPLAY_ANGLE &&
        requestedDisplayType == EGL_PLATFORM_ANGLE_TYPE_DEFAULT_ANGLE)
    {
        // The default display is requested, try the D3D9 and D3D11 renderers, order them using
        // the definition of ANGLE_DEFAULT_D3D11
#       if ANGLE_DEFAULT_D3D11
#           if defined(ANGLE_ENABLE_D3D11)
                rendererCreationFunctions.push_back(CreateTypedRendererD3D<Renderer11>);
#           endif
#           if defined(ANGLE_ENABLE_D3D9)
                rendererCreationFunctions.push_back(CreateTypedRendererD3D<Renderer9>);
#           endif
#       else
#           if defined(ANGLE_ENABLE_D3D9)
                rendererCreationFunctions.push_back(CreateTypedRendererD3D<Renderer9>);
#           endif
#           if defined(ANGLE_ENABLE_D3D11)
                rendererCreationFunctions.push_back(CreateTypedRendererD3D<Renderer11>);
#           endif
#       endif
    }

    egl::Error result(EGL_NOT_INITIALIZED, "No available renderers.");
    for (size_t i = 0; i < rendererCreationFunctions.size(); i++)
    {
        RendererD3D *renderer = rendererCreationFunctions[i](display);
        result = renderer->initialize();

        if (renderer->getRendererClass() == RENDERER_D3D11)
        {
            ASSERT(result.getID() >= 0 && result.getID() < NUM_D3D11_INIT_ERRORS);

            angle::Platform *platform = angle::Platform::current();
            platform->histogramEnumeration("GPU.ANGLE.D3D11InitializeResult",
                                           result.getID(), NUM_D3D11_INIT_ERRORS);
        }
        else
        {
            ASSERT(renderer->getRendererClass() == RENDERER_D3D9);
            ASSERT(result.getID() >= 0 && result.getID() < NUM_D3D9_INIT_ERRORS);

            angle::Platform *platform = angle::Platform::current();
            platform->histogramEnumeration("GPU.ANGLE.D3D9InitializeResult",
                                           result.getID(), NUM_D3D9_INIT_ERRORS);
        }

        if (!result.isError())
        {
            *outRenderer = renderer;
            break;
        }
        else
        {
            // Failed to create the renderer, try the next
            SafeDelete(renderer);
        }
    }

    return result;
}

DisplayD3D::DisplayD3D()
    : mRenderer(nullptr)
{
}

egl::Error DisplayD3D::createWindowSurface(const egl::Config *configuration, EGLNativeWindowType window,
                                           const egl::AttributeMap &attribs, SurfaceImpl **outSurface)
{
    ASSERT(mRenderer != nullptr);

    EGLint postSubBufferSupported = attribs.get(EGL_POST_SUB_BUFFER_SUPPORTED_NV, EGL_FALSE);
    EGLint width = attribs.get(EGL_WIDTH, 0);
    EGLint height = attribs.get(EGL_HEIGHT, 0);
    EGLint fixedSize = attribs.get(EGL_FIXED_SIZE_ANGLE, EGL_FALSE);

    if (!fixedSize)
    {
        width = -1;
        height = -1;
    }

    SurfaceD3D *surface = SurfaceD3D::createFromWindow(mRenderer, mDisplay, configuration, window, fixedSize,
                                                       width, height, postSubBufferSupported);
    egl::Error error = surface->initialize();
    if (error.isError())
    {
        SafeDelete(surface);
        return error;
    }

    *outSurface = surface;
    return egl::Error(EGL_SUCCESS);
}

egl::Error DisplayD3D::createPbufferSurface(const egl::Config *configuration, const egl::AttributeMap &attribs,
                                            SurfaceImpl **outSurface)
{
    ASSERT(mRenderer != nullptr);

    EGLint width = attribs.get(EGL_WIDTH, 0);
    EGLint height = attribs.get(EGL_HEIGHT, 0);
    EGLenum textureFormat = attribs.get(EGL_TEXTURE_FORMAT, EGL_NO_TEXTURE);
    EGLenum textureTarget = attribs.get(EGL_TEXTURE_TARGET, EGL_NO_TEXTURE);
    
    SurfaceD3D *surface = SurfaceD3D::createOffscreen(mRenderer, mDisplay, configuration, NULL,
                                                      width, height, textureFormat, textureTarget);
    egl::Error error = surface->initialize();
    if (error.isError())
    {
        SafeDelete(surface);
        return error;
    }

    *outSurface = surface;
    return egl::Error(EGL_SUCCESS);
}

egl::Error DisplayD3D::createPbufferFromClientBuffer(const egl::Config *configuration, EGLClientBuffer shareHandle,
                                                     const egl::AttributeMap &attribs, SurfaceImpl **outSurface)
{
    ASSERT(mRenderer != nullptr);

    EGLint width = attribs.get(EGL_WIDTH, 0);
    EGLint height = attribs.get(EGL_HEIGHT, 0);
    EGLenum textureFormat = attribs.get(EGL_TEXTURE_FORMAT, EGL_NO_TEXTURE);
    EGLenum textureTarget = attribs.get(EGL_TEXTURE_TARGET, EGL_NO_TEXTURE);

    SurfaceD3D *surface = SurfaceD3D::createOffscreen(mRenderer, mDisplay, configuration, shareHandle,
                                                      width, height, textureFormat, textureTarget);
    egl::Error error = surface->initialize();
    if (error.isError())
    {
        SafeDelete(surface);
        return error;
    }

    *outSurface = surface;
    return egl::Error(EGL_SUCCESS);
}

egl::Error DisplayD3D::createContext(const egl::Config *config, const gl::Context *shareContext, const egl::AttributeMap &attribs,
                                     gl::Context **outContext)
{
    ASSERT(mRenderer != nullptr);

    EGLint clientVersion = attribs.get(EGL_CONTEXT_CLIENT_VERSION, 1);
    bool notifyResets = (attribs.get(EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT, EGL_NO_RESET_NOTIFICATION_EXT) == EGL_LOSE_CONTEXT_ON_RESET_EXT);
    bool robustAccess = (attribs.get(EGL_CONTEXT_OPENGL_ROBUST_ACCESS_EXT, EGL_FALSE) == EGL_TRUE);

    *outContext = new gl::Context(config, clientVersion, shareContext, mRenderer, notifyResets, robustAccess);
    return egl::Error(EGL_SUCCESS);
}

egl::Error DisplayD3D::makeCurrent(egl::Surface *drawSurface, egl::Surface *readSurface, gl::Context *context)
{
    return egl::Error(EGL_SUCCESS);
}

egl::Error DisplayD3D::initialize(egl::Display *display)
{
    ASSERT(mRenderer == nullptr && display != nullptr);
    mDisplay = display;
    return CreateRendererD3D(display, &mRenderer);
}

void DisplayD3D::terminate()
{
    SafeDelete(mRenderer);
}

egl::ConfigSet DisplayD3D::generateConfigs() const
{
    ASSERT(mRenderer != nullptr);
    return mRenderer->generateConfigs();
}

bool DisplayD3D::isDeviceLost() const
{
    ASSERT(mRenderer != nullptr);
    return mRenderer->isDeviceLost();
}

bool DisplayD3D::testDeviceLost()
{
    ASSERT(mRenderer != nullptr);
    return mRenderer->testDeviceLost();
}

egl::Error DisplayD3D::restoreLostDevice()
{
    // Release surface resources to make the Reset() succeed
    for (auto &surface : mSurfaceSet)
    {
        if (surface->getBoundTexture())
        {
            surface->releaseTexImage(EGL_BACK_BUFFER);
        }
        SurfaceD3D *surfaceD3D = GetImplAs<SurfaceD3D>(surface);
        surfaceD3D->releaseSwapChain();
    }

    if (!mRenderer->resetDevice())
    {
        return egl::Error(EGL_BAD_ALLOC);
    }

    // Restore any surfaces that may have been lost
    for (const auto &surface : mSurfaceSet)
    {
        SurfaceD3D *surfaceD3D = GetImplAs<SurfaceD3D>(surface);

        egl::Error error = surfaceD3D->resetSwapChain();
        if (error.isError())
        {
            return error;
        }
    }

    return egl::Error(EGL_SUCCESS);
}

bool DisplayD3D::isValidNativeWindow(EGLNativeWindowType window) const
{
    return NativeWindow::isValidNativeWindow(window);
}

void DisplayD3D::generateExtensions(egl::DisplayExtensions *outExtensions) const
{
    outExtensions->createContextRobustness = true;

    // ANGLE-specific extensions
    if (mRenderer->getShareHandleSupport())
    {
        outExtensions->d3dShareHandleClientBuffer = true;
        outExtensions->surfaceD3DTexture2DShareHandle = true;
    }

    outExtensions->querySurfacePointer = true;
    outExtensions->windowFixedSize = true;

    if (mRenderer->getPostSubBufferSupport())
    {
        outExtensions->postSubBuffer = true;
    }

    outExtensions->createContext = true;
}

std::string DisplayD3D::getVendorString() const
{
    std::string vendorString = "Google Inc.";
    if (mRenderer)
    {
        vendorString += " " + mRenderer->getVendorString();
    }

    return vendorString;
}

void DisplayD3D::generateCaps(egl::Caps *outCaps) const
{
    // Display must be initialized to generate caps
    ASSERT(mRenderer != nullptr);

    outCaps->textureNPOT = mRenderer->getRendererExtensions().textureNPOT;
}

}
