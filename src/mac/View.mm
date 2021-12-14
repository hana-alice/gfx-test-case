/****************************************************************************
 Copyright (c) 2020 Xiamen Yaji Software Co., Ltd.

 http://www.cocos2d-x.org

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 ****************************************************************************/
#import "View.h"
#import "tests/TestBase.h"

#if CC_PLATFORM == CC_PLATFORM_MAC_OSX
    #import <AppKit/AppKit.h>
#endif
#if CC_PLATFORM == CC_PLATFORM_MAC_IOS
    #import <UIKit/UIKit.h>
#endif
#import <AppKit/NSTouch.h>
#import <AppKit/NSEvent.h>
#import <QuartzCore/QuartzCore.h>
#import <AppKit/NSWindow.h>
#import <ctime>
#import "../../../engine-native/cocos/renderer/gfx-base/GFXSwapchain.h"
#import "../../../engine-native/cocos/renderer/gfx-base/GFXDevice.h"
#import "../../../engine-native/cocos/renderer/gfx-base/GFXFramebuffer.h"
#import "../../../engine-native/cocos/renderer/gfx-base/GFXRenderPass.h"
#import "../../../engine-native/cocos/renderer/GFXDeviceManager.h"

using namespace cc::gfx;

namespace
{
    cc::WindowInfo g_windowInfo;
}

@implementation View {
    IOSurfaceRef _surface;
    Swapchain* _swapchain;
    Device* _ccdevice;
    Framebuffer* _frameBuffer;
    RenderPass* _renderPass;
    Texture* _targetTex;
}


- (CALayer *)makeBackingLayer
{
    return [CAMetalLayer layer];
}

- (instancetype)initWithFrame:(NSRect)frameRect {
    if (self = [super initWithFrame:frameRect]) {
        [self.window makeFirstResponder:self];

        int pixelRatio = 1;
#if CC_PLATFORM == CC_PLATFORM_MAC_OSX
        pixelRatio = [[NSScreen mainScreen] backingScaleFactor];
#else
        pixelRatio = [[UIScreen mainScreen] scale];
#endif //CC_PLATFORM == CC_PLATFORM_MAC_OSX
        CGSize size = CGSizeMake(frameRect.size.width * pixelRatio, frameRect.size.height * pixelRatio);

        // Create CAMetalLayer
        self.wantsLayer = YES;
        // Config metal layer
        CAMetalLayer *layer = (CAMetalLayer*)self.layer;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.device = self.device = MTLCreateSystemDefaultDevice();
        layer.drawableSize = size;
        self.depthStencilPixelFormat = MTLPixelFormatDepth24Unorm_Stencil8;

#if CC_USE_METAL
        g_windowInfo.windowHandle = self;
#else
        g_windowInfo.windowHandle = layer;
#endif

        g_windowInfo.screen.x = frameRect.origin.x;
        g_windowInfo.screen.y = frameRect.origin.y;
        g_windowInfo.screen.width = size.width;
        g_windowInfo.screen.height = size.height;
        g_windowInfo.pixelRatio = pixelRatio;

        // Start main loop
        self.timer = [NSTimer scheduledTimerWithTimeInterval:1.0f / 60
                                                 target:self
                                               selector:@selector(tick)
                                               userInfo:nil
                                                repeats:YES];

        cc::TestBaseI::setWindowInfo(g_windowInfo);
        cc::TestBaseI::nextTest();
        
        NSDictionary* dict = [NSDictionary dictionaryWithObjectsAndKeys:
                                 [NSNumber numberWithInt:size.width], kIOSurfaceWidth,
                                 [NSNumber numberWithInt:size.height], kIOSurfaceHeight,
                                 [NSNumber numberWithInt:4], kIOSurfaceBytesPerElement,
                                 nil];

        _surface = IOSurfaceCreate((CFDictionaryRef)dict);
        
        DeviceInfo deviceInfo;
        _ccdevice = DeviceManager::create(deviceInfo);
        
        _ccdevice = Device::getInstance();
        
        SwapchainInfo swapchainInfo = {
            .width = (uint32_t)size.width,
            .height = (uint32_t)size.height,
            .vsyncMode = VsyncMode::RELAXED,
            .windowHandle = self,
        };
        _swapchain = _ccdevice->createSwapchain(swapchainInfo);
        
        ColorAttachment color = {
            .format = Format::BGRA8,
            .loadOp = LoadOp::CLEAR,
            .storeOp = StoreOp::STORE,
        };
        
        RenderPassInfo rpInfo = {
            .dependencies = {},
            .subpasses = {},
            .colorAttachments = {color},
        };
        
        _renderPass = _ccdevice->createRenderPass(rpInfo);
        
        TextureInfo texInfo = {
            .type = TextureType::TEX2D,
            .usage = TextureUsage::COLOR_ATTACHMENT | TextureUsage::SAMPLED,
            .format = Format::BGRA8,
            .width = (uint32_t)size.width,
            .height = (uint32_t)size.height,
            .externalRes = _surface,
        };
        
        _targetTex = _ccdevice->createTexture(texInfo);
        
        FramebufferInfo fbInfo = {
            .renderPass = _renderPass,
            .colorTextures = {_targetTex},
        };

        _frameBuffer = _ccdevice->createFramebuffer(fbInfo);
        
    }
    return self;
}

- (void)setFrameSize:(NSSize)newSize {
    CAMetalLayer *layer = (CAMetalLayer *)self.layer;

    CGSize nativeSize = [self convertSizeToBacking:newSize];
    [super setFrameSize:newSize];
    layer.drawableSize = nativeSize;
    [self viewDidChangeBackingProperties];

    // Add tracking area to receive mouse move events.
    NSRect          rect         = {0, 0, nativeSize.width, nativeSize.height};
    NSTrackingArea *trackingArea = [[NSTrackingArea alloc] initWithRect:rect
                                                                options:(NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingActiveInKeyWindow)
                                                                  owner:self
                                                               userInfo:nil];
    [self addTrackingArea:trackingArea];

    void* windowHandle = nullptr;
#if CC_USE_METAL
    windowHandle = self;
#else
    windowHandle = layer;
#endif
    cc::TestBaseI::resizeGlobal(windowHandle, nativeSize.width, nativeSize.height, cc::gfx::SurfaceTransform::IDENTITY);
}

- (void)viewDidChangeBackingProperties {
    [super viewDidChangeBackingProperties];
    CAMetalLayer *layer = (CAMetalLayer *)self.layer;
    layer.contentsScale = self.window.backingScaleFactor;
}

- (void)keyUp:(NSEvent *)event {
    if (event.keyCode == 49) { // space
        cc::TestBaseI::spacePressed();
    }
}

- (void)tick {
//    IOSurfaceLock(_surface, 0, NULL);
    Swapchain* scs[1];
    scs[0] = _swapchain;
    _ccdevice->acquire(scs, 1);
    
    Color clearColor;
    clearColor.x = 1.0F;
    clearColor.y = std::abs(std::sin(std::time(nullptr)));
    clearColor.z = 0.0F;
    clearColor.w = 1.0F;

    cc::gfx::Rect renderArea = {0, 0, _swapchain->getWidth(), _swapchain->getHeight()};

    auto *commandBuffer = _ccdevice->getCommandBuffer();
    commandBuffer->begin();
    commandBuffer->beginRenderPass(_frameBuffer->getRenderPass(), _frameBuffer, renderArea, &clearColor, 1.0F, 0);
    commandBuffer->endRenderPass();
    
    Extent srcExtent = {
        .width = _swapchain->getWidth(),
        .height = _swapchain->getHeight(),
        .depth = 1,
    };
    
    Offset srcOffset = {0, 0, 0};
    
    TextureSubresLayers layers = {
        .baseArrayLayer = 0,
        .layerCount = 1,
        .mipLevel = 0,
    };
    
    TextureBlit blitRegion = {
        .srcExtent = srcExtent,
        .dstExtent = srcExtent,
        .srcOffset = srcOffset,
        .dstOffset = srcOffset,
        .srcSubres = layers,
        .dstSubres = layers,
    };
    
    commandBuffer->blitTexture(_targetTex, _swapchain->getColorTexture(), {blitRegion}, Filter::LINEAR);
    commandBuffer->end();

    _ccdevice->flushCommands({commandBuffer});
    _ccdevice->getQueue()->submit({commandBuffer});
    
    _ccdevice->present();
    
//    void* data = IOSurfaceGetBaseAddress(_surface);
//    size_t stride = IOSurfaceGetBytesPerRow(_surface);
//
//    CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
//    CGContextRef imgCtx = CGBitmapContextCreate(data, size.width, size.height, 8, stride,
//                                                rgb, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
//    CGColorSpaceRelease(rgb);
//
//    cc::TestBaseI::update();
}

- (void)mouseUp:(NSEvent *)event {
    cc::TestBaseI::onTouchEnd();
}

- (void)rightMouseUp:(NSEvent *)event {
    cc::TestBaseI::onTouchEnd();
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (void)viewDidAppear
{
    // Make the view controller the window's first responder so that it can handle the Key events
    [self.window makeFirstResponder:self];
}

- (void)onTerminate
{
    cc::TestBaseI::destroyGlobal();
}

@end
