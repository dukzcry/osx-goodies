/* Written by Artem Falcon <lomka@gero.in> */

/* Andigilog aSC7621(A) sensor driver */

#include "Andigilog.h"

OSDefineMetaClassAndStructors(Andigilog, FakeSMCPlugin)

bool Andigilog::init (OSDictionary* dict)
{
    bool res = super::init(dict);
    DbgPrint("init\n");
    
	if (!(sensors = OSDictionary::withCapacity(0)))
		return false;
    
    Asc7621_addr = 0; config.num_fan = config.start_fan = 0;
    
    return res;
}


void Andigilog::free(void)
{
    DbgPrint("free\n");
    
    i2cNub->close(this);
    i2cNub->release();
    
    sensors->release();
    
    super::free();
}

IOService *Andigilog::probe (IOService* provider, SInt32* score)
{
    IOService *res = super::probe(provider, score);
    DbgPrint("probe\n");
    return res;
}

bool Andigilog::start(IOService *provider)
{
    int i;
    bool res;
    UInt8 cmd, data, addrs[] = ASC7621_ADDRS;
    /* Mapping common for Intel boards */
    struct MList list[] = {
        { ASC7621_TEMP1H, ASC7621_TEMP1L, {"TC0D",TYPE_SP78,2,-1}, -1, 0, true },
        { ASC7621_TEMP2H, ASC7621_TEMP2L, {KEY_AMBIENT_TEMPERATURE,TYPE_SP78,2,-1}, -1, 0, true },
        { ASC7621_TEMP3H, ASC7621_TEMP3L, {KEY_DIMM_TEMPERATURE,TYPE_SP78,2,-1}, -1, 0, true },
        { ASC7621_TEMP4H, ASC7621_TEMP4L, {KEY_CPU_HEATSINK_TEMPERATURE,TYPE_SP78,2,-1}, -1, 0, true },
        
        { ASC7621_TACH1L, ASC7621_TACH1H, {"Fan 1",TYPE_FPE2,2,0}, true, 0, true },
        { ASC7621_TACH2L, ASC7621_TACH2H, {"Fan 2",TYPE_FPE2,2,1}, true, 0, true },
        { ASC7621_TACH3L, ASC7621_TACH3H, {"Fan 3",TYPE_FPE2,2,2}, true, 0, true },
        { ASC7621_TACH4L, ASC7621_TACH4H, {"Fan 4",TYPE_FPE2,2,2}, true, 0, true }
    };
    struct PList pwm[] = {
        { ASC7621_PWM1R, 0, -2 },
        { ASC7621_PWM2R, 0, -2 },
        { ASC7621_PWM3R, 0, -2 },
    };

    OSDictionary *conf = NULL, *sconf = OSDynamicCast(OSDictionary, getProperty("Sensors Configuration")), *dict;
    IOService *fRoot = getServiceRoot();
    OSString *str, *vendor = NULL;
    char *key; char tempkey[] = "temp ", fankey[] = "tach ";

    
    res = super::start(provider);
    DbgPrint("start\n");
    
    if (!(i2cNub = OSDynamicCast(I2CDevice, provider))) {
        IOPrint("Failed to cast provider\n");
        return false;
    }
    
    i2cNub->retain();
    i2cNub->open(this);
    
    for (i = 0; i < sizeof(addrs) / sizeof(addrs[0]); i++)
        if (!i2cNub->ReadI2CBus(addrs[i], &(cmd = ASC7621_VID_REG), sizeof(cmd), &data,
                            sizeof(data)) && data == ASC7621_VID &&
            !i2cNub->ReadI2CBus(addrs[i], &(cmd = ASC7621_PID_REG), sizeof(cmd), &data,
                            sizeof(data)) && (data == ASC7621_PID || data == ASC7621A_PID)) {
                Asc7621_addr = addrs[i];
                IOPrint("aSC7621 attached at 0x%x.\n", Asc7621_addr);
                break;
            }
    if (!Asc7621_addr) {
        IOPrint("Device matching failed.\n");
        return false;
    }
    
    memcpy(&Measures, &list, sizeof(Measures));
    memcpy(&Pwm, &pwm, sizeof(Pwm));
    
    if (fRoot) {
        vendor = vendorID(OSDynamicCast(OSString, fRoot->getProperty("oem-mb-manufacturer") ?
                                        fRoot->getProperty("oem-mb-manufacturer") :
                                        (fRoot->getProperty("oem-manufacturer") ?
                                         fRoot->getProperty("oem-manufacturer") : NULL)));
        str = OSDynamicCast(OSString, fRoot->getProperty("oem-mb-product") ?
                            fRoot->getProperty("oem-mb-product") :
                            (fRoot->getProperty("oem-product-name") ?
                             fRoot->getProperty("oem-product-name") : NULL));
    }
    if (vendor)
        if (OSDictionary *link = OSDynamicCast(OSDictionary, sconf->getObject(vendor)))
            if(str)
                conf = OSDynamicCast(OSDictionary, link->getObject(str));
    if (sconf && !conf)
        conf = OSDynamicCast(OSDictionary, sconf->getObject("Default"));
    i = 0;
    for (int s = 0, j = 0, k = 0; i < NUM_SENSORS; i++) {
        if (conf) {
                if (Measures[i].fan < 0) {
                    snprintf(&tempkey[4], 2, "%d", j++);
                    key = tempkey;
                } else {
                    snprintf(&fankey[4], 2, "%d", k++);
                    key = fankey;
                }
                if ((dict = OSDynamicCast(OSDictionary, conf->getObject(key)))) {
                    str = OSDynamicCast(OSString, dict->getObject("id"));
                    memcpy(Measures[i].hwsensor.key, str->getCStringNoCopy(), str->getLength()+1);
                    str = OSDynamicCast(OSString, dict->getObject("type"));
                    memcpy(Measures[i].hwsensor.type, str->getCStringNoCopy(), str->getLength()+1);
                    Measures[i].hwsensor.size = ((OSNumber *)OSDynamicCast(OSNumber, dict->getObject("size")))->
                        unsigned8BitValue();
                }
            if (Measures[i].fan > -1)
                    Measures[i].hwsensor.pwm = ((OSNumber *)OSDynamicCast(OSNumber, dict->getObject("pwm")))->
                    unsigned8BitValue();
        }
        if (Measures[i].hwsensor.key[0]) {
            if (Measures[i].fan < 0) {
                addSensor(Measures[i].hwsensor.key, Measures[i].hwsensor.type,
                          Measures[i].hwsensor.size, s);
                s++;
            } else {
                if (!config.start_fan)
                    config.start_fan = i;
                addTachometer(&Measures[i], Measures[i].fan = config.num_fan);
                config.num_fan++;
            }
        }
    }
    config.num_fan++;
    addKey(KEY_FAN_FORCE, TYPE_UI16, 2, 0);
  
    GetConf();
        
    return res;
}

void Andigilog::stop(IOService *provider)
{
    DbgPrint("stop\n");
    
    sensors->flushCollection();
    
    super::stop(provider);
}

/* Temp: update 'em all et once */
void Andigilog::updateSensors()
{
	UInt8 hdata, ldata;
    UInt16 data;
    
    i2cNub->LockI2CBus();
    
    for (int i = 0; i < NUM_SENSORS; i++) {
        /* Skip sensors without keys */
        if (!Measures[i].hwsensor.key[0])
            continue;
        
        Measures[i].obsoleted = false;
        if (i2cNub->ReadI2CBus(Asc7621_addr, &Measures[i].hreg, sizeof Measures[i].hreg, &hdata, sizeof hdata) ||
            i2cNub->ReadI2CBus(Asc7621_addr, &Measures[i].lreg, sizeof Measures[i].lreg, &ldata, sizeof ldata)) {
			Measures[i].value = 0;
			continue;
		}
        
        if (Measures[i].fan < 0) {
            if (hdata == ASC7621_TEMP_NA)
                Measures[i].value = 0;
            else {
                Measures[i].value = ((hdata << 8 | ldata)) >> 6;
                Measures[i].value = (float) Measures[i].value * 0.25f;
            }
        } else {
            data = hdata + (ldata << 8);
            if (!data ||
                data == 0xffff /* alarm-less value */
                )
                Measures[i].value = 0;
            else
                Measures[i].value = 5400000 / data;
        }
    }
    
    i2cNub->UnlockI2CBus();
}

void Andigilog::GetConf()
{
	UInt8 conf, val;
    
    config.pwm_mode = 0;

    /* Ask for PWM mode statuses */
    for (int i = 0; i < NUM_PWM; i++) {
        i2cNub->ReadI2CBus(Asc7621_addr, &Pwm[i].reg[0], sizeof Pwm[i].reg[0], &conf, sizeof conf);
        val = ASC7621_FANCM(conf);
            
        if (!ASC7621_ALTBG(val) && (val == 7 || (val == 3 &&
            /* PWM: 255 -> 0 */
            (conf |= 1 << ASC7621_PWM3B) && ((conf &= ~(1 << ASC7621_PWM2B) & ~(1 << ASC7621_PWM1B)) != -1) &&
            (i2cNub->WriteI2CBus(Asc7621_addr, &Pwm[i].reg[0], sizeof Pwm[i].reg[0], &conf, sizeof conf) != -1)
            /* */
            )))
            for (int j = config.start_fan; j < NUM_SENSORS; j++)
                if (Measures[j].hwsensor.pwm == i && Measures[j].hwsensor.key[0])
                    config.pwm_mode |= 1 << Measures[j].fan;
            
        Pwm[i].value = conf; /* store original conf */
    }
}

void Andigilog::SetPwmMode(UInt16 val)
{
    bool is_auto;
    bool init_pwm[NUM_PWM] = { false };
    char idx;
    UInt8 conf, zon;

	i2cNub->LockI2CBus();   
 
    for (int i = 0, j = config.start_fan; i < config.num_fan; i++, j++)
        if ((is_auto = !(val & (1 << i))) != !(config.pwm_mode & (1 << i))) {
            while (j < NUM_SENSORS && !Measures[j].hwsensor.key[0]) j++;
            /* Can't control fan not assigned with PWM */
            if (Measures[j].hwsensor.pwm < 0)
                continue;

            config.pwm_mode = is_auto ? config.pwm_mode & ~(1 << i) : config.pwm_mode | 1 << i;
            idx = Measures[j].hwsensor.pwm;
            
            if (!init_pwm[idx]) {
                conf = Pwm[idx].value;
                if (is_auto) {
                    zon = ASC7621_FANCM(conf);
                    /* No auto mode info was obtained */
                    if (!ASC7621_ALTBG(zon) && (zon == 4 || zon == 7)) {
                        /* Thermal cruise mode */
                        ASC7621_ALTBS(conf);
                        conf &= ~(1 << ASC7621_PWM3B) & ~(1 << ASC7621_PWM2B);
                        conf |= (1 << ASC7621_PWM1B);
                    }
                } else {
                    ASC7621_ALTBC(conf);
                    conf |= 1 << ASC7621_PWM3B | 1 << ASC7621_PWM2B | 1 << ASC7621_PWM1B;
                }
                i2cNub->WriteI2CBus(Asc7621_addr, &Pwm[idx].reg[0], sizeof Pwm[idx].reg[0], &conf, sizeof conf);
                init_pwm[idx] = true;
            }
        }

    i2cNub->UnlockI2CBus();
}

void Andigilog::SetPwmDuty(char idx, UInt16 val)
{
	UInt8 data;
    
    if (val < 25)
        data = 0;
    else if (val < 50)
        data = 0x40;
    else if (val < 75)
        data = 0x80;
    else if (val < 100)
        data = 0xc0;
    else
        data = 0xff;
    
    if ((Pwm[idx].duty & 0xff) == data)
        return;
    
    i2cNub->LockI2CBus();
    i2cNub->WriteI2CBus(Asc7621_addr, &Pwm[idx].reg[1], sizeof Pwm[idx].reg[1], &data, sizeof data);
    i2cNub->UnlockI2CBus();
    
    Pwm[idx].duty = data;
}

void Andigilog::readSensor(int idx)
{
    if (Measures[idx].obsoleted)
        updateSensors();
    
    Measures[idx].obsoleted = true;
}

/* FakeSMC dependend methods */
bool Andigilog::addKey(const char* key, const char* type, unsigned char size, int index)
{
	if (kIOReturnSuccess == fakeSMC->callPlatformFunction(kFakeSMCAddKeyHandler, true, (void *)key,
                                                          (void *)type, (void *)size, (void *)this)) {
		if (sensors->setObject(key, OSNumber::withNumber(index, 32))) {
            return true;
        } else {
            IOPrint("%s key not set\n", key);
            return 0;
        }
    }
    
	IOPrint("%s key not added\n", key);
    
	return 0;
}

void Andigilog::addTachometer(struct MList *sensor, int index)
{
    UInt8 length = 0;
    void * data = 0;
    
    if (kIOReturnSuccess == fakeSMC->callPlatformFunction(kFakeSMCGetKeyValue, false, (void *)KEY_FAN_NUMBER,
                                                          (void *)&length, (void *)&data, 0))
    {
        length = 0;
        
        bcopy(data, &length, 1);
        
        char name[5];
        
        snprintf(name, 5, KEY_FORMAT_FAN_SPEED, length);
        
        if (addSensor(name, sensor->hwsensor.type, sensor->hwsensor.size, index)) {
            snprintf(name, 5, KEY_FORMAT_FAN_ID, length);
                
            if (kIOReturnSuccess != fakeSMC->callPlatformFunction(kFakeSMCAddKeyValue, false, (void *)
                                                                  name, (void *)TYPE_CH8, (void *)
                                                                  ((UInt64)strlen(sensor->hwsensor.key)),
                                                                  (void *)sensor->hwsensor.key))
                    IOPrint("ERROR adding tachometer id value!\n");
            
            length++;
            
            if (kIOReturnSuccess != fakeSMC->callPlatformFunction(kFakeSMCSetKeyValue, false, (void *)KEY_FAN_NUMBER,
                                                                  (void *)1, (void *)&length, 0))
                IOPrint("ERROR updating FNum value!\n");
        }
        
        snprintf(name, 5, KEY_FORMAT_FAN_MIN_SPEED, length);
        addKey(name, TYPE_FPE2, 2, index);
        snprintf(name, 5, KEY_FORMAT_FAN_MAX_SPEED, length);
        addKey(name, TYPE_FPE2, 2, index);
    }
    else IOPrint("ERROR reading FNum value!\n");
}
/* */

/* Exports for HWSensors4 */
IOReturn Andigilog::callPlatformFunction(const OSSymbol *functionName, bool waitForFunction,
                                         void *param1, void *param2, void *param3, void *param4 )
{
    int i, idx = -1;
    char fan = -1;
    
    if (functionName->isEqualTo(kFakeSMCGetValueCallback)) {
        const char* key = (const char*)param1;
		char * data = (char*)param2;
        
        if (key && data) {
                    if (key[0] == 'T' || (key[0] == KEY_FORMAT_FAN_SPEED[0] &&
                                          key[2] == KEY_FORMAT_FAN_SPEED[3] &&
                                          (fan = strtol(&key[1], NULL, 10)) != -1)) {
                        for (i = 0; i < NUM_SENSORS; i++)
                            if (Measures[i].fan == fan && Measures[i].hwsensor.key[0])
                                    if (fan >= 0 || (Measures[i].hwsensor.key[1] == key[1] &&
                                        Measures[i].hwsensor.key[2] == key[2] &&
                                        Measures[i].hwsensor.key[3] == key[3])) {
                                            idx = i; break;
                                    }
                        
                        if (idx > -1) {
                            readSensor(idx);
                            if (fan >= 0 && (*((uint32_t*)&Measures[idx].hwsensor.type) == *((uint32_t*)TYPE_FPE2)))
                                Measures[idx].value = encode_fpe2(Measures[idx].value);
                            memcpy(data, &Measures[idx].value, Measures[idx].hwsensor.size);
                            return kIOReturnSuccess;
                        }
                    }
                    else if (key[0] == 'F') {
                        if (key[1] == KEY_FAN_FORCE[1]) {
                            /* Return real states */
                            memcpy(data, &(idx = swap_value(config.pwm_mode)), 2);
                            return kIOReturnSuccess;
                        } else if (key[2] == 'M' &&
                                   (key[3] == KEY_FORMAT_FAN_MIN_SPEED[4] || key[3] == KEY_FORMAT_FAN_MAX_SPEED[4])) {
                            fan = strtol(&key[1], NULL, 10);
                            for (i = config.start_fan; i < NUM_SENSORS; i++) {
                                if (Measures[i].fan == fan && Measures[i].hwsensor.key[0]) {
                                    idx = i; break;
                                }
                            }
                            if (idx > -1) {
                            if (key[3] == KEY_FORMAT_FAN_MAX_SPEED[4]) {
                                if (Measures[idx].hwsensor.pwm > -1)
                                    memcpy(data, &(idx = 0x9001), 2); /* PWM % */
                            }
                            else if (Measures[idx].hwsensor.pwm < 0) /* MIN_SPEED */
                                    memset(data, 0, 2);
                            }
                            return kIOReturnSuccess;
                        }
                    }
        }
        return kIOReturnBadArgument;
    }
    if (functionName->isEqualTo(kFakeSMCSetValueCallback)) {
        const char* key = (const char*)param1;
		char * data = (char*)param2;
        
        if (key[0] == 'F' && data) {
                if (key[2] == KEY_FORMAT_FAN_MIN_SPEED[3] && key[3] == KEY_FORMAT_FAN_MIN_SPEED[4]) {
                    if (config.pwm_mode & (1 << (fan = strtol(&key[1], NULL, 10)))) {
                        for (i = config.start_fan; i < NUM_SENSORS; i++) {
                            if (Measures[i].fan == fan && Measures[i].hwsensor.key[0]) {
                                idx = i; break;
                            }
                        };
                        if (idx > -1 && Measures[idx].hwsensor.pwm > -1)
                            SetPwmDuty(Measures[idx].hwsensor.pwm, decode_fpe2(*((UInt16 *) data)));
                    }
                    return kIOReturnSuccess;
                }
                else if(key[1] == KEY_FAN_FORCE[1]) {
                    SetPwmMode(swap_value(*((UInt16 *) data)));
                    return kIOReturnSuccess;
                }
        }
        return kIOReturnBadArgument;
    }
    return super::callPlatformFunction(functionName, waitForFunction, param1, param2, param3, param4);
}
/* */
