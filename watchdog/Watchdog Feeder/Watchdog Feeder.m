/* Written by Artem Falcon <lomka@gero.in> */

#include <Cocoa/Cocoa.h>

io_object_t   ioObject;
struct sigaction sig_act;
bool logstd;

void disable(int sig)
{
    IORegistryEntrySetCFProperty(ioObject, CFSTR("tcoWdDisableTimer"), CFSTR(""));
    
    if (sig > -1) {
        /* For launchd */
        if (sig == SIGTERM) exit(EXIT_SUCCESS);
        
        sigprocmask(SIG_UNBLOCK, &sig_act.sa_mask, NULL);
        
        sig_act.sa_handler = SIG_DFL;
        sigaction(sig, &sig_act, NULL);
    }
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
    int i = 0;
    bool st = false;
    
    if ((self = [super init])) {
        /* Wait for a kernel service to be available via stupid polling */
        for (; i < 10; i++) {
            if ((st = [self communicate]))
                break;
            [NSThread sleepForTimeInterval:8];
        }
        if (!ioObject)
        {
            NSLog(@"Error: no iTCOWatchdog found\n");
            return NULL;
        }
        if (!st) return NULL;
        IOObjectRetain(ioObject);

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
        
        sig_act.sa_handler = disable;
        sig_act.sa_flags = 0;
        sigfillset(&sig_act.sa_mask);
        
        sigdelset(&sig_act.sa_mask, SIGINT);
        sigdelset(&sig_act.sa_mask, SIGTERM);
        sigaction(SIGINT, &sig_act, NULL); /* Catch this signal even if -d flag wasn't given */
        sigaction(SIGTERM, &sig_act, NULL);
        
        sigprocmask(SIG_BLOCK, &sig_act.sa_mask, NULL);
        
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
    disable(-1);
    IOObjectRelease(ioObject);
    [super dealloc];
}
- (void) bone:(NSTimer *)timer
{
    if (logstd) NSLog(@"here's a bone for watchdog");
    IORegistryEntrySetCFProperty(ioObject, CFSTR("tcoWdLoadTimer"), CFSTR(""));
}
- (bool) communicate
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
        return false;
    }
    
    ioObject = IOIteratorNext(iterator);
    IOObjectRelease(iterator);
    
    if (!ioObject) return false;
    
    return true;
}
@end

int main(int argc, char *argv[])
{
    NSRunLoop *loop;
    NSAutoreleasePool *pool;
    FeedWatchDog *feed_watchdog;
    
    int c;
    
    logstd = false;
    if ((c = getopt(argc, argv, "dh")) != -1) {
        switch(c) {
            case 'd':
                logstd = true;
            break;
            case 'h':
                printf("Usage:\n");
                printf("%s [options]\n", argv[0]);
                printf("\t-d\t: log to stderr\n");
                printf("\t-h\t: help\n");
                return EXIT_SUCCESS;
            break;
        }
    }
    
    loop = [NSRunLoop currentRunLoop];
    pool = [[NSAutoreleasePool alloc] init];
    if (!(feed_watchdog = [[FeedWatchDog alloc] init])) {
        [pool release];
        return EXIT_FAILURE;
    }
    
    while ([loop runMode: NSDefaultRunLoopMode beforeDate: [NSDate distantFuture]]) ;
    
    [feed_watchdog release];
    [pool release];
	return EXIT_SUCCESS;   
}