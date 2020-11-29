//
//  AppDelegate.m
//  ios-test
//
//  Created by minggo on 2019/12/5.
//  Copyright © 2019 minggo. All rights reserved.
//

#import "AppDelegate.h"
#import "ViewController.h"
#import "View.h"
#include "tests/StressTest.h"
#include "tests/ClearScreenTest.h"
#include "tests/BasicTriangleTest.h"
#include "tests/BasicTextureTest.h"
#include "tests/DepthTest.h"
#include "tests/StencilTest.h"
#include "tests/BlendTest.h"
#include "tests/ParticleTest.h"
#include "tests/BunnyTest.h"

using namespace cc;

namespace
{
    int g_nextTextIndex = 0;
    using createFunc = TestBaseI * (*)(const WindowInfo& info);
    std::vector<createFunc> g_tests;
    TestBaseI* g_test    = nullptr;
    WindowInfo g_windowInfo;
}

@interface AppDelegate ()

@end

@implementation AppDelegate

@synthesize window;

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    // Override point for customization after application launch.

    CGRect rect = [[UIScreen mainScreen] bounds];
    self.window = [[UIWindow alloc] initWithFrame: rect];
    ViewController* viewController = [[ViewController alloc] init];

    UIView* view = [[View alloc] initWithFrame: rect];
    viewController.view = view;

    [self initWindowInfo: view size:rect.size];
    [self initTests];

#ifndef USE_METAL
    [self run];
#endif

    [self.window setRootViewController:viewController];
    [self.window makeKeyAndVisible];

    return YES;
}

- (void)initWindowInfo:(UIView*)view size:(CGSize)size {
    g_windowInfo.windowHandle = (intptr_t)(view);

    float scale = 1.0f;
    if ( [view respondsToSelector:@selector(setContentScaleFactor:)] )
    {
        scale = [[UIScreen mainScreen] scale];
        view.contentScaleFactor = scale;
    }

    g_windowInfo.screen.x = 0;
    g_windowInfo.screen.y = 0;
    g_windowInfo.screen.width = size.width * scale;
    g_windowInfo.screen.height = size.height * scale;

    g_windowInfo.physicalHeight = g_windowInfo.screen.height;
    g_windowInfo.physicalWidth = g_windowInfo.screen.width;
}

- (void)initTests {
    static bool first = true;
    if (first)
    {
        g_tests = {
            StressTest::create,
            ClearScreen::create,
            BasicTriangle::create,
            BasicTexture::create,
            DepthTexture::create,
            StencilTest::create,
            BlendTest::create,
            ParticleTest::create,
            BunnyTest::create,
        };
        g_test = g_tests[g_nextTextIndex](g_windowInfo);
        if (g_test == nullptr)
            return;
        first = false;
    }
}

- (void)run {
    CADisplayLink* displayLink = [NSClassFromString(@"CADisplayLink") displayLinkWithTarget:self selector:@selector(loop:)];
    displayLink.preferredFramesPerSecond = 60;
    [displayLink addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
}

-(void)loop:(id)sender {
    g_test->tick();
}

- (void)touchesEnded:(NSSet *)touches withEvent:(UIEvent *)event
{
    /* *
    g_nextTextIndex = (++g_nextTextIndex) % g_tests.size();
    CC_SAFE_DESTROY(g_test);
    g_test = g_tests[g_nextTextIndex](g_windowInfo);
    /* */
    TestBaseI::toggleMultithread();
    /* */
}


@end
