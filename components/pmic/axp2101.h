#ifndef AXP2101_H
#define AXP2101_H

#define APX2101_REG_MON_VBAT_BASE   0x34
#define APX2101_REG_MON_TS_BASE     0x36
#define APX2101_REG_MON_VBUS_BASE   0x38
#define APX2101_REG_MON_VSYS_BASE   0x3A
#define APX2101_REG_MON_TDIE_BASE   0x3C

#define APX2101_REG_CHARGE_EN   0x18
#define APX2101_REG_SLEEP_WAKE  0x26
#define APX2101_REG_LEVEL_DUR   0x27
#define APX2101_REG_ADC_EN      0x30
#define APX2101_REG_MAIN_CHARGE_VOLT 0x64
#define APX2101_REG_CHGLED_CNT  0x69
#define APX2101_REG_BUTTON_TERM_VOLT 0x6A
#define APX2101_REG_DC1_VOLT        0x82

#define APX2101_REG_LDO_EN0     0x90
#define APX2101_REG_LDO_EN1     0x91
#define APX2101_REG_ALDO3_VOLT  0x94
#define APX2101_REG_ALDO4_VOLT  0x95

// dc/dc voltage settings. precalculated as (reg = (targ-1.5)/0.1)
#define APX2101_DCDC_VOLT_3V3     18

// a-ldo voltage settings. precalculated as (reg = (targ-0.5)/0.1)
#define APX2101_ALDO_VOLT_3V3     29

#endif