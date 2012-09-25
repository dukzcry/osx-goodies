/* Written by Artem Falcon <lomka@gero.in> */

/* Andigilog ASC7621(a) sensor driver */

#include "Andigilog.h"

OSDefineMetaClassAndStructors(Andigilog, FakeSMCPlugin)

bool Andigilog::init (OSDictionary* dict)
{
    bool res = super::init(dict);
    DbgPrint("init\n");
    
	if (!(sensors = OSDictionary::withCapacity(0)))
		return false;
    
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
    UInt8 cmd, data;
    UInt8 addrs[] = ASC7621_ADDRS;
    /* Point keys for me, me, me, me */
    struct MList list[] = {
        { ASC7621_TEMP1H, ASC7621_TEMP1L, {"",0,0}, -1, 0, true },
        { ASC7621_TEMP2H, ASC7621_TEMP2L, {"",0,0}, -1, 0, true },
        { ASC7621_TEMP3H, ASC7621_TEMP3L, {KEY_DIMM_TEMPERATURE,TYPE_SP78,2}, -1, 0, true },
        { ASC7621_TEMP4H, ASC7621_TEMP4L, {KEY_CPU_HEATSINK_TEMPERATURE,TYPE_SP78,2}, -1, 0, true },
        
        { ASC7621_TACH1L, ASC7621_TACH1H, {"",0,0}, true, 0, true },
        { ASC7621_TACH2L, ASC7621_TACH2H, {"PCI cage",TYPE_FPE2,2}, true, 0, true },
        { ASC7621_TACH3L, ASC7621_TACH3H, {"Memory",TYPE_FPE2,2}, true, 0, true },
        { ASC7621_TACH4L, ASC7621_TACH4H, {"CPU, 2x",TYPE_FPE2,2}, true, 0, true }
    };
    struct PList pwm[] = {
        { ASC7621_PWM1R /*,ASC7621_PWM1B*/ },
        { ASC7621_PWM2R /*,ASC7621_PWM2B*/ },
        { ASC7621_PWM3R /*,ASC7621_PWM3B*/ },
    };
    
    res = super::start(provider);
    DbgPrint("start\n");
    
    if (!(i2cNub = OSDynamicCast(I2CDevice, provider))) {
        IOPrint("Failed to cast provider\n");
        return false;
    }
    
    i2cNub->retain();
    i2cNub->open(this);
    
    Asc7621_addr = 0;
    for (i = 0; i < sizeof(addrs) / sizeof(addrs[0]); i++)
        if (!i2cNub->ReadI2CBus(addrs[i], &(cmd = ASC7621_VID_REG), sizeof(cmd), &data,
                            sizeof(data)) && data == ASC7621_VID &&
            !i2cNub->ReadI2CBus(addrs[i], &(cmd = ASC7621_PID_REG), sizeof(cmd), &data,
                            sizeof(data)) && (data == ASC7621_PID || data == ASC7621A_PID)) {
                Asc7621_addr = addrs[i];
                IOPrint("ASC7621 attached at 0x%x.\n", Asc7621_addr);
                break;
            }
    if (!Asc7621_addr) {
        IOPrint("Device matching failed.\n");
        return false;
    }
    
    memcpy(&Measures, &list, sizeof(Measures));
    memcpy(&Pwm, &pwm, sizeof(Pwm));
    i = 0;
    for (int s = 0, f = 0; i < NUM_SENSORS; i++)
        if (Measures[i].hwsensor.key[0]) {
            if (Measures[i].fan < 0) {
                addSensor(Measures[i].hwsensor.key, Measures[i].hwsensor.type,
                          Measures[i].hwsensor.size, s);
                s++;
            } else {
                addTachometer(&Measures[i], Measures[i].fan = f);
                f++;
            }
        }
    addKey(KEY_FAN_FORCE, TYPE_UI16, 2, 0);
    
    GetConf();
        
    return res;
}

void Andigilog::stop(IOService *provider)
{
    DbgPrint("stop\n");
    
    //fakeSMC->callPlatformFunction(kFakeSMCRemoveHandler, true, this, NULL, NULL, NULL);
    
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
            if (data == 0 || data == 0xffff)
                Measures[i].value = 0;
            else
                Measures[i].value = 5400000 / data;
        }
    }
    
    i2cNub->UnlockI2CBus();
}

void Andigilog::GetConf()
{
	UInt8 conf, val /*,offmin*/;
    
    config.pwm_mode = 0;

    i2cNub->LockI2CBus();
    
    /* Ask for PWM mode statuses */
    /*if (i2cNub->ReadI2CBus(Asc7621_addr, &(conf = ASC7621_OFFMINR), sizeof conf, &offmin, sizeof offmin))
        return;*/
    for (int i = 0; i < NUM_PWM; i++) {
        i2cNub->ReadI2CBus(Asc7621_addr, &Pwm[i].reg, sizeof Pwm[i].reg, &conf, sizeof conf);
        val = ASC7621_FANCM(conf);
            
        //(offmin >> Pwm[i].shift) & 0x01);
        if (!ASC7621_ALTBG(val) && (val == 7 || (val == 3 &&
            /* PWM: 255 -> 0 */
            (conf |= 1 << ASC7621_PWM3B) && ((conf &= ~(1 << ASC7621_PWM2B) & ~(1 << ASC7621_PWM1B)) != -1) &&
            (i2cNub->WriteI2CBus(Asc7621_addr, &Pwm[i].reg, sizeof Pwm[i].reg, &conf, sizeof conf) != -1)
            /* */
            )))
                config.pwm_mode |= 1 << i;
            
        Pwm[i].reg = conf; /* store original conf */
    }
    
    i2cNub->UnlockI2CBus();
}

void Andigilog::SetPwmMode(UInt16 val)
{
    bool is_auto;
    UInt8 conf, zon;
    
    for (int i = 0; i < NUM_PWM; i++)
        if ((is_auto = !(val & (1 << i))) != !(config.pwm_mode & (1 << i))) {
            conf = Pwm[i].reg;
            if (is_auto) {
                zon = ASC7621_FANCM(conf);
                /* No auto mode info was obtained */
                if (!ASC7621_ALTBG(zon) && (zon == 4 || zon == 7)) {
                    /* Thermal cruise mode */
                    ASC7621_ALTBS(conf);
                    conf &= ~(1 << ASC7621_PWM3B) & ~(1 << ASC7621_PWM2B);
                    conf |= (1 << ASC7621_PWM1B);
                }
                config.pwm_mode &= ~(1 << i);
            }
            else {
                conf |= 1 << ASC7621_PWM3B | 1 << ASC7621_PWM2B | 1 << ASC7621_PWM1B;
                config.pwm_mode |= 1 << i;
            }
            i2cNub->WriteI2CBus(Asc7621_addr, &Pwm[i].reg, sizeof Pwm[i].reg, &conf, sizeof conf);
        }
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
    }
    else IOPrint("ERROR reading FNum value!\n");
}
/* */

/* Exports for HWSensors4 */
IOReturn Andigilog::callPlatformFunction(const OSSymbol *functionName, bool waitForFunction,
                                         void *param1, void *param2, void *param3, void *param4 )
{
    if (functionName->isEqualTo(kFakeSMCGetValueCallback)) {
        const char* key = (const char*)param1;
		char * data = (char*)param2;
        
        if (key && data) {
            if (OSNumber *number = OSDynamicCast(OSNumber, sensors->getObject(key))) {
                UInt32 index = number->unsigned16BitValue();
                
                if (index < NUM_SENSORS) {
                    char fan = -1;
                    if (key[0] == 'T' || (key[0] == KEY_FORMAT_FAN_SPEED[0] &&
                                          key[2] == KEY_FORMAT_FAN_SPEED[3] &&
                                          (fan = strtol(&key[1], NULL, 10)) != -1)) {
                        int idx = -1;
                        for (int i = 0; i < NUM_SENSORS; i++)
                            if (Measures[i].hwsensor.key[0] && Measures[i].fan == fan)
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
                        }
                    }
                    else if (key[0] == KEY_FAN_FORCE[0] && key[1] == KEY_FAN_FORCE[1])
                        memcpy(data, &(index = swap_value(config.pwm_mode)), 2);
                    return kIOReturnSuccess;
                }
            }
            return kIOReturnBadArgument;
        }
        return kIOReturnBadArgument;
    }
    if (functionName->isEqualTo(kFakeSMCSetValueCallback)) {
        const char* key = (const char*)param1;
		char * data = (char*)param2;
        
        if (key && data) {
            if (OSNumber *number = OSDynamicCast(OSNumber, sensors->getObject(key))) {
                UInt32 index = number->unsigned16BitValue();
                
                if (index < NUM_SENSORS) {
                    if (key[0] == KEY_FAN_FORCE[0] && key[1] == KEY_FAN_FORCE[1])
                        SetPwmMode(swap_value(*((UInt16 *) data)));
                    return kIOReturnSuccess;
                }
            }
            return kIOReturnBadArgument;
        }
        return kIOReturnBadArgument;
    }
    return super::callPlatformFunction(functionName, waitForFunction, param1, param2, param3, param4);
}
/* */