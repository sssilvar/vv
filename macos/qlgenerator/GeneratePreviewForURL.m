#import <CoreFoundation/CoreFoundation.h>
#import <QuickLook/QuickLook.h>
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <dlfcn.h>

// kUTTypePNG removed in macOS 12 SDK — use the raw UTI string directly.
#define VV_UTI_PNG CFSTR("public.png")

// Locate the vv executable bundled alongside this qlgenerator.
// At runtime this dylib lives at:
//   vv.app/Contents/Library/QuickLook/vv.qlgenerator/Contents/MacOS/vv_ql
// So vv is at:
//   vv.app/Contents/MacOS/vv
static NSString *findVVExecutable(void) {
    Dl_info info;
    if (dladdr((void *)findVVExecutable, &info) == 0 || !info.dli_fname) {
        return nil;
    }
    NSString *dylibPath = [NSString stringWithUTF8String:info.dli_fname];
    // Strip:  MacOS/vv_ql → Contents → vv.qlgenerator → QuickLook → Library → Contents
    NSString *macosDir       = [dylibPath stringByDeletingLastPathComponent];
    NSString *qlgenContents  = [macosDir stringByDeletingLastPathComponent];
    NSString *qlgenBundle    = [qlgenContents stringByDeletingLastPathComponent];
    NSString *quicklookDir   = [qlgenBundle stringByDeletingLastPathComponent];
    NSString *libraryDir     = [quicklookDir stringByDeletingLastPathComponent];
    NSString *appContents    = [libraryDir stringByDeletingLastPathComponent];
    NSString *vvPath         = [appContents stringByAppendingPathComponent:@"MacOS/vv"];
    return [[NSFileManager defaultManager] fileExistsAtPath:vvPath] ? vvPath : nil;
}

static NSString *renderToPNG(CFURLRef url) {
    NSString *vvPath = findVVExecutable();
    if (!vvPath) return nil;

    NSString *filePath = [(__bridge NSURL *)url path];
    NSString *tmpPng = [NSTemporaryDirectory() stringByAppendingPathComponent:
        [NSString stringWithFormat:@"vv_ql_%@.png", [[NSUUID UUID] UUIDString]]];

    NSTask *task = [[NSTask alloc] init];
    task.launchPath = vvPath;
    task.arguments  = @[@"--thumbnail", filePath, tmpPng];
    // Suppress stdout/stderr from the subprocess
    task.standardOutput = [NSFileHandle fileHandleWithNullDevice];
    task.standardError  = [NSFileHandle fileHandleWithNullDevice];
    [task launch];
    [task waitUntilExit];

    return (task.terminationStatus == 0) ? tmpPng : nil;
}

OSStatus GeneratePreviewForURL(void *thisInterface,
                                QLPreviewRequestRef preview,
                                CFURLRef url,
                                CFStringRef contentTypeUTI,
                                CFDictionaryRef options) {
    @autoreleasepool {
        NSString *pngPath = renderToPNG(url);
        if (!pngPath) return noErr;

        NSData *pngData = [NSData dataWithContentsOfFile:pngPath];
        [[NSFileManager defaultManager] removeItemAtPath:pngPath error:nil];
        if (!pngData) return noErr;

        NSDictionary *props = @{(__bridge NSString *)kQLPreviewPropertyMIMETypeKey: @"image/png"};
        QLPreviewRequestSetDataRepresentation(preview,
            (__bridge CFDataRef)pngData,
            VV_UTI_PNG,
            (__bridge CFDictionaryRef)props);
    }
    return noErr;
}

void CancelPreviewGeneration(void *thisInterface, QLPreviewRequestRef preview) {}

OSStatus GenerateThumbnailForURL(void *thisInterface,
                                  QLThumbnailRequestRef thumbnail,
                                  CFURLRef url,
                                  CFStringRef contentTypeUTI,
                                  CFDictionaryRef options,
                                  CGFloat maxSize) {
    @autoreleasepool {
        NSString *pngPath = renderToPNG(url);
        if (!pngPath) return noErr;

        NSData *pngData = [NSData dataWithContentsOfFile:pngPath];
        [[NSFileManager defaultManager] removeItemAtPath:pngPath error:nil];
        if (!pngData) return noErr;

        CGDataProviderRef provider = CGDataProviderCreateWithCFData((__bridge CFDataRef)pngData);
        CGImageRef image = CGImageCreateWithPNGDataProvider(provider, NULL, false,
                                                             kCGRenderingIntentDefault);
        if (image) {
            QLThumbnailRequestSetImage(thumbnail, image, NULL);
            CGImageRelease(image);
        }
        CGDataProviderRelease(provider);
    }
    return noErr;
}

void CancelThumbnailGeneration(void *thisInterface, QLThumbnailRequestRef thumbnail) {}
