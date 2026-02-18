#include "./macos.h"
#include <Cocoa/Cocoa.h>
#include <QDebug>
#include <QMainWindow>

void setupMacOSWindow(QMainWindow *window)
{
    window->setUnifiedTitleAndToolBarOnMac(true);

    NSView *nativeView = reinterpret_cast<NSView *>(window->winId());
    NSWindow *nativeWindow = [nativeView window];

    [nativeWindow setStyleMask:[nativeWindow styleMask] |
                               NSWindowStyleMaskFullSizeContentView |
                               NSWindowTitleHidden];
    [nativeWindow setTitlebarAppearsTransparent:YES];
    [nativeWindow center];
}
// TODO:remove
void setupMacOSWindowOLD(QMainWindow *window)
{

    if (!window) {
        qWarning() << "setupMacOSWindow: window is null";
        return;
    }

    NSView *nativeView = reinterpret_cast<NSView *>(window->winId());
    NSWindow *nativeWindow = [nativeView window];
    [nativeWindow setMovableByWindowBackground:YES];
    if (!nativeWindow) {
        qWarning() << "setupMacOSWindow: native window is null";
        return;
    }
    // TODO: implement theme switching from app settings
    // // Force dark mode
    // nsWindow.overrideUserInterfaceStyle = NSUserInterfaceStyleDark;

    // Force light mode
    // if (@available(macOS 10.14, *)) {
    //     [nativeWindow
    //         setAppearance:[NSAppearance
    //         appearanceNamed:NSAppearanceNameAqua]];
    // } else {
    //     // Fallback: no-op on older macOS versions
    // }
    // nativeWindow.overrideUserInterfaceStyle = NSUserInterfaceStyleLight;

    // // Reset to follow system (default)
    // nsWindow.overrideUserInterfaceStyle = NSUserInterfaceStyleUnspecified;

    qDebug() << "Setting up macOS window styles";

    // window->setUnifiedTitleAndToolBarOnMac(true);

    [nativeWindow setStyleMask:[nativeWindow styleMask] |
                               NSWindowStyleMaskFullSizeContentView |
                               NSWindowTitleHidden];
    [nativeWindow setTitleVisibility:NSWindowTitleHidden];
    [nativeWindow setTitlebarAppearsTransparent:YES];

    NSToolbar *toolbar =
        [[NSToolbar alloc] initWithIdentifier:@"HiddenInsetToolbar"];
    toolbar.showsBaselineSeparator =
        NO; // equivalent to HideToolbarSeparator: true
    [nativeWindow setToolbar:toolbar];
    // [toolbar setVisible:NO];
    // todo : is it ok ?
    [toolbar release];
    // [nativeWindow setContentBorderThickness:0.0 forEdge:NSMinYEdge];

    [nativeWindow center];
}

@interface DiskUsagePopoverViewController : NSViewController
@property(nonatomic, strong) NSTextField *typeLabel;
@property(nonatomic, strong) NSTextField *sizeLabel;
@property(nonatomic, strong) NSTextField *percentageLabel;
@end

// Static variables for popover management
NSPopover *s_popover = nil;
NSViewController *s_viewController = nil;

@implementation DiskUsagePopoverViewController
- (void)loadView
{
    NSView *view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 180, 80)];

    // Type label
    self.typeLabel =
        [[NSTextField alloc] initWithFrame:NSMakeRect(40, 55, 130, 16)];
    self.typeLabel.editable = NO;
    self.typeLabel.selectable = NO;
    self.typeLabel.bordered = NO;
    self.typeLabel.backgroundColor = [NSColor clearColor];
    self.typeLabel.font = [NSFont boldSystemFontOfSize:13];
    [view addSubview:self.typeLabel];

    // Size label
    self.sizeLabel =
        [[NSTextField alloc] initWithFrame:NSMakeRect(10, 30, 160, 16)];
    self.sizeLabel.editable = NO;
    self.sizeLabel.selectable = NO;
    self.sizeLabel.bordered = NO;
    self.sizeLabel.backgroundColor = [NSColor clearColor];
    self.sizeLabel.font = [NSFont systemFontOfSize:11];
    [view addSubview:self.sizeLabel];

    // Percentage label
    self.percentageLabel =
        [[NSTextField alloc] initWithFrame:NSMakeRect(10, 10, 160, 16)];
    self.percentageLabel.editable = NO;
    self.percentageLabel.selectable = NO;
    self.percentageLabel.bordered = NO;
    self.percentageLabel.backgroundColor = [NSColor clearColor];
    self.percentageLabel.font = [NSFont systemFontOfSize:11];
    self.percentageLabel.textColor = [NSColor secondaryLabelColor];
    [view addSubview:self.percentageLabel];

    self.view = view;
}

- (void)updateWithInfo:(const UsageInfo &)info
{
    self.typeLabel.stringValue =
        [NSString stringWithUTF8String:info.type.toUtf8().constData()];
    self.sizeLabel.stringValue =
        [NSString stringWithUTF8String:info.formattedSize.toUtf8().constData()];
    self.percentageLabel.stringValue = [NSString
        stringWithFormat:@"%.1f%% of total capacity", info.percentage];
}

@end

void hidePopoverForBarWidget()
{
    if (s_popover) {
        [s_popover close];
        [s_popover release];
        s_popover = nil;
    }
    if (s_viewController) {
        [s_viewController release];
        s_viewController = nil;
    }
}
// TODO: bug report to Qt, window becomes blurry, shifted or resized after
// showing popover
void showPopoverForBarWidget(QWidget *widget, const UsageInfo &info)
{
    if (!widget)
        return;

    // Hide existing popover if any
    hidePopoverForBarWidget();

    // Get the native view
    NSView *nativeView = reinterpret_cast<NSView *>(widget->winId());
    if (!nativeView)
        return;

    NSWindow *window = [nativeView window];
    if (!window)
        return;

    // Create view controller and force view loading
    DiskUsagePopoverViewController *viewController =
        [[DiskUsagePopoverViewController alloc] init];

    // Force the view to load before updating
    [viewController loadView];
    [viewController updateWithInfo:info];

    // Create popover
    NSPopover *popover = [[NSPopover alloc] init];
    [popover setContentSize:NSMakeSize(180, 80)];
    [popover setBehavior:NSPopoverBehaviorTransient];
    [popover setAnimates:YES];
    [popover setContentViewController:viewController];

    // Use the widget's bounds for a simpler approach
    NSRect widgetBounds = nativeView.bounds;

    // Show popover
    [popover showRelativeToRect:widgetBounds
                         ofView:nativeView
                  preferredEdge:NSMinYEdge];

    // Store references (retain them)
    s_popover = [popover retain];
    s_viewController = [viewController retain];
}

// taken from
// https://github.com/tauri-apps/tao/blob/3c2b4447aa53151ae96d30a60928d1d71e9bb5fc/src/platform_impl/macos/view.rs#L1154
void setTrafficLightInset(NSPoint position, NSWindow *window)
{
    CGFloat x = position.x;
    CGFloat y = position.y;

    NSButton *closeButton = [window standardWindowButton:NSWindowCloseButton];
    NSButton *miniaturizeButton =
        [window standardWindowButton:NSWindowMiniaturizeButton];
    NSButton *zoomButton = [window standardWindowButton:NSWindowZoomButton];

    NSView *titleBarContainer = closeButton.superview.superview;

    NSRect closeRect = closeButton.frame;
    CGFloat titleBarFrameHeight = closeRect.size.height + y;
    NSRect titleBarRect = titleBarContainer.frame;
    titleBarRect.size.height = titleBarFrameHeight;
    titleBarRect.origin.y = window.frame.size.height - titleBarFrameHeight;
    [titleBarContainer setFrame:titleBarRect];

    CGFloat spaceBetween = NSMinX(miniaturizeButton.frame) - NSMinX(closeRect);
    NSArray<NSButton *> *buttons =
        @[ closeButton, miniaturizeButton, zoomButton ];

    for (NSUInteger i = 0; i < buttons.count; i++) {
        NSButton *button = buttons[i];
        NSRect rect = button.frame;
        rect.origin.x = x + (i * spaceBetween);
        [button setFrameOrigin:rect.origin];
    }
}

void setupToolFrame(QWidget *toolFrame)
{
    if (!toolFrame) {
        qWarning() << "setupToolFrame: toolFrame is null";
        return;
    }

    NSView *nativeView = reinterpret_cast<NSView *>(toolFrame->winId());
    if (!nativeView) {
        qWarning() << "setupToolFrame: native view is null";
        return;
    }

    NSWindow *window = [nativeView window];
    if (!window) {
        qWarning() << "setupToolFrame: native window is null";
        return;
    }

    // Doesn't work, need to figure out a better way, we need to remove
    // fullscreen button NSWindowStyleMask mask = [window styleMask]; mask &=
    // ~NSWindowStyleMaskClosable; mask &= ~NSWindowStyleMaskMiniaturizable;
    // mask &= ~NSWindowStyleMaskFullScreen;
    // [window setStyleMask:mask];

    NSRect windowFrame = [[window contentView] frame];

    // NSEdgeInsets insets = NSEdgeInsetsMake(20, 15, 0, 0);
    // // macOS 12.0+ uses a single inset point; adjust as needed for your
    // target if (@available(macOS 12.0, *)) {
    //     [window setTrafficLightInset:NSMakePoint(15, 20)];
    // } else {
    //     [window setTrafficLightInsets:insets];
    // }

    // Works but resizing messes up the styles
    // dispatch_async(dispatch_get_main_queue(), ^{
    //   setTrafficLightInset(NSMakePoint(20, 25), window);
    // // x = horizontal inset from left, y = extra height added to title bar
    // setTrafficLightInset(NSMakePoint(35.0, 44.0), window);
    // });

    NSToolbar *toolbar =
        [[NSToolbar alloc] initWithIdentifier:@"HiddenInsetToolbar"];
    toolbar.showsBaselineSeparator =
        NO; // equivalent to HideToolbarSeparator: true
    [window setToolbar:toolbar];
    [window setBackgroundColor:[NSColor colorWithWhite:0.95 alpha:1.0]];
    // [toolbar setVisible:NO];
    // todo : is it ok ?
    [toolbar release];
    // TODO: theming
    [window setBackgroundColor:[NSColor colorWithWhite:0.95 alpha:1.0]];

    // NSButton *closeBtn = [window standardWindowButton:NSWindowCloseButton];
    // if (closeBtn) {
    //         qDebug() << "Hiding close button";
    //     [closeBtn setHidden:YES]; }
    //   // NSButton *miniBtn = [window
    // standardWindowButton:NSWindowMiniaturizeButton]; if (miniBtn) {
    //         qDebug() << "Hiding minimize button";
    //     [miniBtn setHidden:YES]// ; }
}
