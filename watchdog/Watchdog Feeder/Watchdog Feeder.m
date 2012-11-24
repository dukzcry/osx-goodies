/* Written by Artem Falcon <lomka@gero.in> */

#include <stdio.h>
#include <stdlib.h>
#include <IOKit/IOKitLib.h>
#import <Foundation/Foundation.h>

io_object_t   ioObject;
struct sigaction sig_act;

void disable()
{
    IORegistryEntrySetCFProperty(ioObject, CFSTR("tcoWdDisableTimer"), CFSTR(""));
    sig_act.sa_handler = SIG_DFL;
    sigaction(SIGINT, &sig_act, 0);
}

@interface FeedWatchDog : NSObject
{
@protected
    NSTimer *timer;
}
@end 

@implementation FeedWatchDog
- (id) init
{
    CFMutableDictionaryRef properties = NULL;
    CFTypeRef              settings, timeout;
    CFTypeID type;
    UInt32 time;
    
    if ((self = [super init])) {
        if ([self communicate])
            return NULL;
        
        if (IORegistryEntryCreateCFProperties(ioObject, &properties, kCFAllocatorDefault, kNilOptions)
            != KERN_SUCCESS || !properties) {
            NSLog(@"Error: Can't get system properties\n");
            return NULL;
        }
        
        if (!(settings = CFDictionaryGetValue(properties, CFSTR("Settings")))) {
            CFRelease((CFTypeRef) properties);
            NSLog(@"Error: Can't read timeout\n");
            return NULL;
        }
        
        CFRetain(settings);
        CFRelease((CFTypeRef) properties);
        
        type = CFGetTypeID(settings);
        if (type != CFDictionaryGetTypeID()) {
            CFRelease(settings);
            NSLog(@"Error: Settings isn't a dictionary\n");
            return NULL;
        }
        
        if (!(timeout = CFDictionaryGetValue(settings, CFSTR("Timeout")))) {
            CFRelease(settings);
            NSLog(@"Error: Can't get timeout\n");
            return NULL;
        }
        
        CFRelease(settings);
        
        type = CFGetTypeID(timeout);
        if (type != CFNumberGetTypeID()) {
            NSLog(@"Error: Settings isn't a dictionary\n");
            return NULL;
        }
        
        CFRetain(timeout);
        CFNumberGetValue((CFNumberRef) timeout, kCFNumberSInt32Type, &time);
        
        if (IORegistryEntrySetCFProperty(ioObject, CFSTR("tcoWdSetTimer"), timeout) != KERN_SUCCESS ||
            IORegistryEntrySetCFProperty(ioObject, CFSTR("tcoWdEnableTimer"), CFSTR("")) != KERN_SUCCESS) {
            CFRelease(timeout);
            NSLog(@"Error: Can't init watchdog\n");
            return NULL;
        }
        
        sig_act.sa_flags = 0;
        sigemptyset(&sig_act.sa_mask);
        sigaddset(&sig_act.sa_mask, SIGINT);
        sig_act.sa_handler = disable;
        
        sigaction(SIGINT, &sig_act, 0);
        
        NSLog(@"Watchdog Feeder running\n");
            
        timer = [NSTimer scheduledTimerWithTimeInterval:time-2 target:self
                                               selector:@selector(bone:) userInfo:nil repeats:YES];
        
        CFRelease(timeout);
    }
    
    return self;
}
- (void) dealloc
{
    [timer invalidate];
    disable();
    IOObjectRelease(ioObject);
    [super dealloc];
}
- (void) bone:(NSTimer *)timer
{
    NSLog(@"here's a bone for watchdog");
    IORegistryEntrySetCFProperty(ioObject, CFSTR("tcoWdLoadTimer"), CFSTR(""));
}
- (kern_return_t) communicate
{
    kern_return_t result;
    mach_port_t   masterPort;
    io_iterator_t iterator;
    
	IOMasterPort(MACH_PORT_NULL, &masterPort);
    
    CFMutableDictionaryRef matchingDictionary = IOServiceMatching("iTCOWatchdog");
    result = IOServiceGetMatchingServices(masterPort, matchingDictionary, &iterator);
    if (result != kIOReturnSuccess)
    {
        NSLog(@"Error: IOServiceGetMatchingServices() = %08x\n", result);
        return 1;
    }
    
    ioObject = IOIteratorNext(iterator);
    IOObjectRelease(iterator);
    if (!ioObject)
    {
        NSLog(@"Error: no iTCOWatchdog found\n");
        return 1;
    }
    
    IOObjectRetain(ioObject);
    
    return kIOReturnSuccess;
}
@end

int main(int argc, char *argv[])
{
    NSRunLoop *loop = [NSRunLoop currentRunLoop];
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    FeedWatchDog *feed_watchdog = [[FeedWatchDog alloc] init];
    
    if (!feed_watchdog) {
        [pool release];
        return EXIT_FAILURE;
    }
    
    while ([loop runMode: NSDefaultRunLoopMode beforeDate: [NSDate distantFuture]]) ;
    
    [feed_watchdog release];
    [pool release];
	return EXIT_SUCCESS; 
}