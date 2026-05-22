#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>

#import <Foundation/Foundation.h>
#include <CoreFoundation/CFBundle.h>
#include <ApplicationServices/ApplicationServices.h>

const char* PlatformOpenURL(const char* textUrl)
{
    std::string_view urlView = textUrl;
    CFURLRef url = CFURLCreateWithBytes(nullptr, (const UInt8*)urlView.data(), urlView.size(), kCFStringEncodingUTF8, nullptr);
    LSOpenCFURLRef(url, nullptr);
    CFRelease(url);
    return nullptr;
}

std::tuple<std::optional<std::filesystem::path>, std::optional<std::string>> PlatformFindUserPath()
{
    NSArray* paths = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
    if (!paths || [paths count] == 0) {
        return { {}, "Could not obtain Application Support path from macOS" };
    }
    NSString* base = [paths objectAtIndex:0];
    std::filesystem::path path = std::filesystem::path([base UTF8String]) / "Path of Building 2";
    return { path, {} };
}
