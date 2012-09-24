#include "I2CCommon.h"
#include "FakeSMCPlugin.h"

#define drvid "[Andigilog] "
#define super FakeSMCPlugin

#ifdef ASC_DEBUG
#define DbgPrint(arg...) IOLog(drvid arg)
#else
#define DbgPrint(arg...) ;
#endif
#define IOPrint(arg...) IOLog(drvid arg)

#define ASC7621A_ADDR 0x2e /* Ugly, binded to absolute address for now */

#define ASC7621_VID_REG 0x3e
#define ASC7621_PID_REG 0x3f
#define ASC7621_VID     0x61
#define ASC7621A_PID    0x6d

#define NUM_SENSORS	8
#define ASC7621_TEMP1H		0x25
#define ASC7621_TEMP1L		0x10
#define ASC7621_TEMP2H		0x26
#define ASC7621_TEMP2L		0x15
#define ASC7621_TEMP3H		0x27
#define ASC7621_TEMP3L		0x16
#define ASC7621_TEMP4H		0x33
#define ASC7621_TEMP4L		0x17
#define ASC7621_TEMP_NA		0x80
#define ASC7621_TACH1H		0x29
#define ASC7621_TACH1L		0x28
#define ASC7621_TACH2H		0x2b
#define ASC7621_TACH2L		0x2a
#define ASC7621_TACH3H		0x2d
#define ASC7621_TACH3L		0x2c
#define ASC7621_TACH4H		0x2f
#define ASC7621_TACH4L		0x2e

class Andigilog: public FakeSMCPlugin
{
    OSDeclareDefaultStructors(Andigilog)
private:
    I2CDevice *i2cNub;
    struct MList {
        UInt8	hreg;			/* MS-byte */
        UInt8	lreg;			/* LS-byte */
        struct {
            char	key[16];
            char    type[5];
            unsigned char size;
        } hwsensor;
        int	fan;
        
        SInt64 value;
        bool obsoleted;
    } Measures[NUM_SENSORS];
    
    OSDictionary *	sensors;
    
    
    void updateSensors();
    void readSensor(int);
    
    bool addSensor(const char* key, const char* type, unsigned char size, int index);
    void addTachometer(struct MList *, int);
protected:
    virtual bool    init (OSDictionary* dictionary = NULL);
    virtual void    free (void);
    virtual IOService*      probe (IOService* provider, SInt32* score);
    virtual bool    start (IOService* provider);
    virtual void    stop (IOService* provider);
    
    virtual IOReturn	callPlatformFunction(const OSSymbol *functionName, bool waitForFunction,
                                             void *param1, void *param2, void *param3, void *param4 );
};