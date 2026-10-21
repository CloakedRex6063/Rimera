#import "AppKit/AppKit.h"
#include "window.hpp"
#include "optional"

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

@interface RimeraApplicationDelegate : NSObject <NSApplicationDelegate>

@end

@implementation RimeraApplicationDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
    NSEvent* event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                        location:NSMakePoint(0.0, 0.0)
                                   modifierFlags:0
                                       timestamp:0
                                    windowNumber:0
                                         context:nil
                                         subtype:0
                                           data1:0
                                           data2:0];
    [NSApp postEvent:event atStart:YES];
    [NSApp stop:nil];
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
        static RimeraApplicationDelegate* delegate = [[RimeraApplicationDelegate alloc] init];
        [application setDelegate:delegate];
        if (![[NSRunningApplication currentApplication] isFinishedLaunching]) [application run];
        prepared = true;
    }

    std::optional<Key> ToKey(unsigned short key_code)
    {
        switch (key_code)
        {
            case 0x00:
                return Key::A;
            case 0x0B:
                return Key::B;
            case 0x08:
                return Key::C;
            case 0x02:
                return Key::D;
            case 0x0E:
                return Key::E;
            case 0x03:
                return Key::F;
            case 0x05:
                return Key::G;
            case 0x04:
                return Key::H;
            case 0x22:
                return Key::I;
            case 0x26:
                return Key::J;
            case 0x28:
                return Key::K;
            case 0x25:
                return Key::L;
            case 0x2E:
                return Key::M;
            case 0x2D:
                return Key::N;
            case 0x1F:
                return Key::O;
            case 0x23:
                return Key::P;
            case 0x0C:
                return Key::Q;
            case 0x0F:
                return Key::R;
            case 0x01:
                return Key::S;
            case 0x11:
                return Key::T;
            case 0x20:
                return Key::U;
            case 0x09:
                return Key::V;
            case 0x0D:
                return Key::W;
            case 0x07:
                return Key::X;
            case 0x10:
                return Key::Y;
            case 0x06:
                return Key::Z;
            case 0x1D:
                return Key::Num0;
            case 0x12:
                return Key::Num1;
            case 0x13:
                return Key::Num2;
            case 0x14:
                return Key::Num3;
            case 0x15:
                return Key::Num4;
            case 0x17:
                return Key::Num5;
            case 0x16:
                return Key::Num6;
            case 0x1A:
                return Key::Num7;
            case 0x1C:
                return Key::Num8;
            case 0x19:
                return Key::Num9;
            case 0x31:
                return Key::Space;
            case 0x24:
                return Key::Enter;
            case 0x35:
                return Key::Escape;
            case 0x30:
                return Key::Tab;
            case 0x7B:
                return Key::Left;
            case 0x7C:
                return Key::Right;
            case 0x7E:
                return Key::Up;
            case 0x7D:
                return Key::Down;
            default:
                return std::nullopt;
        }
    }

    glm::uvec2 GetWindowSize(RimeraWindow* window)
    {
        if (window == nil) return {0, 0};

        const NSRect bounds = [[window contentView] bounds];
        return {static_cast<std::uint32_t>(NSWidth(bounds)),
                static_cast<std::uint32_t>(NSHeight(bounds))};
    }
}

RM::Result<Window*> Window::Create()
{
    auto* window = new (std::nothrow) Window();
    if (!window) return std::unexpected("Window allocation failed");
    auto result = window->Initialize();
    if (!result)
    {
        auto error = std::move(result.error());
        delete window;
        return std::unexpected(std::move(error));
    }
    return window;
}

RM::Result<void> Window::Initialize()
{
    @autoreleasepool
    {
        if (![NSThread isMainThread])
            return std::unexpected("Window creation requires the main thread");

        PrepareApplication();

        const NSRect content_rect = NSMakeRect(0.0, 0.0, m_size.x, m_size.y);
        const NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                       NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        RimeraWindow* window = [[RimeraWindow alloc] initWithContentRect:content_rect
                                                               styleMask:style
                                                                 backing:NSBackingStoreBuffered
                                                                   defer:NO];
        if (window == nil) return std::unexpected("Failed to create window");

        [window setReleasedWhenClosed:NO];
        [window setAcceptsMouseMovedEvents:YES];
        [window setTitle:@"Rimera"];
        [window center];
        [window makeKeyAndOrderFront:nil];
        [[NSApplication sharedApplication] activateIgnoringOtherApps:YES];

        m_window = window;
        m_running = true;
        m_size = GetWindowSize(window);
    }
    return {};
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
    BeginInput();
    auto* window = static_cast<RimeraWindow*>(m_window);
    if (window == nil)
    {
        m_running = false;
        m_size = {0, 0};
        m_resized = previous_size.x != 0 || previous_size.y != 0;
        return;
    }

    @autoreleasepool
    {
        NSApplication* application = [NSApplication sharedApplication];
        NSEvent* event = nil;
        while ((event = [application nextEventMatchingMask:NSEventMaskAny
                                                 untilDate:[NSDate distantPast]
                                                    inMode:NSDefaultRunLoopMode
                                                   dequeue:YES]) != nil)
        {
            bool handled = false;
            switch ([event type])
            {
                case NSEventTypeKeyDown:
                case NSEventTypeKeyUp:
                    if ([event modifierFlags] & NSEventModifierFlagCommand) break;
                    if (auto key = ToKey([event keyCode]))
                        SetKey(*key, [event type] == NSEventTypeKeyDown);
                    handled = true;
                    break;
                case NSEventTypeFlagsChanged:
                {
                    const NSEventModifierFlags flags = [event modifierFlags];
                    SetKey(Key::Shift, flags & NSEventModifierFlagShift);
                    SetKey(Key::Control, flags & NSEventModifierFlagControl);
                    SetKey(Key::Alt, flags & NSEventModifierFlagOption);
                    break;
                }
                case NSEventTypeLeftMouseDown:
                    if ([event window] == window) SetMouseButton(MouseButton::Left, true);
                    break;
                case NSEventTypeLeftMouseUp:
                    SetMouseButton(MouseButton::Left, false);
                    break;
                case NSEventTypeRightMouseDown:
                    if ([event window] == window) SetMouseButton(MouseButton::Right, true);
                    break;
                case NSEventTypeRightMouseUp:
                    SetMouseButton(MouseButton::Right, false);
                    break;
                case NSEventTypeOtherMouseDown:
                    if ([event window] == window && [event buttonNumber] == 2)
                        SetMouseButton(MouseButton::Middle, true);
                    break;
                case NSEventTypeOtherMouseUp:
                    if ([event buttonNumber] == 2) SetMouseButton(MouseButton::Middle, false);
                    break;
                case NSEventTypeMouseMoved:
                case NSEventTypeLeftMouseDragged:
                case NSEventTypeRightMouseDragged:
                case NSEventTypeOtherMouseDragged:
                    m_mouse_delta += glm::vec2([event deltaX], [event deltaY]);
                    break;
                case NSEventTypeScrollWheel:
                    m_scroll_delta += static_cast<float>(
                        [event hasPreciseScrollingDeltas] ? [event scrollingDeltaY] * 0.1
                                                          : [event scrollingDeltaY]);
                    break;
                default:
                    break;
            }
            if (!handled) [application sendEvent:event];
        }
        [application updateWindows];
        m_running = ![window is_rimera_closed];

        const NSPoint mouse = [window mouseLocationOutsideOfEventStream];
        const NSRect bounds = [[window contentView] bounds];
        m_mouse_position = {static_cast<float>(mouse.x),
                            static_cast<float>(NSHeight(bounds) - mouse.y)};
        if (![window isKeyWindow]) ClearInput();
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
