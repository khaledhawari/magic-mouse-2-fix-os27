// Magic Mouse 2 (Lightning) wake-up helper for macOS 27.
//
// Without arguments it runs once and exits. With --watch it stays running and
// repeats the same wake-up whenever the mouse is connected but Apple's native
// driver is missing (after a screen lock, a reconnect, or a restart). The
// --watch mode is meant to be started by the per-user LaunchAgent that
// "Install Auto Wake.command" sets up.
//
// Security properties:
// - no network access
// - no input-event reading or recording
// - one-shot mode: no files, logs, daemon, or login item
// - watch mode: prints state changes only; never reads input events
// - no administrator/root privileges
// - matches only Apple Magic Mouse 2 product 0x0269, pointer interface 0x01/0x02
// - sends report ID 0xF1 with the three-byte payload 0x06 0x01 0x37

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hidsystem/IOHIDLib.h>
#include <IOKit/hid/IOHIDManager.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define APPLE_USB_VENDOR_ID       0x05ac
#define APPLE_BLUETOOTH_VENDOR_ID 0x004c
#define MAGIC_MOUSE_2_PRODUCT_ID  0x0269
#define GENERIC_DESKTOP_PAGE      0x0001
#define MOUSE_USAGE               0x0002
#define KEYHOLE_REPORT_ID         0x00f1

// Exit / result codes shared by both modes.
#define RESULT_OK              0
#define RESULT_ERROR           1
#define RESULT_NO_MOUSE        2
#define RESULT_NO_PERMISSION   3
#define RESULT_REJECTED        4
#define RESULT_NO_DRIVER       5

// In watch mode the per-attempt messages are suppressed; only state changes
// are logged by the watch loop.
static int quiet = 0;

static void say(FILE *stream, const char *format, ...) {
    if (quiet) return;
    va_list args;
    va_start(args, format);
    vfprintf(stream, format, args);
    va_end(args);
}

static CFMutableDictionaryRef create_match(int vendor_id) {
    int product_id = MAGIC_MOUSE_2_PRODUCT_ID;
    int usage_page = GENERIC_DESKTOP_PAGE;
    int usage = MOUSE_USAGE;

    CFMutableDictionaryRef match = CFDictionaryCreateMutable(
        kCFAllocatorDefault,
        0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    if (!match) return NULL;

    CFNumberRef vendor = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendor_id);
    CFNumberRef product = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &product_id);
    CFNumberRef page = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usage_page);
    CFNumberRef use = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usage);

    if (!vendor || !product || !page || !use) {
        if (vendor) CFRelease(vendor);
        if (product) CFRelease(product);
        if (page) CFRelease(page);
        if (use) CFRelease(use);
        CFRelease(match);
        return NULL;
    }

    CFDictionarySetValue(match, CFSTR(kIOHIDVendorIDKey), vendor);
    CFDictionarySetValue(match, CFSTR(kIOHIDProductIDKey), product);
    CFDictionarySetValue(match, CFSTR(kIOHIDPrimaryUsagePageKey), page);
    CFDictionarySetValue(match, CFSTR(kIOHIDPrimaryUsageKey), use);

    CFRelease(vendor);
    CFRelease(product);
    CFRelease(page);
    CFRelease(use);
    return match;
}

static int number_property(IOHIDDeviceRef device, CFStringRef key, int *out_value) {
    CFTypeRef property = IOHIDDeviceGetProperty(device, key);
    if (!property || CFGetTypeID(property) != CFNumberGetTypeID()) return 0;
    return CFNumberGetValue((CFNumberRef)property, kCFNumberIntType, out_value);
}

// Some macOS Bluetooth HID interfaces expose a KeyholeReportID. Only 0xF1 is
// the pointer interface that should receive this report. Older systems may not
// expose the property, in which case the strict usage-page match above is used.
static int correct_keyhole_interface(IOHIDDeviceRef device) {
    int keyhole = 0;
    if (!number_property(device, CFSTR("KeyholeReportID"), &keyhole)) return 1;
    return keyhole == KEYHOLE_REPORT_ID;
}

static int native_driver_active(void) {
    io_iterator_t iterator = IO_OBJECT_NULL;
    kern_return_t result = IOServiceGetMatchingServices(
        kIOMainPortDefault,
        IOServiceMatching("AppleMultitouchDevice"),
        &iterator);
    if (result != KERN_SUCCESS) return 0;

    int active = 0;
    io_object_t service;
    while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        CFTypeRef property = IORegistryEntrySearchCFProperty(
            service,
            kIOServicePlane,
            CFSTR("ProductID"),
            kCFAllocatorDefault,
            kIORegistryIterateRecursively | kIORegistryIterateParents);

        if (property && CFGetTypeID(property) == CFNumberGetTypeID()) {
            int product_id = 0;
            if (CFNumberGetValue((CFNumberRef)property, kCFNumberIntType, &product_id) &&
                product_id == MAGIC_MOUSE_2_PRODUCT_ID) {
                active = 1;
            }
        }
        if (property) CFRelease(property);
        IOObjectRelease(service);
    }

    IOObjectRelease(iterator);
    return active;
}

static int wake_once(void) {
    if (native_driver_active()) {
        say(stdout, "The native Magic Mouse driver is already active. Nothing was changed.\n");
        return 0;
    }

    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (!manager) {
        say(stderr, "Could not create the macOS HID manager.\n");
        return 1;
    }

    CFMutableDictionaryRef bluetooth_match = create_match(APPLE_BLUETOOTH_VENDOR_ID);
    CFMutableDictionaryRef usb_match = create_match(APPLE_USB_VENDOR_ID);
    if (!bluetooth_match || !usb_match) {
        say(stderr, "Could not create the Magic Mouse matching rules.\n");
        if (bluetooth_match) CFRelease(bluetooth_match);
        if (usb_match) CFRelease(usb_match);
        CFRelease(manager);
        return 1;
    }

    const void *match_values[] = { bluetooth_match, usb_match };
    CFArrayRef matches = CFArrayCreate(
        kCFAllocatorDefault,
        match_values,
        2,
        &kCFTypeArrayCallBacks);
    CFRelease(bluetooth_match);
    CFRelease(usb_match);

    if (!matches) {
        say(stderr, "Could not create the Magic Mouse matching list.\n");
        CFRelease(manager);
        return 1;
    }

    IOHIDManagerSetDeviceMatchingMultiple(manager, matches);
    CFRelease(matches);

    IOReturn manager_result = IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
    if (manager_result != kIOReturnSuccess) {
        say(stderr, "macOS denied HID access (0x%08x).\n", (unsigned int)manager_result);
        say(stderr, "Enable Terminal under System Settings > Privacy & Security > Input Monitoring,\n"
              "then quit Terminal and double-click the launcher again.\n");
        CFRelease(manager);
        return 3;
    }

    CFSetRef devices = IOHIDManagerCopyDevices(manager);
    if (!devices || CFSetGetCount(devices) == 0) {
        say(stderr, "No connected Lightning Magic Mouse 2 (product 0x0269) pointer interface was found.\n");
        if (devices) CFRelease(devices);
        IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
        CFRelease(manager);
        return 2;
    }

    CFIndex device_count = CFSetGetCount(devices);
    IOHIDDeviceRef device_list[device_count];
    CFSetGetValues(devices, (const void **)device_list);

    static const uint8_t payload[] = { 0x06, 0x01, 0x37 };
    int matching_interfaces = 0;
    int accepted_reports = 0;
    int permission_failures = 0;

    for (CFIndex index = 0; index < device_count; index++) {
        IOHIDDeviceRef device = device_list[index];
        if (!correct_keyhole_interface(device)) continue;
        matching_interfaces++;

        IOReturn open_result = IOHIDDeviceOpen(device, kIOHIDOptionsTypeNone);
        if (open_result != kIOReturnSuccess) permission_failures++;

        for (int attempt = 1; attempt <= 5; attempt++) {
            IOReturn send_result = IOHIDDeviceSetReport(
                device,
                kIOHIDReportTypeFeature,
                KEYHOLE_REPORT_ID,
                payload,
                (CFIndex)sizeof(payload));

            if (send_result == kIOReturnSuccess) accepted_reports++;
            usleep(500000);
            if (native_driver_active()) break;
        }

        if (open_result == kIOReturnSuccess) {
            IOHIDDeviceClose(device, kIOHIDOptionsTypeNone);
        }
        if (native_driver_active()) break;
    }

    CFRelease(devices);
    IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
    CFRelease(manager);

    if (native_driver_active()) {
        say(stdout, "Success: the native Magic Mouse driver is active. Test the mouse now.\n");
        return 0;
    }

    if (matching_interfaces == 0) {
        say(stderr, "The correct Magic Mouse pointer interface was not found.\n");
        return 2;
    }

    if (accepted_reports == 0 && permission_failures > 0) {
        say(stderr, "macOS blocked access to the mouse. Enable Terminal under\n"
              "System Settings > Privacy & Security > Input Monitoring, then try again.\n");
        return 3;
    }

    if (accepted_reports == 0) {
        say(stderr, "The mouse rejected the wake-up report. No settings were changed.\n");
        return 4;
    }

    say(stderr, "The report was accepted, but the native driver did not start.\n"
          "This workaround does not match your particular mouse or macOS build.\n");
    return 5;
}

static void log_line(const char *message) {
    time_t now = time(NULL);
    struct tm local;
    char stamp[32];
    localtime_r(&now, &local);
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
    printf("%s  %s\n", stamp, message);
    fflush(stdout);
}

static const char *describe(int result) {
    switch (result) {
        case RESULT_OK:            return "native driver active";
        case RESULT_NO_MOUSE:      return "mouse not connected; waiting";
        case RESULT_NO_PERMISSION: return "macOS denied HID access; enable this helper under "
                                          "System Settings > Privacy & Security > Input Monitoring";
        case RESULT_REJECTED:      return "mouse rejected the wake-up report; will retry";
        case RESULT_NO_DRIVER:     return "report accepted but native driver did not start; will retry";
        default:                   return "unexpected HID error; will retry";
    }
}

// Stays running and repeats the wake-up whenever the mouse is connected
// without its native driver. Checking is a cheap IORegistry lookup; the HID
// device is opened only when a wake-up is actually needed.
static int watch(void) {
    quiet = 1;
    log_line("watching for a Magic Mouse 2 without its native driver");

    // Registers this helper in the Input Monitoring list and shows the macOS
    // prompt the first time. The grant applies only after a restart of the
    // process, so exit and let launchd start it again.
    if (!IOHIDRequestAccess(kIOHIDRequestTypeListenEvent)) {
        log_line(describe(RESULT_NO_PERMISSION));
        return RESULT_NO_PERMISSION;
    }

    int last_result = -1;
    int failures = 0;

    for (;;) {
        int result = native_driver_active() ? RESULT_OK : wake_once();

        if (result != last_result) {
            if (result == RESULT_OK && last_result != -1) {
                log_line("woke the mouse: native driver active");
            } else {
                log_line(describe(result));
            }
            last_result = result;
        }

        if (result == RESULT_NO_PERMISSION) return RESULT_NO_PERMISSION;

        // Back off when the mouse keeps refusing, so it is not flooded.
        if (result == RESULT_REJECTED || result == RESULT_NO_DRIVER || result == RESULT_ERROR) {
            if (failures < 10) failures++;
        } else {
            failures = 0;
        }
        sleep(failures > 3 ? 15 : 2);
    }
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "--watch") == 0) return watch();
    return wake_once();
}
