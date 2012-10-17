//
//  FakeSMCPlugin.h
//  HWSensors
//
//  Created by mozo on 11/02/12.
//  Copyright (c) 2012 mozodojo. All rights reserved.
//

#ifndef HWSensors_FakeSMCFamily_h
#define HWSensors_FakeSMCFamily_h

#include <IOKit/IOService.h>
#include "FakeSMCDefinitions.h"

#define parent IOService

inline UInt16 swap_value(UInt16 value)
{
    return ((value & 0xff00) >> 8) | ((value & 0xff) << 8);
}
inline UInt16 encode_fpe2(UInt16 value)
{
	return swap_value(value << 2);
}
inline UInt16 decode_fpe2(UInt16 value)
{
    return (swap_value(value) >> 2);
}
inline OSString * vendorID(OSString * smbios_manufacturer)
{
    if (smbios_manufacturer) {
        if (smbios_manufacturer->isEqualTo("Alienware")) return OSString::withCString("Alienware");
        if (smbios_manufacturer->isEqualTo("Apple Inc.")) return OSString::withCString("Apple");
        if (smbios_manufacturer->isEqualTo("ASRock")) return OSString::withCString("ASRock");
        if (smbios_manufacturer->isEqualTo("ASUSTeK Computer INC.")) return OSString::withCString("ASUS");
        if (smbios_manufacturer->isEqualTo("ASUSTeK COMPUTER INC.")) return OSString::withCString("ASUS");
        if (smbios_manufacturer->isEqualTo("Dell Inc.")) return OSString::withCString("Dell");
        if (smbios_manufacturer->isEqualTo("DFI")) return OSString::withCString("DFI");
        if (smbios_manufacturer->isEqualTo("DFI Inc.")) return OSString::withCString("DFI");
        if (smbios_manufacturer->isEqualTo("ECS")) return OSString::withCString("ECS");
        if (smbios_manufacturer->isEqualTo("EPoX COMPUTER CO., LTD")) return OSString::withCString("EPoX");
        if (smbios_manufacturer->isEqualTo("EVGA")) return OSString::withCString("EVGA");
        if (smbios_manufacturer->isEqualTo("First International Computer, Inc.")) return OSString::withCString("FIC");
        if (smbios_manufacturer->isEqualTo("FUJITSU")) return OSString::withCString("FUJITSU");
        if (smbios_manufacturer->isEqualTo("FUJITSU SIEMENS")) return OSString::withCString("FUJITSU");
        if (smbios_manufacturer->isEqualTo("Gigabyte Technology Co., Ltd.")) return OSString::withCString("Gigabyte");
        if (smbios_manufacturer->isEqualTo("Hewlett-Packard")) return OSString::withCString("HP");
        if (smbios_manufacturer->isEqualTo("IBM")) return OSString::withCString("IBM");
        if (smbios_manufacturer->isEqualTo("Intel")) return OSString::withCString("Intel");
        if (smbios_manufacturer->isEqualTo("Intel Corp.")) return OSString::withCString("Intel");
        if (smbios_manufacturer->isEqualTo("Intel Corporation")) return OSString::withCString("Intel");
        if (smbios_manufacturer->isEqualTo("INTEL Corporation")) return OSString::withCString("Intel");
        if (smbios_manufacturer->isEqualTo("Lenovo")) return OSString::withCString("Lenovo");
        if (smbios_manufacturer->isEqualTo("LENOVO")) return OSString::withCString("Lenovo");
        if (smbios_manufacturer->isEqualTo("Micro-Star International")) return OSString::withCString("MSI");
        if (smbios_manufacturer->isEqualTo("MICRO-STAR INTERNATIONAL CO., LTD")) return OSString::withCString("MSI");
        if (smbios_manufacturer->isEqualTo("MICRO-STAR INTERNATIONAL CO.,LTD")) return OSString::withCString("MSI");
        if (smbios_manufacturer->isEqualTo("MSI")) return OSString::withCString("MSI");
        if (smbios_manufacturer->isEqualTo("Shuttle")) return OSString::withCString("Shuttle");
        if (smbios_manufacturer->isEqualTo("TOSHIBA")) return OSString::withCString("TOSHIBA");
        if (smbios_manufacturer->isEqualTo("XFX")) return OSString::withCString("XFX");
        if (smbios_manufacturer->isEqualTo("To be filled by O.E.M.")) return NULL;
    }
    return NULL;
}

class FakeSMCPlugin : public IOService {
    OSDeclareAbstractStructors(FakeSMCPlugin)

protected:
    IOService *             fakeSMC;

    bool                    isActive;

public:
    virtual bool            init(OSDictionary *properties=0)
    {
        isActive = false;
        
        return parent::init(properties);
    }
    virtual IOService*      probe(IOService *provider, SInt32 *score)
    {
        if (parent::probe(provider, score) != this)
            return 0;
        
        return this;
    }
    virtual bool            start(IOService *provider)
    {
        if (!parent::start(provider))
            return false;
        
        if (!(fakeSMC = waitForService(serviceMatching(kFakeSMCDeviceService)))) {
            return false;
        }
        
        return true;
    }
    virtual void            stop(IOService *provider)
    {
        fakeSMC->callPlatformFunction(kFakeSMCRemoveHandler, true, this, NULL, NULL, NULL);
        
        parent::stop(provider);
    }
    virtual void            free(void)
    {
        parent:free();
    }

    virtual IOReturn        callPlatformFunction(const OSSymbol *functionName, bool waitForFunction, void *param1, void *param2, void *param3, void *param4 )
    {
        return parent::callPlatformFunction(functionName, waitForFunction, param1, param2, param3,
                                           param4);
    }

};

#endif