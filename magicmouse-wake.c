// One-shot Magic Mouse 2 (Lightning) wake-up helper for macOS 27.
//
// Security properties:
// - no network access
// - no input-event reading or recording
// - no files, logs, daemon, or login item
// - no administrator/root privileges
// - matches only Apple Magic Mouse 2 product 0x0269, pointer interface 0x01/0x02
// - sends report ID 0xF1 with the three-byte payload 0x06 0x01 0x37

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDManager.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#define APPLE_USB_VENDOR_ID       0x05ac
#define APPLE_BLUETOOTH_VENDOR_ID 0x004c
#define MAGIC_MOUSE_2_PRODUCT_ID  0x0269
#define GENERIC_DESKTOP_PAGE      0x0001
#define MOUSE_USAGE               0x0002
#define KEYHOLE_REPORT_ID         0x00f1

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

int main(void) {
    if (native_driver_active()) {
        puts("The native Magic Mouse driver is already active. Nothing was changed.");
        return 0;
    }

    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (!manager) {
        fputs("Could not create the macOS HID manager.\n", stderr);
        return 1;
    }

    CFMutableDictionaryRef bluetooth_match = create_match(APPLE_BLUETOOTH_VENDOR_ID);
    CFMutableDictionaryRef usb_match = create_match(APPLE_USB_VENDOR_ID);
    if (!bluetooth_match || !usb_match) {
        fputs("Could not create the Magic Mouse matching rules.\n", stderr);
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
        fputs("Could not create the Magic Mouse matching list.\n", stderr);
        CFRelease(manager);
        return 1;
    }

    IOHIDManagerSetDeviceMatchingMultiple(manager, matches);
    CFRelease(matches);

    IOReturn manager_result = IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
    if (manager_result != kIOReturnSuccess) {
        fprintf(stderr, "macOS denied HID access (0x%08x).\n", (unsigned int)manager_result);
        fputs("Enable Terminal under System Settings > Privacy & Security > Input Monitoring,\n"
              "then quit Terminal and double-click the launcher again.\n", stderr);
        CFRelease(manager);
        return 3;
    }

    CFSetRef devices = IOHIDManagerCopyDevices(manager);
    if (!devices || CFSetGetCount(devices) == 0) {
        fputs("No connected Lightning Magic Mouse 2 (product 0x0269) pointer interface was found.\n",
              stderr);
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
        puts("Success: the native Magic Mouse driver is active. Test the mouse now.");
        return 0;
    }

    if (matching_interfaces == 0) {
        fputs("The correct Magic Mouse pointer interface was not found.\n", stderr);
        return 2;
    }

    if (accepted_reports == 0 && permission_failures > 0) {
        fputs("macOS blocked access to the mouse. Enable Terminal under\n"
              "System Settings > Privacy & Security > Input Monitoring, then try again.\n",
              stderr);
        return 3;
    }

    if (accepted_reports == 0) {
        fputs("The mouse rejected the wake-up report. No settings were changed.\n", stderr);
        return 4;
    }

    fputs("The report was accepted, but the native driver did not start.\n"
          "This workaround does not match your particular mouse or macOS build.\n",
          stderr);
    return 5;
}
