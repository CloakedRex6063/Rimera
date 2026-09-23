#import "AppKit/AppKit.h"
#include "window.hpp"

@interface RimeraWindow : NSWindow

@property(nonatomic, assign, getter=is_rimera_closed) BOOL rimera_closed;

@end

@implementation RimeraWindow

- (void)close
{
    self.rimera_closed = YES;
    [super close];
}

@end

namespace
{
    void PrepareApplication()
    {
        static bool prepared = false;
        if (prepared) return;

        NSApplication* application = [NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];
        [application finishLaunching];
        prepared = true;
    }

    glm::uvec2 GetWindowSize(RimeraWindow* window)
    {
        if (window == nil) return {0, 0};

        const NSRect bounds = [[window contentView] bounds];
        return {static_cast<std::uint32_t>(NSWidth(bounds)),
                static_cast<std::uint32_t>(NSHeight(bounds))};
    }
}

Window::Window()
{
    @autoreleasepool
    {
        if (![NSThread isMainThread]) return;

        PrepareApplication();

        const NSRect content_rect = NSMakeRect(0.0, 0.0, m_size.x, m_size.y);
        const NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                       NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        RimeraWindow* window = [[RimeraWindow alloc] initWithContentRect:content_rect
                                                               styleMask:style
                                                                 backing:NSBackingStoreBuffered
                                                                   defer:NO];
        if (window == nil) return;

        [window setReleasedWhenClosed:NO];
        [window setTitle:@"Rimera"];
        [window center];
        [window makeKeyAndOrderFront:nil];
        [[NSApplication sharedApplication] activateIgnoringOtherApps:YES];

        m_window = window;
        m_running = true;
        m_size = GetWindowSize(window);
    }
}

Window::~Window()
{
    if (m_window == nullptr) return;

    @autoreleasepool
    {
        auto* window = static_cast<RimeraWindow*>(m_window);
        if (![window is_rimera_closed]) [window close];
        [window release];
    }
}

void Window::Tick()
{
    const auto previous_size = m_size;
    @autoreleasepool
    {
        auto* window = static_cast<RimeraWindow*>(m_window);
        if (window != nil)
        {
            NSApplication* application = [NSApplication sharedApplication];
            NSEvent* event = nil;
            while ((event = [application nextEventMatchingMask:NSEventMaskAny
                                                     untilDate:[NSDate distantPast]
                                                        inMode:NSDefaultRunLoopMode
                                                       dequeue:YES]) != nil)
            {
                [application sendEvent:event];
            }
            [application updateWindows];
            m_running = ![window is_rimera_closed];
        }
        else
        {
            m_running = false;
        }
        m_size = GetWindowSize(window);
    }
    m_resized = m_size.x != previous_size.x || m_size.y != previous_size.y;
}

bool Window::IsResized() const
{
    return m_resized;
}

void* Window::GetNativeHandle() const
{
    return m_window;
}
