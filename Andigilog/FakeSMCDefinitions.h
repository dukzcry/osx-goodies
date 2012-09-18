//
//  FakeSMCDefinitions.h
//  HWSensors
//
//  Created by mozo on 20.10.11.
//  Copyright (c) 2011 mozodojo. All rights reserved.
//

#ifndef HWSensors_definitions_h
#define HWSensors_definitions_h

// Temperature (*C)
// CPU
#define KEY_FORMAT_CPU_DIE_CORE_TEMPERATURE     "TC%XC" // CPU Core %X Die Digital
#define KEY_FORMAT_CPU_DIODE_TEMPERATURE        "TC%XD" // CPU Core %X Die Analog
#define KEY_CPU_HEATSINK_TEMPERATURE            "Th0H"
#define KEY_CPU_PROXIMITY_TEMPERATURE           "TC0P"
// GPU
#define KEY_FORMAT_GPU_DIODE_TEMPERATURE        "TG%XD"
#define KEY_FORMAT_GPU_BOARD_TEMPERATURE        "TG%XH"
#define KEY_FORMAT_GPU_PROXIMITY_TEMPERATURE    "TG%XP"
#define KEY_GPU_MEMORY_TEMPERATURE              "TG0M"
// PECI
#define KEY_FORMAT_CPU_PECI_CORE_TEMPERATURE    "TC%Xc" // SNB
#define KEY_PECI_GFX_TEMPERATURE                "TCGc"  // SNB HD2/3000
#define KEY_PECI_SA_TEMPERATURE                 "TCSc"  // SNB 
#define KEY_PECI_PACKAGE_TEMPERATURE            "TCXc"  // SNB 
// NorthBridge, MCH, MCP, PCH
#define KEY_MCH_DIODE_TEMPERATURE               "TN0C"
#define KEY_MCH_HEATSINK_TEMPERATURE            "TN0H"
#define KEY_MCP_DIE_TEMPERATURE                 "TN0D"
#define KEY_MCP_INTERNAL_DIE_TEMPERATURE        "TN1D"
#define KEY_MCP_PROXIMITY_TEMPERATURE           "TM0P" // MCP Proximity/Inlet
#define KEY_NORTHBRIDGE_TEMPERATURE             "TN0P" // MCP Proximity Top Side
#define KEY_NORTHBRIDGE_PROXIMITY_TEMPERATURE   "TN1P"
#define KEY_PCH_DIE_TEMPERATURE                 "TPCD" // SNB PCH Die Digital
#define KEY_PCH_PROXIMITY_TEMPERATURE           "TP0P" // SNB
// Misc
#define KEY_ACDC_TEMPERATURE                    "Tp0C" // PSMI Supply AC/DC Supply 1
#define KEY_AMBIENT_TEMPERATURE                 "TA0P"
#define KEY_AMBIENT1_TEMPERATURE                "TA1P"
#define KEY_DIMM_TEMPERATURE                    "Tm0P" // MLB Proximity/DC In Proximity Airflow
#define KEY_AIRVENT_TEMPERATURE                 "TV0P" // Air Vent Exit
#define KEY_AIRPORT_TEMPERATURE                 "TW0P"

// Voltage (Volts)
// CPU
#define KEY_CPU_VOLTAGE                         "VC0C" // CPU 0 Core
#define KEY_CPU_VOLTAGE_RAW                     "VC0c" // CPU 0 V-Sense
#define KEY_CPU_VCCIO_VOLTAGE                   "VC1C" // CPU VccIO PP1V05
#define KEY_CPU_VCCSA_VOLTAGE                   "VC2C" // CPU VCCSA
#define KEY_CPU_DRAM_VOLTAGE                    "VC5R" // VCSR-DIMM 1.5V S0
#define KEY_CPU_PLL_VOLTAGE                     "VC8R" // CPU 1.8V S0
#define KEY_CPU_VRM_SUPPLY0                     "VS0C"
#define KEY_CPU_VRM_SUPPLY1                     "VS1C"
#define KEY_CPU_VRM_SUPPLY2                     "VS2C"

// GPU
#define KEY_GPU0_VOLTAGE                        "VC0G" // GPU 0 Core
#define KEY_GPU1_VOLTAGE                        "VC1G" // GPU 1 Core

#define KEY_PCH_VOLTAGE                         "VN1C" // PCH 1.05V S0, VS1C-PP1V05 S0 SB, VV1R-1.05 S0

//#define KEY_POWER_BATTERY_VOLTAGE             "VP0R" // Power/Battery (iStat)
#define KEY_DCIN_12V_S0_VOLTAGE                 "VDPR" // VD2R-Power Supply 12V S0 VD0R DC In VDPR AC/DC
#define KEY_DCIN_3V3_S5_VOLTAGE                 "VS8C" // PP3V3 S5 SB

#define KEY_MEMORY_VOLTAGE                      "VM0R" 
#define KEY_12V_VOLTAGE                         "VP0R" //"Vp0C"
#define KEY_N12VC_VOLTAGE                       "Vp0C"
#define KEY_5VC_VOLTAGE                         "Vp1C"
#define KEY_5VSB_VOLTAGE                        "Vp2C"
#define KEY_3VCC_VOLTAGE                        "Vp3C"
#define KEY_3VSB_VOLTAGE                        "Vp4C"
#define KEY_VBAT_VOLTAGE                        "VBAT"


#define KEY_NORTHBRIDGE_VOLTAGE                 "VN0C"

// Current (Amps)
#define KEY_CPU_CURRENT                         "IC0C" // CPU 0 Core
#define KEY_CPU_CURRENT_RAW                     "IC0c" // CPU 0 I-Sense
#define KEY_CPU_VCCIO_CURRENT                   "IC1C" // CPU VccIO PP1V05
#define KEY_CPU_VCCSA_CURRENT                   "IC2C" // CPU VCCSA
#define KEY_CPU_DRAM_CURRENT                    "IC5R" // VCSR-DIMM 1.5V S0
#define KEY_CPU_PLL_CURRENT                     "IC8R" // CPU 1.8V S0

#define KEY_CPU_VCORE_VTT_CURRENT               "IC0R"

// Power (Watts)
#define KEY_CPU_PACKAGE_CORE                    "PCPC" // SNB
#define KEY_CPU_PACKAGE_GFX                     "PCPG" // SNB
#define KEY_CPU_PACKAGE_TOTAL                   "PCPT" // SNB

// FAN's
#define KEY_FAN_NUMBER                          "FNum"
#define KEY_FORMAT_FAN_ID                       "F%XID"
#define KEY_FORMAT_FAN_SPEED                    "F%XAc"

// Other
#define KEY_FORMAT_NON_APPLE_CPU_FREQUENCY      "FRC%X"
#define KEY_FORMAT_NON_APPLE_CPU_MULTIPLIER     "MC%XC"
#define KEY_FORMAT_NON_APPLE_GPU_FREQUENCY      "FGC%X"

#define KEY_NON_APPLE_PACKAGE_MULTIPLIER        "MPkC"


//SmartGuardian keys
#define KEY_FORMAT_FAN_MAIN_CONTROL             "FMCL"
#define KEY_FORMAT_FAN_REG_CONTROL              "FMCR"

#define KEY_FORMAT_FAN_TARGET_SPEED             "F%dTg"
// Old bad legacy naming but i have to keep it actually means

//￼￼￼￼￼￼Bit - 7 R/W Auto/Manual mode selection: 0 - software control, 1 - automatic chip control
//Bits 6-0 R/W Software PWM value... see description below
//FAN PWM mode Automatic/Software Operation Selection
//0: Software operation. 1: Automatic operation.
//128 steps of PWM control when in Software operation (bit 7=0), or Temperature input selection when in Automatic operation (bit 7=1). Bits[1:0]: - select temperature sensor to control the fan
//00: TMPIN1
//01: TMPIN2 
//10: TMPIN3 
//11: Reserved

//Smart guardian software mode. The fan speed will be determined by the PWM value entered into a register in it8718f by a software program. The pwm value is stored in bits 6-0 of a register. This is 0 for stopped and 127 for full speed.

//Smart guardian Automatic mode. The fan speed will be determined by the values in the it8718f registers.
#define KEY_FORMAT_FAN_MIN_SPEED                "F%dMn"
#define KEY_FORMAT_FAN_MAX_SPEED                "F%dMx"
#define KEY_FAN_FORCE                           "FS! "


#define KEY_FORMAT_FAN_START_TEMP               "F%dSt"
//start temperature, At this temperature the fan will start with the start pwm value. 
#define KEY_FORMAT_FAN_OFF_TEMP                 "F%dSs"
//off temperature, At temperatures below this value the fan pwm value will be 0.   Usually 0 degrees is default value
#define KEY_FORMAT_FAN_FULL_TEMP                "F%dFt"
//Temperature limit when fan will run at max speed/PWM
#define KEY_FORMAT_FAN_START_PWM                "F%dPt"
//start PWM value, At start temperature this is the pwm value the fan will be running at. 
//Bit 7 - R/W Slope PWM bit[6]
//Please refer to the description of SmartGuardian Automatic Mode Control Register
//Bits 6-0 R/W Start PWM Value
#define KEY_FORMAT_FAN_TEMP_DELTA               "F%dFo"
//￼￼￼￼￼￼￼￼￼￼￼￼￼￼￼￼￼￼￼￼￼￼￼￼￼￼￼￼￼￼￼￼￼Bit - 7 R/W Direct-Down Control
//This bit selects the PWM linear changing decreasing mode. 0: Slow decreasing mode. 1: Direct decreasing mode.
//Bits 6-5 - reserved
//￼￼￼￼￼￼￼Bit - ￼4-0 R/W  delta-Temperature interval [4:0].
//Direct-down control,  Direct decreasing mode. As temperature decreases the pwm value             will decrease  by the slope pwm value for each degree decrease.  Slow decreasing mode. As temperature decreases the pwm value will not decrease  until the temperature has decreased the value of  temperature interval. Then it will decrease by the slope pwm value.
//temperature interval, In Slow decreasing mode this is the value temperature has to decrease before  pwm value will decrease by slope pwm value. This is a 5 bit value, bits 4-0 of a register.

#define KEY_FORMAT_FAN_CONTROL                  "F%dCt"
//Bit 7 R/W FAN Smoothing
//This bit enables the FAN PWM smoothing changing. 0: Disable
// 1: Enable
//Bit 6 R/W Reserved
//R/W Slope PWM bit[5:0]
//Slope = (Slope PWM bit[6:3] + Slope PWM bit[2:0] / 8) PWM value/°C 
//slope PWM value, At temperatures above start temperature, for each degree increase in 
//temperature, pwm value will increase by slope PWM value. This is an 7 bit value. 4 bits for 
//the whole number part and 3 bits  for the fractional part. this can be from 0 0/8 to 15 7/8.
//The fractional part is bits 2-0 of a register.The whole number part is bits 5-3 of a register 
//and bit 7 of another register.

#define KEY_FORMAT_FAKESMC_GPU_FREQUENCY        "CG%XP"
#define KEY_FORMAT_FAKESMC_GPU_MEMORY_FREQUENCY "CG%XM"
#define KEY_FORMAT_FAKESMC_GPU_SHADER_FREQUENCY "CG%XS"
#define KEY_FORMAT_FAKESMC_GPU_ROP_FREQUENCY    "CG%XR"


// Types
#define TYPE_FPE2                               "fpe2"
#define TYPE_FP2E                               "fp2e"
#define TYPE_FP4C                               "fp4c"
#define TYPE_CH8                                "ch8*"
#define TYPE_SP78                               "sp78"
#define TYPE_UI8                                "ui8"
#define TYPE_UI16                               "ui16"
#define TYPE_UI32                               "ui32"
#define TYPE_SI16                               "si16"
#define TYPE_FLAG                               "flag"
#define TYPE_FREQ                               "freq"

// Protocol
#define kFakeSMCDeviceService                   "FakeSMCDevice"
#define kFakeSMCDeviceValues                    "Values"
#define kFakeSMCDeviceUpdateKeyValue            "updateKeyValue"
#define kFakeSMCDevicePopulateValues            "populateValues"
#define kFakeSMCDeviceKeysList                  "KeysList"
#define kFakeSuperIOMonitorModel                "Model"

#define kFakeSMCAddKeyValue                     "FakeSMC_AddKeyValue"
#define kFakeSMCAddKeyHandler                   "FakeSMC_AddKeyHandler"
#define kFakeSMCSetKeyValue                     "FakeSMC_SetKeyValue"
#define kFakeSMCGetKeyValue                     "FakeSMC_GetKeyValue"
#define kFakeSMCGetValueCallback                "FakeSMC_GetValueCallback"
#define kFakeSMCSetValueCallback                "FakeSMC_SetValueCallback"
#define kFakeSMCRemoveHandler                   "FakeSMC_RemoveHandler"

#endif
