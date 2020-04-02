/*
 * Battery charger driver for X-POWERS AXP259X
 *
 * Copyright (C) 2013 X-POWERS Ltd.
 *  Weijin Zhong <zhwj@x-powers.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/wakelock.h>

#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/slab.h>

#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/input.h>
#include <linux/mfd/axp-mfd.h>
#include <asm/div64.h>

#include <mach/sys_config.h>
#include <linux/arisc/arisc.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include "../axp-cfg.h"
#include "axp259-sply.h"

#define DBG_AXP_PSY 1
#if  DBG_AXP_PSY
#define DBG_PSY_MSG(format, args...)   printk(KERN_DEBUG "[AXP]"format, ##args)
#else
#define DBG_PSY_MSG(format, args...)   do {} while (0)
#endif

static uint8_t g_charge_done_fix_flag;
#define CHARGE_DONE_RECHARGE            (0x01)
#define CHARGE_DONE_END                 (0x02)
#define CHARGE_DONE_END_RECHARGE        (0x03)

static int axp_debug;
static int reg_debug;
static int long_key_power_off = 1;
static struct axp_adc_res adc;
#ifdef AXP259_WITH_USB
static struct delayed_work usbwork;
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend axp_early_suspend;
int early_suspend_flag;
#endif
#ifdef CONFIG_HAS_WAKELOCK
static struct wake_lock axp_wakeup_lock;
#endif

#ifdef AXP259_WITH_USB
static aw_charge_type axp_usbcurflag = CHARGE_AC;
static aw_charge_type axp_usbvolflag = CHARGE_AC;
#endif
static int axp_power_key;
static DEFINE_SPINLOCK(axp_powerkey_lock);

void axp_powerkey_set(int value)
{
	spin_lock(&axp_powerkey_lock);
	axp_power_key = value;
	spin_unlock(&axp_powerkey_lock);
}
EXPORT_SYMBOL_GPL(axp_powerkey_set);

int axp_powerkey_get(void)
{
	int value;

	spin_lock(&axp_powerkey_lock);
	value = axp_power_key;
	spin_unlock(&axp_powerkey_lock);

	return value;
}
EXPORT_SYMBOL_GPL(axp_powerkey_get);

#ifdef AXP259_WITH_USB
int axp_usbvol(aw_charge_type type)
{
	axp_usbvolflag = type;
	return 0;
}
EXPORT_SYMBOL_GPL(axp_usbvol);

int axp_usbcur(aw_charge_type type)
{
	axp_usbcurflag = type;
	mod_timer(&axp_charger->usb_status_timer,
			jiffies + msecs_to_jiffies(0));
	return 0;
}
EXPORT_SYMBOL_GPL(axp_usbcur);

int axp_usb_det(void)
{
	uint8_t ret = 0;

	if (axp_charger == NULL || axp_charger->master == NULL)
		return ret;
	axp_read(axp_charger->master, AXP259_CHARGE_STATUS, &ret);
	if (ret & 0x10)		/*usb or usb adapter can be used */
		return 1;
	else			/*no usb or usb adapter */
		return 0;
}
EXPORT_SYMBOL_GPL(axp_usb_det);

#else
int axp_usbvol(aw_charge_type type)
{
	return 0;
}
EXPORT_SYMBOL_GPL(axp_usbvol);

int axp_usbcur(aw_charge_type type)
{
	return 0;
}
EXPORT_SYMBOL_GPL(axp_usbcur);

int axp_usb_det(void)
{
	return 0;
}
EXPORT_SYMBOL_GPL(axp_usb_det);

#endif
static ssize_t
axpdebug_store(struct class *class,
			struct class_attribute *attr,
			const char *buf, size_t count)
{
	if (buf[0] == '1')
		axp_debug = 1;
	else
		axp_debug = 0;
	return count;
}

static ssize_t
axpdebug_show(struct class *class,
			struct class_attribute *attr,
			char *buf)
{
	return sprintf(buf, "bat-debug value is %d\n", axp_debug);
}

static ssize_t
axp_regdebug_store(struct class *class,
				struct class_attribute *attr,
				const char *buf, size_t count)
{
	int var;
	if (kstrtoul(buf, 16, (unsigned long *)&var))
		return 0;
	if (var)
		reg_debug = var;
	else
		reg_debug = 0;
	return count;
}

static ssize_t
axp_regdebug_show(struct class *class,
				struct class_attribute *attr,
				char *buf)
{
	return sprintf(buf, "reg-debug value is 0x%x\n", reg_debug);
}

void axp_reg_debug(int reg, int len, uint8_t *val)
{
	int i = 0;
	if (reg_debug != 0) {
		for (i = 0; i < len; i++) {
			if (reg + i == reg_debug)
				printk(KERN_ERR "###***axp_reg 0x%x write value 0x%x\n",
				reg_debug, *(val + i));
		}
	}
	return;
}

#ifdef AXP259_WITH_USB
static ssize_t
vbuslimit_store(struct class *class,
			struct class_attribute *attr,
			const char *buf, size_t count)
{
	if (buf[0] == '1')
		vbus_curr_limit_debug = 1;
	else
		vbus_curr_limit_debug = 0;
	return count;
}

static ssize_t
vbuslimit_show(struct class *class,
			struct class_attribute *attr,
			char *buf)
{
	return sprintf(buf, "vbus curr limit value is %d\n",
					vbus_curr_limit_debug);
}
#endif
/*for long key power off control added by zhwj at 20130502*/
static ssize_t
longkeypoweroff_store(struct class *class,
				struct class_attribute *attr,
				const char *buf, size_t count)
{
	int addr;
	uint8_t data;
	if (buf[0] == '1')
		long_key_power_off = 1;
	else
		long_key_power_off = 0;
	/*for long key power off control added by zhwj at 20130502 */
	addr = AXP259_POK_SET;
	axp_read(axp_charger->master, addr, &data);
	data &= 0xf7;
	data |= (long_key_power_off << 3);
	axp_write(axp_charger->master, addr, data);
	return count;
}

static ssize_t
longkeypoweroff_show(struct class *class,
					struct class_attribute *attr,
					char *buf)
{
	return sprintf(buf, "long key power off value is %d\n",
					long_key_power_off);
}

static ssize_t
out_factory_mode_show(struct class *class,
					struct class_attribute *attr,
					char *buf)
{
	uint8_t addr = AXP259_BUFFERB;
	uint8_t data;
	axp_read(axp_charger->master, addr, &data);
	return sprintf(buf, "0x%x\n", data);
}

static ssize_t
out_factory_mode_store(struct class *class,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	uint8_t addr = AXP259_BUFFERB;
	uint8_t data;
	int var;
	if (kstrtoul(buf, 10, (unsigned long *)&var))
		return 0;
	if (var) {
		data = 0x0d;
		axp_write(axp_charger->master, addr, data);
	} else {
		data = 0x00;
		axp_write(axp_charger->master, addr, data);
	}
	return count;
}

static struct class_attribute axppower_class_attrs[] = {
#ifdef AXP259_WITH_USB
	__ATTR(vbuslimit, S_IRUGO|S_IWUSR, vbuslimit_show, vbuslimit_store),
#endif
	__ATTR(axpdebug, S_IRUGO | S_IWUSR, axpdebug_show, axpdebug_store),
	__ATTR(regdebug, S_IRUGO | S_IWUSR,
		axp_regdebug_show, axp_regdebug_store),
	__ATTR(longkeypoweroff, S_IRUGO | S_IWUSR,
			longkeypoweroff_show, longkeypoweroff_store),
	__ATTR(out_factory_mode, S_IRUGO | S_IWUSR,
			out_factory_mode_show, out_factory_mode_store),
	__ATTR_NULL
};

static struct class axppower_class = {
	.name = "axppower",
	.class_attrs = axppower_class_attrs,
};

static inline int axp259_vts_to_temp(int data)
{
	int temp;
	if (data < 80)
		return 30;
	else if (data < axp259_config.pmu_temp_para16)
		return 80;
	else if (data <= axp259_config.pmu_temp_para15) {
		temp = 70 + (axp259_config.pmu_temp_para15 - data) * 10 \
		/ (axp259_config.pmu_temp_para15 \
		- axp259_config.pmu_temp_para16);
	} else if (data <= axp259_config.pmu_temp_para14) {
		temp = 60 + (axp259_config.pmu_temp_para14 - data) * 10 \
		/ (axp259_config.pmu_temp_para14 \
		- axp259_config.pmu_temp_para15);
	} else if (data <= axp259_config.pmu_temp_para13) {
		temp = 55 + (axp259_config.pmu_temp_para13 - data) * 5 \
		/ (axp259_config.pmu_temp_para13 \
		- axp259_config.pmu_temp_para14);
	} else if (data <= axp259_config.pmu_temp_para12) {
		temp = 50 + (axp259_config.pmu_temp_para12 - data) * 5 \
		/ (axp259_config.pmu_temp_para12 \
		- axp259_config.pmu_temp_para13);
	} else if (data <= axp259_config.pmu_temp_para11) {
		temp = 45 + (axp259_config.pmu_temp_para11 - data) * 5 \
		/ (axp259_config.pmu_temp_para11 \
		- axp259_config.pmu_temp_para12);
	} else if (data <= axp259_config.pmu_temp_para10) {
		temp = 40 + (axp259_config.pmu_temp_para10 - data) * 5 \
		/ (axp259_config.pmu_temp_para10 \
		- axp259_config.pmu_temp_para11);
	} else if (data <= axp259_config.pmu_temp_para9) {
		temp = 30 + (axp259_config.pmu_temp_para9 - data) * 10 \
		/ (axp259_config.pmu_temp_para9 \
		- axp259_config.pmu_temp_para10);
	} else if (data <= axp259_config.pmu_temp_para8) {
		temp = 20 + (axp259_config.pmu_temp_para8 - data) * 10 \
		/ (axp259_config.pmu_temp_para8 \
		- axp259_config.pmu_temp_para9);
	} else if (data <= axp259_config.pmu_temp_para7) {
		temp = 10 + (axp259_config.pmu_temp_para7 - data) * 10 \
		/ (axp259_config.pmu_temp_para7 \
		- axp259_config.pmu_temp_para8);
	} else if (data <= axp259_config.pmu_temp_para6) {
		temp = 5 + (axp259_config.pmu_temp_para6 - data) * 5 \
		/ (axp259_config.pmu_temp_para6 \
		- axp259_config.pmu_temp_para7);
	} else if (data <= axp259_config.pmu_temp_para5) {
		temp = 0 + (axp259_config.pmu_temp_para5 - data) * 5 \
		/ (axp259_config.pmu_temp_para5 \
		- axp259_config.pmu_temp_para6);
	} else if (data <= axp259_config.pmu_temp_para4) {
		temp = -5 + (axp259_config.pmu_temp_para4 - data) * 5 \
		/ (axp259_config.pmu_temp_para4 \
		- axp259_config.pmu_temp_para5);
	} else if (data <= axp259_config.pmu_temp_para3) {
		temp = -10 + (axp259_config.pmu_temp_para3 - data) * 5 \
		/ (axp259_config.pmu_temp_para3 \
		- axp259_config.pmu_temp_para4);
	} else if (data <= axp259_config.pmu_temp_para2) {
		temp = -15 + (axp259_config.pmu_temp_para2 - data) * 5 \
		/ (axp259_config.pmu_temp_para2 \
		- axp259_config.pmu_temp_para3);
	} else if (data <= axp259_config.pmu_temp_para1) {
		temp = -25 + (axp259_config.pmu_temp_para1 - data) * 10 \
		/ (axp259_config.pmu_temp_para1 \
		- axp259_config.pmu_temp_para2);
	} else
		temp = -25;
	return temp;
}

static int axp259_bat_temp_set(int bat_temp)
{
	static int count;
	static int bat_val = -7;

	if (-7 >= bat_temp) {
		if (10 == count) {
			if (-13 >= bat_val)
				bat_val = -13;
			else {
				bat_val--;
				bat_temp = bat_val;
			}
			count = 0;
		} else
			count++;
	} else if (-7 > bat_val) {
		if (10 == count) {
			bat_val++;
			bat_temp = bat_val;
			count = 0;
		} else
			count++;
	}
	return bat_temp;
}

static inline int axp259_vts_to_mV(uint16_t reg)
{
	return ((int)(((reg >> 8) << 4) | (reg & 0x000F))) * 800 / 1000;
}

static inline int axp259_vbat_to_mV(uint16_t reg)
{
	return ((int)(((reg >> 8) << 4) | (reg & 0x000F))) * 1200 / 1000;
}

static inline int axp259_ocvbat_to_mV(uint16_t reg)
{
	return ((int)(((reg >> 8) << 4) | (reg & 0x000F))) * 1100 / 1000;
}

static inline int axp259_vdc_to_mV(uint16_t reg)
{
	return ((int)(((reg >> 8) << 4) | (reg & 0x000F))) * 1700 / 1000;
}

static inline int axp259_ibat_to_mA(uint16_t reg)
{
	return (int)(((reg >> 8) << 4) | (reg & 0x000F));
}

static inline int axp259_icharge_to_mA(uint16_t reg)
{
	return (int)(((reg >> 8) << 4) | (reg & 0x000F));
}

static inline int axp259_iac_to_mA(uint16_t reg)
{
	return ((int)(((reg >> 8) << 4) | (reg & 0x000F))) * 625 / 1000;
}

static inline int axp259_iusb_to_mA(uint16_t reg)
{
	return ((int)(((reg >> 8) << 4) | (reg & 0x000F))) * 375 / 1000;
}

int axp_read_bat_cap(void)
{
	int bat_cap;
	uint8_t tmp;

	axp_read(axp_charger->master, AXP259_CAP, &tmp);
	bat_cap = (int)(tmp & 0x7F);
	return bat_cap;
}
EXPORT_SYMBOL_GPL(axp_read_bat_cap);

int axp_read_ac_chg(void)
{
	int ac_chg = 0;

	if (axp_charger->ac_valid || axp_charger->usb_adapter_valid)
		ac_chg = 1;
	else
		ac_chg = 0;

	return ac_chg;
}
EXPORT_SYMBOL_GPL(axp_read_ac_chg);

static int axp259x_chg_current_limit(int current_limit)
{
#if defined CONFIG_AXP_CHGCHANGE
	uint8_t tmp = 0;
	if (current_limit == 0)
		axp_clr_bits(axp_charger->master, AXP259_CHARGE_CONTROL1, 0x80);
	else
		axp_set_bits(axp_charger->master, AXP259_CHARGE_CONTROL1, 0x80);
	printk(KERN_DEBUG "current_limit = %d\n", current_limit);

	if (current_limit >= 400000 && current_limit <= 3400000) {
		tmp = (current_limit - 400000) / 200000;
		axp_clr_bits(axp_charger->master, AXP259_CHARGE_CONTROL2, 0x1F);
		axp_update(axp_charger->master,
			AXP259_CHARGE_CONTROL2, tmp, 0x1F);
	} else if (current_limit < 400000) {
		axp_clr_bits(axp_charger->master, AXP259_CHARGE_CONTROL2, 0x1F);
	} else if (current_limit) {
		axp_set_bits(axp_charger->master, AXP259_CHARGE_CONTROL2, 0x1F);
	}
#endif
	return 0;
}

#ifdef CONFIG_ARCH_SUN8IW5P1
static int axp259x_chg_current_temp_limit(int current_limit, int temp_limit)
{
#if defined(CONFIG_AXP_CHGCHANGE)
	if (current_limit == 0)
		axp_clr_bits(axp_charger->master, AXP259_CHARGE_CONTROL1, 0x80);
	else
		axp_set_bits(axp_charger->master, AXP259_CHARGE_CONTROL1, 0x80);
	printk(KERN_DEBUG "current_limit = %d temp_limit = %d\n",
		current_limit, temp_limit);
	if (current_limit > 2550000)
		arisc_adjust_pmu_chgcur(2550000, (unsigned int)temp_limit);
	else if (current_limit > 0 && current_limit < 300000)
		arisc_adjust_pmu_chgcur(300000, (unsigned int)temp_limit);
	else
		arisc_adjust_pmu_chgcur((unsigned int)current_limit,
				(unsigned int)temp_limit);

#endif
	return 0;
}
#endif

static inline void
axp_read_adc(struct axp_charger *charger,
		struct axp_adc_res *adc)
{
	uint8_t tmp[8];
	adc->vac_res = 0;
	adc->iac_res = 0;
	adc->vusb_res = 0;
	adc->iusb_res = 0;

	axp_reads(charger->master, AXP259_VBATH_RES, 6, tmp);
	adc->vbat_res = ((uint16_t) tmp[0] << 8) | tmp[1];
	adc->ichar_res = ((uint16_t) tmp[2] << 8) | tmp[3];
	adc->idischar_res = ((uint16_t) tmp[4] << 8) | tmp[5];
	axp_reads(charger->master, AXP259_OCVBATH_RES, 2, tmp);
	adc->ocvbat_res = ((uint16_t) tmp[0] << 8) | tmp[1];
	axp_reads(charger->master, AXP259_VTS_RES, 2, tmp);
	adc->ts_res = ((uint16_t) tmp[0] << 8) | tmp[1];
}

static void axp_usb_ac_check_status(struct axp_charger *charger)
{
#ifdef AXP259_WITH_USB
	charger->usb_valid = (((CHARGE_USB_20 == axp_usbcurflag)
			|| (CHARGE_USB_30 == axp_usbcurflag))
			&& charger->ext_valid);
	charger->usb_adapter_valid = ((0 == charger->ac_valid)
			&& (CHARGE_USB_20 != axp_usbcurflag)
			&& (CHARGE_USB_30 != axp_usbcurflag)
			&& (charger->ext_valid));
	if (charger->in_short) {
		charger->ac_valid = ((charger->usb_adapter_valid == 0)
			&& (charger->usb_valid == 0) && (charger->ext_valid));
	}
#endif

	if (axp_debug) {
		DBG_PSY_MSG("usb_valid=%d ac_valid=%d usb_adapter_valid=%d\n",
				charger->usb_valid,
				charger->ac_valid,
				charger->usb_adapter_valid);
		DBG_PSY_MSG("usb_det=%d ac_det=%d\n",
				charger->usb_det,
				charger->ac_det);
	}
	charger->ac_valid = charger->ext_valid;
	power_supply_changed(&charger->ac);
#ifdef AXP259_WITH_USB
	power_supply_changed(&charger->usb);
#endif
	return;
}

#ifdef AXP259_WITH_USB
static void axp_charger_update_usb_state(unsigned long data)
{
	struct axp_charger *charger = (struct axp_charger *)data;

	axp_usb_ac_check_status(charger);
	schedule_delayed_work(&usbwork, 0);
}
#endif

static void axp_charger_update_state(struct axp_charger *charger)
{
	uint8_t val[2];
	uint16_t tmp;

	axp_reads(charger->master, AXP259_CHARGE_STATUS, 2, val);
	tmp = (val[1] << 8) + val[0];
	charger->is_on = (val[0] & AXP259_IN_CHARGE) ? 1 : 0;
	charger->fault = val[1];
	charger->bat_det = (val[0] & AXP259_STATUS_BATEN) ? 1 : 0;
	charger->ac_det = (tmp & AXP259_STATUS_ACEN) ? 1 : 0;
#ifdef AXP259_WITH_USB
	charger->usb_det = (tmp & AXP259_STATUS_USBEN) ? 1 : 0;
#endif
	charger->ext_valid = charger->ac_det;
	charger->in_short = 1;
	charger->ac_valid = charger->ac_det;

	charger->bat_current_direction = (tmp & AXP259_IN_CHARGE) ? 1 : 0;
	charger->batery_active = (tmp & AXP259_STATUS_BATINACT) ? 1 : 0;
	charger->int_over_temp = (tmp & AXP259_STATUS_ICTEMOV) ? 1 : 0;
	axp_read(charger->master, AXP259_CHARGE_CONTROL1, val);
	charger->charge_on = ((val[0] >> 7) & 0x01);
}

static void axp_charger_update(struct axp_charger *charger)
{
	uint16_t tmp;
	uint8_t val[2];
	int bat_temp_mv;
	int bat_temp;

	charger->adc = &adc;
	axp_read_adc(charger, &adc);
	tmp = charger->adc->vbat_res;
	charger->vbat = axp259_vbat_to_mV(tmp);
	tmp = charger->adc->ocvbat_res;
	charger->ocv = axp259_ocvbat_to_mV(tmp);
	tmp = charger->adc->ichar_res + charger->adc->idischar_res;
	charger->ibat = ABS(axp259_icharge_to_mA(charger->adc->ichar_res)
			- axp259_ibat_to_mA(charger->adc->idischar_res));
	tmp = 00;
	charger->vac = axp259_vdc_to_mV(tmp);
	tmp = 00;
	charger->iac = axp259_iac_to_mA(tmp);
	tmp = 00;
	charger->vusb = axp259_vdc_to_mV(tmp);
	tmp = 00;
	charger->iusb = axp259_iusb_to_mA(tmp);
	axp_reads(charger->master, AXP259_INTTEMP, 2, val);
	tmp = (val[0] << 4) + (val[1] & 0x0F);
	charger->ic_temp = (int)tmp * 1063 / 10000 - 2667 / 10;
	charger->disvbat = charger->vbat;
	charger->disibat = axp259_ibat_to_mA(charger->adc->idischar_res);
	tmp = charger->adc->ts_res;
	bat_temp_mv = axp259_vts_to_mV(tmp);
	bat_temp = axp259_vts_to_temp(bat_temp_mv);
	charger->bat_temp = axp259_bat_temp_set(bat_temp);
}

unsigned long axp_read_power_sply(void)
{
	uint16_t tmp;
	int disvbat;
	int disibat;
	unsigned long power_sply = 0;

	axp_read_adc(axp_charger, &adc);
	tmp = adc.vbat_res;
	disvbat = axp259_vbat_to_mV(tmp);
	tmp = adc.idischar_res;
	disibat = axp259_ibat_to_mA(tmp);
	printk(KERN_ERR "vbat = %d mV, disibat=%d mA\n", disvbat, disibat);
	power_sply = disvbat * disibat;
	if (0 != power_sply)
		power_sply = power_sply / 1000;
	return power_sply;
}
EXPORT_SYMBOL(axp_read_power_sply);

#if defined CONFIG_AXP_CHARGEINIT
static void axp_set_charge(struct axp_charger *charger)
{
	uint8_t val = 0x00;
	uint8_t tmp = 0x00;

	if (charger->chgend == 10)
		val = 0;
	else if (charger->chgend == 20)
		val = 1;
	else if (charger->chgend == 100)
		val = 2;
	else if (charger->chgend == 200)
		val = 3;
	else if (charger->chgend == 300)
		val = 4;
	else if (charger->chgend == 400)
		val = 5;
	else if (charger->chgend == 500)
		val = 6;
	else if (charger->chgend == 600)
		val = 7;
	val <<= 4;

	if (charger->chgpretime < 40)
		charger->chgpretime = 40;

	if (charger->chgcsttime < 360)
		charger->chgcsttime = 360;

	tmp = ((((charger->chgpretime - 40) / 10) << 2)
	       | ((charger->chgcsttime - 360) / 120));
	val |= tmp;
	axp_update(charger->master, AXP259_CHARGE_CONTROL1, val, 0x7F);

	if (charger->chgcur == 0)
		charger->chgen = 0;

	if (charger->chgvol < 4100) {
		val = 0;
	} else if (charger->chgvol >= 4100 && charger->chgvol <= 4250) {
		val = (charger->chgvol - 4100) / 50;
		val <<= 5;
	} else if (charger->chgvol >= 4350 && charger->chgvol <= 4500) {
		val = (charger->chgvol - 4350) / 50;
		val |= (0x1 << 2);
		val <<= 5;
	} else if (charger->chgvol > 4500) {
		val = 0xE0;
	} else {
		pr_warn("unsupported voltage: %dmv, use default 4200mv\n",
			charger->chgvol);
		val = 0x2 << 5;
	}
	axp_update(charger->master, AXP259_CHARGE_CONTROL2, val, 0xE0);

	 /*TODO*/
#if 0
#ifdef CONFIG_ARCH_SUN8IW5P1
	    if (axp259_config.pmu_chg_temp_en) {
		axp259x_chg_current_limit(
			axp259_config.pmu_runtime_chgcur / 2);
		axp259x_chg_current_temp_limit(
			axp259_config.pmu_runtime_chgcur,
			axp259_config.pmu_runtime_chg_temp);
	} else
		axp259x_chg_current_limit(charger->chgcur);
#else
	    axp259x_chg_current_limit(charger->chgcur);
#endif
#endif
	axp259x_chg_current_limit(charger->chgcur);
}
#else
static void axp_set_charge(struct axp_charger *charger)
{

}
#endif

static enum power_supply_property axp_battery_props[] = {
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
};

static enum power_supply_property axp_ac_props[] = {
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
};

#ifdef AXP259_WITH_USB
static enum power_supply_property axp_usb_props[] = {
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
};
#endif
static void
axp_battery_check_status(struct axp_charger *charger,
			union power_supply_propval *val)
{
	if (charger->bat_det) {
		if (charger->ext_valid) {
			if (charger->rest_vol == 100)
				val->intval = POWER_SUPPLY_STATUS_FULL;
			else if (charger->charge_on)
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
			else
				val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		} else
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
	} else
		val->intval = POWER_SUPPLY_STATUS_FULL;
}

static void
axp_battery_check_health(struct axp_charger *charger,
			union power_supply_propval *val)
{
	val->intval = POWER_SUPPLY_HEALTH_GOOD;
#if 0
	if (charger->fault & AXP259_FAULT_LOG_BATINACT)
		val->intval = POWER_SUPPLY_HEALTH_DEAD;
	else if (charger->fault & AXP259_FAULT_LOG_OVER_TEMP)
		val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (charger->fault & AXP259_FAULT_LOG_COLD)
		val->intval = POWER_SUPPLY_HEALTH_COLD;
	else
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
#endif
}

static int
axp_battery_get_property(struct power_supply *psy,
			enum power_supply_property psp,
			union power_supply_propval *val)
{
	struct axp_charger *charger;
	int ret = 0;

	charger = container_of(psy, struct axp_charger, batt);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		axp_battery_check_status(charger, val);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		axp_battery_check_health(charger, val);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = charger->battery_info->technology;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = charger->battery_info->voltage_max_design;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = charger->battery_info->voltage_min_design;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = charger->vbat * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = charger->ibat * 1000;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = charger->batt.name;
		break;
/*
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		   val->intval = charger->battery_info->charge_full_design;
		   break;
*/
	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
		val->intval = charger->battery_info->energy_full_design;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = charger->rest_vol;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		{
			/* in order to get hardware state,
			 * we must update charger state now.
			 * by sunny at 2012-12-23 11:06:15.
			 */
			axp_charger_update_state(charger);
			val->intval = charger->bat_current_direction;
			if (charger->bat_temp > 50 || -5 > charger->bat_temp)
				val->intval = 0;
			break;
		}
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = charger->bat_det;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = charger->bat_temp * 10;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int
axp_ac_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct axp_charger *charger;
	int ret = 0;

	charger = container_of(psy, struct axp_charger, ac);

	switch (psp) {
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = charger->ac.name;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = (charger->ac_valid || charger->usb_adapter_valid);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = (charger->ac_valid || charger->usb_adapter_valid);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = charger->vac * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = charger->iac * 1000;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

#ifdef AXP259_WITH_USB
static int
axp_usb_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct axp_charger *charger;
	int ret = 0;

	charger = container_of(psy, struct axp_charger, usb);

	switch (psp) {
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = charger->usb.name;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = charger->usb_valid;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = charger->usb_valid;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = charger->vusb * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = charger->iusb * 1000;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}
#endif
static void axp_change(struct axp_charger *charger)
{
	if (axp_debug)
		DBG_PSY_MSG("battery state change\n");
	axp_charger_update_state(charger);
	axp_charger_update(charger);
	flag_state_change = 1;
	axp_usb_ac_check_status(charger);
	power_supply_changed(&charger->batt);
}

static void axp_presslong(struct axp_charger *charger)
{
	if (axp_debug)
		DBG_PSY_MSG("press long\n");
	input_report_key(powerkeydev, KEY_POWER, 1);
	input_sync(powerkeydev);
	ssleep(2);
	DBG_PSY_MSG("press long up\n");
	input_report_key(powerkeydev, KEY_POWER, 0);
	input_sync(powerkeydev);
}

static void axp_pressshort(struct axp_charger *charger)
{
	if (axp_debug)
		DBG_PSY_MSG("press short\n");
	input_report_key(powerkeydev, KEY_POWER, 1);
	input_sync(powerkeydev);
	msleep(100);
	input_report_key(powerkeydev, KEY_POWER, 0);
	input_sync(powerkeydev);
}

static void axp_keyup(struct axp_charger *charger)
{
	if (axp_debug)
		DBG_PSY_MSG("power key up\n");
	input_report_key(powerkeydev, KEY_POWER, 0);
	input_sync(powerkeydev);
}

static void axp_keydown(struct axp_charger *charger)
{
	if (axp_debug)
		DBG_PSY_MSG("power key down\n");
	input_report_key(powerkeydev, KEY_POWER, 1);
	input_sync(powerkeydev);
}

static void axp_capchange(struct axp_charger *charger)
{
	uint8_t val;
	int k;

	if (axp_debug)
		DBG_PSY_MSG("battery change\n");
	ssleep(2);
	axp_charger_update_state(charger);
	axp_charger_update(charger);
	axp_read(charger->master, AXP259_CAP, &val);
	charger->rest_vol = (int)(val & 0x7F);
	if ((charger->bat_det == 0) || (charger->rest_vol == 127))
		charger->rest_vol = 100;
	if (axp_debug)
		DBG_PSY_MSG("rest_vol = %d\n", charger->rest_vol);
	memset(Bat_Cap_Buffer, 0, sizeof(Bat_Cap_Buffer));
	for (k = 0; k < AXP259_VOL_MAX; k++)
		Bat_Cap_Buffer[k] = charger->rest_vol;
	Total_Cap = charger->rest_vol * AXP259_VOL_MAX;

	power_supply_changed(&charger->batt);
}

/* fix charge-done bug
 * when battery in high voltage and plug ac,
 * vbat maybe large than Vtarget a lot,
 * so error to report charge-done.
 *
 * now, use this function to fix the bug.
 *
 */
static void axp_charge_done_fix_start(struct axp_charger *charger)
{
	uint8_t val;

	/* in sharp charge-done mode or not*/
	axp_read(charger->master, AXP259_DATA_BUFFER8, &val);
	if (val&0x02)
		return;
	if (axp259_config.pmu_batdeten != 0) {
		/* disable battery detection */
		axp_read(charger->master, AXP259_CHARGE3, &val);
		val &= ~(1<<7);
		axp_write(charger->master, AXP259_CHARGE3, val);
	}
	/* disable charger */
	axp_read(charger->master, AXP259_CHARGE1, &val);
	val &= ~(1<<7);
	axp_write(charger->master, AXP259_CHARGE1, val);
	/* enable charger */
	val |= (1<<7);
	axp_write(charger->master, AXP259_CHARGE1, val);
	g_charge_done_fix_flag = CHARGE_DONE_RECHARGE;
	if (axp_debug)
		DBG_PSY_MSG("====fix charge done bug start!!\n");
}

static void axp_charge_done_fix_end(struct axp_charger *charger)
{
	uint8_t val;

	if (axp259_config.pmu_batdeten != 0) {
		/* enable battery detection */
		axp_read(charger->master, AXP259_CHARGE3, &val);
		val |= (1<<7);
		axp_write(charger->master, AXP259_CHARGE3, val);
	}
	g_charge_done_fix_flag = CHARGE_DONE_END;
	if (axp_debug)
		DBG_PSY_MSG("====fix charge done bug end!!\n");
}

static int
axp_battery_event(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct axp_charger *charger = container_of(nb, struct axp_charger, nb);
	uint8_t w[9];
	int value;

	if (axp_debug)
		DBG_PSY_MSG("axp_battery_event enter...\n");
	if ((bool) data == 0) {
		if (axp_debug)
			DBG_PSY_MSG("low 32bit status...\n");
		if (event & (AXP259_IRQ_BATIN | AXP259_IRQ_BATRE))
			axp_capchange(charger);
		if (event & AXP259_IRQ_CHAOV) {
			axp_capchange(charger);
			if (axp259_config.pmu_batdeten != 0) {
				if(charger->rest_vol < 100)
                                        axp_charge_done_fix_start(charger);
                                else
                                        g_charge_done_fix_flag = CHARGE_DONE_END;
			}
		}
		if (event & AXP259_IRQ_CHAST)
			axp_change(charger);
		if (event & (AXP259_IRQ_ACIN)) {
#ifdef CONFIG_HAS_WAKELOCK
			wake_lock_timeout(&axp_wakeup_lock,
				msecs_to_jiffies(2000));
#endif
			if (axp_debug)
				DBG_PSY_MSG("axp259 ac in!\n");

			axp_change(charger);
		}
		if (event & (AXP259_IRQ_ACRE)) {
#ifdef CONFIG_HAS_WAKELOCK
			wake_lock_timeout(&axp_wakeup_lock,
				msecs_to_jiffies(2000));
#endif
			if (axp_debug)
				DBG_PSY_MSG("axp259 ac out!\n");
			if (g_charge_done_fix_flag != 0) {
				axp_charge_done_fix_end(charger);
				g_charge_done_fix_flag = 0;
			}
			axp_change(charger);
		}
		if (event & AXP259_IRQ_POKLO)
			axp_presslong(charger);
		if (event & AXP259_IRQ_POKSH)
			axp_pressshort(charger);
		/*under or over temperature in work mode */
		if (event & (AXP259_IRQ_BWUT | AXP259_IRQ_BWOT))
			axp_change(charger);
		/*under or over temperature in charge mode */
		if (event & (AXP259_IRQ_BCUT | AXP259_IRQ_BCOT))
			axp_change(charger);
		value = axp_powerkey_get();
		if (0 != value)
			axp_powerkey_set(0);
		else {
			if (event & AXP259_IRQ_PEKFE)
				axp_keydown(charger);
			if (event & AXP259_IRQ_PEKRE)
				axp_keyup(charger);
		}
		w[0] = (uint8_t) ((event) & 0xFF);
		w[1] = AXP259_INTSTS2;
		w[2] = (uint8_t) ((event >> 8) & 0xFF);
		w[3] = AXP259_INTSTS3;
		w[4] = (uint8_t) ((event >> 16) & 0xFF);
		w[5] = AXP259_INTSTS4;
		w[6] = (uint8_t) ((event >> 24) & 0xFF);
		w[7] = AXP259_INTSTS5;
		w[8] = 0;
	} else {
		if (axp_debug)
			DBG_PSY_MSG("high 32bit status...\n");
		if (event & (AXP259_IRQ_BUCKOV_6V6 >> 32))
			DBG_PSY_MSG("ac poor power!\n");
		w[0] = 0;
		w[1] = AXP259_INTSTS2;
		w[2] = 0;
		w[3] = AXP259_INTSTS3;
		w[4] = 0;
		w[5] = AXP259_INTSTS4;
		w[6] = 0;
		w[7] = AXP259_INTSTS5;
		w[8] = (uint8_t) ((event) & 0xFF);
	}
	if (axp_debug)
		DBG_PSY_MSG("event = 0x%x\n", (int)event);
	axp_writes(charger->master, AXP259_INTSTS1, 9, w);
	return 0;
}

static char *supply_list[] = {
	"battery",
};

static void axp_battery_setup_psy(struct axp_charger *charger)
{
	struct power_supply *batt = &charger->batt;
	struct power_supply *ac = &charger->ac;
#ifdef AXP259_WITH_USB
	struct power_supply *usb = &charger->usb;
#endif
	struct power_supply_info *info = charger->battery_info;

	batt->name = "battery";
	batt->use_for_apm = info->use_for_apm;
	batt->type = POWER_SUPPLY_TYPE_BATTERY;
	batt->get_property = axp_battery_get_property;
	batt->properties = axp_battery_props;
	batt->num_properties = ARRAY_SIZE(axp_battery_props);

	ac->name = "ac";
	ac->type = POWER_SUPPLY_TYPE_MAINS;
	ac->get_property = axp_ac_get_property;
	ac->supplied_to = supply_list;
	ac->num_supplicants = ARRAY_SIZE(supply_list);
	ac->properties = axp_ac_props;
	ac->num_properties = ARRAY_SIZE(axp_ac_props);
#ifdef AXP259_WITH_USB
	usb->name = "usb";
	usb->type = POWER_SUPPLY_TYPE_USB;
	usb->get_property = axp_usb_get_property;
	usb->supplied_to = supply_list,
	usb->num_supplicants = ARRAY_SIZE(supply_list),
	usb->properties = axp_usb_props;
	usb->num_properties = ARRAY_SIZE(axp_usb_props);
#endif
};

#if defined  CONFIG_AXP_CHARGEINIT
static int axp_battery_adc_set(struct axp_charger *charger)
{
	int ret;
	uint8_t val;

	/*enable adc and set adc */
	val = AXP259_ADC_BATVOL_ENABLE | AXP259_ADC_BATCUR_ENABLE;
	if (0 != axp259_config.pmu_temp_enable)
		val = val | AXP259_ADC_TSVOL_ENABLE;

	ret = axp_update(charger->master, AXP259_ADC_CONTROL, val,
			AXP259_ADC_BATVOL_ENABLE | AXP259_ADC_BATCUR_ENABLE
			| AXP259_ADC_TSVOL_ENABLE);
	if (ret)
		return ret;
	ret = axp_read(charger->master, AXP259_TS_PIN_CONTROL, &val);
	switch (charger->sample_time / 100) {
	case 1:
		val &= ~(3 << 3);
		break;
	case 2:
		val &= ~(3 << 3);
		val |= 1 << 3;
		break;
	case 4:
		val &= ~(3 << 3);
		val |= 2 << 3;
		break;
	case 8:
		val |= 3 << 6;
		break;
	default:
		break;
	}
	if (0 != axp259_config.pmu_temp_enable)
		val &= (~(1 << 2));
	ret = axp_write(charger->master, AXP259_TS_PIN_CONTROL, val);
	if (ret)
		return ret;
	return 0;
}
#else
static int axp_battery_adc_set(struct axp_charger *charger)
{
	return 0;
}
#endif

static int axp_battery_first_init(struct axp_charger *charger)
{
	int ret;
	uint8_t val;

	axp_set_charge(charger);
	ret = axp_battery_adc_set(charger);
	if (ret)
		return ret;
	ret = axp_read(charger->master, AXP259_TS_PIN_CONTROL, &val);
	switch ((val >> 3) & 0x03) {
	case 0:
		charger->sample_time = 100;
		break;
	case 1:
		charger->sample_time = 200;
		break;
	case 2:
		charger->sample_time = 400;
		break;
	case 3:
		charger->sample_time = 800;
		break;
	default:
		break;
	}
	return ret;
}

static ssize_t
chgen_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	uint8_t val;

	axp_read(charger->master, AXP259_CHARGE_CONTROL1, &val);
	charger->chgen = val >> 7;
	return sprintf(buf, "%d\n", charger->chgen);
}

static ssize_t
chgen_store(struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t count)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	int var;

	if (kstrtoul(buf, 10, (unsigned long *)&var))
		return 0;
	if (var) {
		charger->chgen = 1;
		axp_set_bits(charger->master, AXP259_CHARGE_CONTROL1, 0x80);
	} else {
		charger->chgen = 0;
		axp_clr_bits(charger->master, AXP259_CHARGE_CONTROL1, 0x80);
	}
	return count;
}

static ssize_t
chgmicrovol_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	uint8_t val;

	axp_read(charger->master, AXP259_CHARGE_CONTROL2, &val);
	switch ((val >> 5) & 0x07) {
	case 0:
		charger->chgvol = 4100000;
		break;
	case 1:
		charger->chgvol = 4150000;
		break;
	case 2:
		charger->chgvol = 4200000;
		break;
	case 3:
		charger->chgvol = 4250000;
		break;
	case 4:
		charger->chgvol = 4350000;
		break;
	case 5:
		charger->chgvol = 4400000;
		break;
	case 6:
		charger->chgvol = 4450000;
		break;
	case 7:
		charger->chgvol = 4500000;
		break;
	}
	return sprintf(buf, "%d\n", charger->chgvol);
}

static ssize_t
chgmicrovol_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	int var;
	uint8_t tmp, val;

	if (kstrtoul(buf, 10, (unsigned long *)&var))
		return 0;
	switch (var) {
	case 4100000:
		tmp = 0;
		break;
	case 4150000:
		tmp = 1;
		break;
	case 4200000:
		tmp = 2;
		break;
	case 4250000:
		tmp = 3;
		break;
	case 4350000:
		tmp = 4;
		break;
	case 4400000:
		tmp = 5;
		break;
	case 4450000:
		tmp = 6;
		break;
	case 4500000:
		tmp = 7;
		break;
	default:
		tmp = 8;
		break;
	}
	if (tmp < 8) {
		charger->chgvol = var;
		axp_read(charger->master, AXP259_CHARGE_CONTROL2, &val);
		val &= 0x1F;
		val |= tmp << 5;
		axp_write(charger->master, AXP259_CHARGE_CONTROL2, val);
	}
	return count;
}

static ssize_t
chgintmicrocur_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	uint8_t val;

	axp_read(charger->master, AXP259_CHARGE_CONTROL2, &val);
	charger->chgcur = (val & 0x0F) * 200000 + 400000;
	return sprintf(buf, "%d\n", charger->chgcur);
}

static ssize_t
chgintmicrocur_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	int var;
	uint8_t val, tmp;

	if (kstrtoul(buf, 10, (unsigned long *)&var))
		return 0;
	if (var >= 400000 && var <= 3400000) {
		tmp = (var - 400000) / 200000;
		charger->chgcur = tmp * 200000 + 400000;
		axp_read(charger->master, AXP259_CHARGE_CONTROL2, &val);
		val &= 0xF0;
		val |= tmp;
		axp_write(charger->master, AXP259_CHARGE_CONTROL2, val);
	}
	return count;
}

static ssize_t
chgendcur_show(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	uint8_t val;

	axp_read(charger->master, AXP259_CHARGE_CONTROL1, &val);
	val = (val >> 4) & 0x07;
	if (val == 0)
		charger->chgend = 10;
	else if (val == 1)
		charger->chgend = 20;
	else
		charger->chgend = (val - 2) * 100 + 100;
	return sprintf(buf, "%d\n", charger->chgend);
}

static ssize_t
chgendcur_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	int var;

	if (kstrtoul(buf, 10, (unsigned long *)&var))
		return 0;
	if (var == 10) {
		charger->chgend = var;
		axp_clr_bits(charger->master, AXP259_CHARGE_CONTROL1, 0x70);
	} else if (var == 20) {
		charger->chgend = var;
		axp_update(axp_charger->master,
			AXP259_CHARGE_CONTROL1, 0x10, 0x70);
	} else {
		switch (var) {
		case 100:
		case 200:
		case 300:
		case 400:
		case 500:
		case 600:
			charger->chgend = var;
			axp_update(axp_charger->master,
				AXP259_CHARGE_CONTROL1,
				(1 + var / 100) << 4, 0x70);
			break;
		default:
			DBG_PSY_MSG("invalid charger end condition.");
			break;
		}
	}
	return count;
}

static ssize_t
chgpretimemin_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	uint8_t val;

	axp_read(charger->master, AXP259_CHARGE_CONTROL1, &val);
	charger->chgpretime = ((val >> 2) & 0x03) * 10 + 40;
	return sprintf(buf, "%d\n", charger->chgpretime);
}

static ssize_t
chgpretimemin_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	int var;
	uint8_t tmp, val;

	if (kstrtoul(buf, 10, (unsigned long *)&var))
		return 0;
	if (var >= 40 && var <= 70) {
		tmp = (var - 40) / 10;
		charger->chgpretime = tmp * 10 + 40;
		axp_read(charger->master, AXP259_CHARGE_CONTROL1, &val);
		val &= 0xF3;
		val |= (tmp << 2);
		axp_write(charger->master, AXP259_CHARGE_CONTROL1, val);
	}
	return count;
}

static ssize_t
chgcsttimemin_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	uint8_t val;

	axp_read(charger->master, AXP259_CHARGE_CONTROL1, &val);
	charger->chgcsttime = (val & 0x03) * 120 + 360;
	return sprintf(buf, "%d\n", charger->chgcsttime);
}

static ssize_t
chgcsttimemin_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	int var;
	uint8_t tmp, val;

	if (kstrtoul(buf, 10, (unsigned long *)&var))
		return 0;
	if (var >= 360 && var <= 720) {
		tmp = (var - 360) / 120;
		charger->chgcsttime = tmp * 120 + 360;
		axp_read(charger->master, AXP259_CHARGE_CONTROL1, &val);
		val &= 0xFC;
		val |= tmp;
		axp_write(charger->master, AXP259_CHARGE_CONTROL1, val);
	}
	return count;
}

static ssize_t
adcfreq_show(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	uint8_t val;

	axp_read(charger->master, AXP259_TS_PIN_CONTROL, &val);
	switch ((val >> 3) & 0x03) {
	case 0:
		charger->sample_time = 100;
		break;
	case 1:
		charger->sample_time = 200;
		break;
	case 2:
		charger->sample_time = 400;
		break;
	case 3:
		charger->sample_time = 800;
		break;
	default:
		break;
	}
	return sprintf(buf, "%d\n", charger->sample_time);
}

static ssize_t
adcfreq_store(struct device *dev,
	struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	int var;
	uint8_t val;

	if (kstrtoul(buf, 10, (unsigned long *)&var))
		return 0;
	axp_read(charger->master, AXP259_TS_PIN_CONTROL, &val);
	switch (var / 100) {
	case 1:
		val &= ~(3 << 2);
		charger->sample_time = 100;
		break;
	case 2:
		val &= ~(3 << 2);
		val |= 1 << 2;
		charger->sample_time = 200;
		break;
	case 4:
		val &= ~(3 << 2);
		val |= 2 << 2;
		charger->sample_time = 400;
		break;
	case 8:
		val |= 3 << 2;
		charger->sample_time = 800;
		break;
	default:
		break;
	}
	axp_write(charger->master, AXP259_TS_PIN_CONTROL, val);
	return count;
}

#ifdef AXP259_WITH_USB
static ssize_t
vholden_show(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	uint8_t val;

	axp_read(charger->master, AXP259_CHARGE_VBUS, &val);
	val = (val >> 6) & 0x01;
	return sprintf(buf, "%d\n", val);
}

static ssize_t
vholden_store(struct device *dev,
	struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	int var;

	if (kstrtoul(buf, 10, (unsigned long *)&var))
		return 0;
	if (var)
		axp_set_bits(charger->master, AXP259_CHARGE_VBUS, 0x40);
	else
		axp_clr_bits(charger->master, AXP259_CHARGE_VBUS, 0x40);
	return count;
}

static ssize_t
vhold_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	uint8_t val;
	int vhold;

	axp_read(charger->master, AXP259_CHARGE_VBUS, &val);
	vhold = ((val >> 3) & 0x07) * 100000 + 4000000;
	return sprintf(buf, "%d\n", vhold);
}

static ssize_t
vhold_store(struct device *dev,
	struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	int var;
	uint8_t val, tmp;

	if (kstrtoul(buf, 10, (unsigned long *)&var))
		return 0;
	if (var >= 4000000 && var <= 4700000) {
		tmp = (var - 4000000) / 100000;
		axp_read(charger->master, AXP259_CHARGE_VBUS, &val);
		val &= 0xC7;
		val |= tmp << 3;
		axp_write(charger->master, AXP259_CHARGE_VBUS, val);
	}
	return count;
}

static ssize_t
iholden_show(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	uint8_t val;

	axp_read(charger->master, AXP259_CHARGE_VBUS, &val);
	return sprintf(buf, "%d\n", ((val & 0x03) == 0x03) ? 0 : 1);
}

static ssize_t
iholden_store(struct device *dev,
	struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	int var;

	if (kstrtoul(buf, 10, (unsigned long *)&var))
		return 0;
	if (var)
		axp_clr_bits(charger->master, AXP259_CHARGE_VBUS, 0x01);
	else
		axp_set_bits(charger->master, AXP259_CHARGE_VBUS, 0x03);
	return count;
}

static ssize_t
ihold_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	uint8_t val, tmp;
	int ihold;

	axp_read(charger->master, AXP259_CHARGE_VBUS, &val);
	tmp = (val) & 0x03;
	switch (tmp) {
	case 0:
		ihold = 900000;
		break;
	case 1:
		ihold = 500000;
		break;
	default:
		ihold = 0;
		break;
	}
	return sprintf(buf, "%d\n", ihold);
}

static ssize_t
ihold_store(struct device *dev,
	struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	int var;

	if (kstrtoul(buf, 10, (unsigned long *)&var))
		return 0;
	if (var == 900000)
		axp_clr_bits(charger->master, AXP259_CHARGE_VBUS, 0x03);
	else if (var == 500000) {
		axp_clr_bits(charger->master, AXP259_CHARGE_VBUS, 0x02);
		axp_set_bits(charger->master, AXP259_CHARGE_VBUS, 0x01);
	}
	return count;
}

#ifdef CONFIG_AXP809
static ssize_t
acin_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	uint8_t val;

	axp_read(charger->master, AXP259_CHARGE3, &val);
	val = (val) & 0x10;
	if (val)
		return 0;
	else
		return 1;
}

static ssize_t
acin_enable_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	int var;
	uint8_t val;

	if (kstrtoul(buf, 10, (unsigned long *)&var))
		return 0;
	axp_read(charger->master, AXP259_CHARGE3, &val);
	if (var) {
		val &= ~(0x10);
		axp_write(charger->master, AXP259_CHARGE3, val);
	} else {
		val |= 0x10;
		axp_write(charger->master, AXP259_CHARGE3, val);
	}
	return count;
}
#endif
#endif

static struct device_attribute axp_charger_attrs[] = {
	AXP_CHG_ATTR(chgen),
	AXP_CHG_ATTR(chgmicrovol),
	AXP_CHG_ATTR(chgintmicrocur),
	AXP_CHG_ATTR(chgendcur),
	AXP_CHG_ATTR(chgpretimemin),
	AXP_CHG_ATTR(chgcsttimemin),
	AXP_CHG_ATTR(adcfreq),
#ifdef AXP259_WITH_USB
	AXP_CHG_ATTR(vholden),
	AXP_CHG_ATTR(vhold),
	AXP_CHG_ATTR(iholden),
	AXP_CHG_ATTR(ihold),
#endif
#ifdef CONFIG_AXP809
	AXP_CHG_ATTR(acin_enable),
#endif
};

#if defined CONFIG_HAS_EARLYSUSPEND
static void axp_earlysuspend(struct early_suspend *h)
{

	if (axp_debug)
		DBG_PSY_MSG("======early suspend=======\n");
	early_suspend_flag = 1;
#ifdef CONFIG_ARCH_SUN8IW5P1
	if (axp259_config.pmu_chg_temp_en) {
		axp259x_chg_current_limit(
			axp259_config.pmu_earlysuspend_chgcur / 2);
		axp259x_chg_current_temp_limit(
			axp259_config.pmu_earlysuspend_chgcur,
			axp259_config.pmu_earlysuspend_chg_temp);
	} else
		axp259x_chg_current_limit(
			axp259_config.pmu_earlysuspend_chgcur);
#else
	axp259x_chg_current_limit(axp259_config.pmu_earlysuspend_chgcur);
#endif
}

static void axp_lateresume(struct early_suspend *h)
{
	int value;

	value = axp_powerkey_get();
	if (0 != value)
		axp_powerkey_set(0);

	if (axp_debug)
		DBG_PSY_MSG("======late resume=======\n");
	early_suspend_flag = 0;
#ifdef CONFIG_ARCH_SUN8IW5P1
	if (axp259_config.pmu_chg_temp_en) {
		axp259x_chg_current_limit(
			axp259_config.pmu_runtime_chgcur / 2);
		axp259x_chg_current_temp_limit(
			axp259_config.pmu_runtime_chgcur,
			axp259_config.pmu_runtime_chg_temp);
	} else
		axp259x_chg_current_limit(axp259_config.pmu_runtime_chgcur);
#else
	axp259x_chg_current_limit(axp259_config.pmu_runtime_chgcur);
#endif

}
#endif

static int axp_charger_create_attrs(struct power_supply *psy)
{
	int j, ret;

	for (j = 0; j < ARRAY_SIZE(axp_charger_attrs); j++) {
		ret = device_create_file(psy->dev, &axp_charger_attrs[j]);
		if (ret)
			goto sysfs_failed;
	}
	goto succeed;

sysfs_failed:
	while (j--)
		device_remove_file(psy->dev, &axp_charger_attrs[j]);
succeed:
	return ret;
}

static void axp_charging_monitor(struct work_struct *work)
{
	struct axp_charger *charger;
	uint8_t val, temp_val[4], batt_max_cap_val[3];
	int pre_rest_vol, pre_bat_curr_dir, batt_max_cap, coulumb_counter;
	unsigned long power_sply = 0;

	charger = container_of(work, struct axp_charger, work.work);
	pre_rest_vol = charger->rest_vol;
	pre_bat_curr_dir = charger->bat_current_direction;
	axp_charger_update_state(charger);
	axp_charger_update(charger);
	axp_read(charger->master, AXP259_CAP, &val);
	charger->rest_vol = (int)(val & 0x7F);
	axp_reads(charger->master, 0xe2, 2, temp_val);
	coulumb_counter = (((temp_val[0] & 0x7f) << 8) + temp_val[1]) \
			* 1456 / 1000;
	axp_reads(charger->master, 0xe0, 2, temp_val);
	batt_max_cap = (((temp_val[0] & 0x7f) << 8) + temp_val[1]) \
			* 1456 / 1000;
	/* Avoid the power stay in 100% for a long time. */
	if (coulumb_counter > batt_max_cap) {
		batt_max_cap_val[0] = temp_val[0] | (0x1 << 7);
		batt_max_cap_val[1] = 0xe3;
		batt_max_cap_val[2] = temp_val[1];
		axp_writes(charger->master, 0xe2, 3, batt_max_cap_val);
		DBG_PSY_MSG("Axp259 coulumb_counter = %d\n", batt_max_cap);
	}

	switch(g_charge_done_fix_flag) {
                /* charge or recharge done */
                case CHARGE_DONE_END:
                        if(charger->rest_vol < 100) {
                                axp_charge_done_fix_start(charger);
                                g_charge_done_fix_flag = CHARGE_DONE_END_RECHARGE;
                        }
                        break;
                /* recharge */
                case CHARGE_DONE_RECHARGE:
                /* after charge done, battery discharge long time and
                 * cap less than 100, so recharge, but cap should fake 100.
                 */
                case CHARGE_DONE_END_RECHARGE:
                        if(charger->rest_vol == 100)
                                axp_charge_done_fix_end(charger);
                        break;
        }

	if (axp_debug) {
		DBG_PSY_MSG("charger->ic_temp = %d\n", charger->ic_temp);
		DBG_PSY_MSG("charger->bat_temp = %d\n", charger->bat_temp);
		DBG_PSY_MSG("charger->vbat = %d\n", charger->vbat);
		DBG_PSY_MSG("charger->ibat = %d\n", charger->ibat);
		DBG_PSY_MSG("charger->ocv = %d\n", charger->ocv);
		DBG_PSY_MSG("charger->disvbat = %d\n", charger->disvbat);
		DBG_PSY_MSG("charger->disibat = %d\n", charger->disibat);
		power_sply = charger->disvbat * charger->disibat;
		if (0 != power_sply)
			power_sply = power_sply / 1000;
		DBG_PSY_MSG("power_sply = %ld mW\n", power_sply);
		DBG_PSY_MSG("charger->rest_vol = %d\n", charger->rest_vol);
		axp_reads(charger->master, 0xba, 2, temp_val);
		DBG_PSY_MSG("Axp259 Rdc = %d\n", (((temp_val[0] & 0x1f) << 8)\
				 + temp_val[1]) * 10742 / 10000);
		DBG_PSY_MSG("Axp259 coulumb_counter = %d\n", coulumb_counter);
		DBG_PSY_MSG("Axp259 batt_max_cap = %d\n", batt_max_cap);
		axp_read(charger->master, 0xb8, temp_val);
		DBG_PSY_MSG("Axp259 REG_B8 = %x\n", temp_val[0]);
		axp_reads(charger->master, 0xe4, 2, temp_val);
		DBG_PSY_MSG("Axp259 OCV_percentage = %d\n",
				(temp_val[0] & 0x7f));
		DBG_PSY_MSG("Axp259 Coulumb_percentage = %d\n",
				(temp_val[1] & 0x7f));
		DBG_PSY_MSG("charger->is_on = %d\n", charger->is_on);
		DBG_PSY_MSG("charger->bat_current_direction = %d\n",
				charger->bat_current_direction);
		DBG_PSY_MSG("charger->charge_on = %d\n", charger->charge_on);
		DBG_PSY_MSG("charger->ext_valid = %d\n", charger->ext_valid);
		DBG_PSY_MSG("pmu_runtime_chgcur           = %d\n",
				axp259_config.pmu_runtime_chgcur);
		DBG_PSY_MSG("pmu_earlysuspend_chgcur   = %d\n",
				axp259_config.pmu_earlysuspend_chgcur);
		DBG_PSY_MSG("pmu_suspend_chgcur        = %d\n",
				axp259_config.pmu_suspend_chgcur);
		DBG_PSY_MSG("pmu_shutdown_chgcur       = %d\n\n\n",
				axp259_config.pmu_shutdown_chgcur);
	}

	/* if battery volume changed, inform uevent */
	if ((charger->rest_vol - pre_rest_vol)
		|| (charger->bat_current_direction != pre_bat_curr_dir)) {
		if (axp_debug) {
			axp_reads(charger->master, 0xe2, 2, temp_val);
			axp_reads(charger->master, 0xe4, 2, (temp_val + 2));
			DBG_PSY_MSG("battery vol change: %d->%d\n",
				pre_rest_vol, charger->rest_vol);
			DBG_PSY_MSG("for test %d %d %d %d %d %d\n",
				charger->vbat, charger->ocv, charger->ibat,
			       (temp_val[2] & 0x7f), (temp_val[3] & 0x7f),
			       (((temp_val[0] & 0x7f) << 8) + temp_val[1]) \
				* 1456 / 1000);
		}
		pre_rest_vol = charger->rest_vol;
		power_supply_changed(&charger->batt);
	} else {
		if ((60 <= charger->bat_temp) || (-10 >= charger->bat_temp))
			power_supply_changed(&charger->batt);
	}
	/* reschedule for the next time */
	schedule_delayed_work(&charger->work, charger->interval);
}

#ifdef AXP259_WITH_USB
static void axp_usb(struct work_struct *work)
{
	int var;
	uint8_t tmp, val;
	struct axp_charger *charger;

	charger = axp_charger;
	if (axp_debug)
		DBG_PSY_MSG("[axp_usb]axp_usbcurflag = %d\n", axp_usbcurflag);
	axp_read(axp_charger->master, AXP259_CHARGE_STATUS, &val);
	if ((val & 0x10) == 0x00) {	/*usb or usb adapter can not be used */
		if (axp_debug)
			DBG_PSY_MSG("USB not insert!\n");
		axp_clr_bits(charger->master, AXP259_CHARGE_VBUS, 0x02);
		axp_set_bits(charger->master, AXP259_CHARGE_VBUS, 0x01);
	} else if (CHARGE_USB_20 == axp_usbcurflag) {
		if (axp_debug)
			DBG_PSY_MSG("set usbcur_pc %d mA\n",
				axp259_config.pmu_usbcur_pc);
		if (axp259_config.pmu_usbcur_pc) {
			var = axp259_config.pmu_usbcur_pc * 1000;
			if (var >= 900000)
				axp_clr_bits(charger->master,
					AXP259_CHARGE_VBUS, 0x03);
			else if (var < 900000) {
				axp_clr_bits(charger->master,
					AXP259_CHARGE_VBUS, 0x02);
				axp_set_bits(charger->master,
					AXP259_CHARGE_VBUS, 0x01);
			}
		} else		/*not limit*/
			axp_set_bits(charger->master, AXP259_CHARGE_VBUS, 0x03);
	} else if (CHARGE_USB_30 == axp_usbcurflag) {
		axp_clr_bits(charger->master, AXP259_CHARGE_VBUS, 0x03);
	} else {
		if (axp_debug)
			DBG_PSY_MSG("set usbcur %d mA\n",
				axp259_config.pmu_usbcur);
		if ((axp259_config.pmu_usbcur) &&
			(axp259_config.pmu_usbcur_limit)) {
			var = axp259_config.pmu_usbcur * 1000;
			if (var >= 900000)
				axp_clr_bits(charger->master,
					AXP259_CHARGE_VBUS, 0x03);
			else if (var < 900000) {
				axp_clr_bits(charger->master,
					AXP259_CHARGE_VBUS, 0x02);
				axp_set_bits(charger->master,
					AXP259_CHARGE_VBUS, 0x01);
			}
		} else		/*not limit*/
			axp_set_bits(charger->master, AXP259_CHARGE_VBUS, 0x03);
	}

	if (!vbus_curr_limit_debug) {	/*usb current not limit*/
		if (axp_debug)
			DBG_PSY_MSG("vbus_curr_limit_debug = %d\n",
				vbus_curr_limit_debug);
		axp_set_bits(charger->master, AXP259_CHARGE_VBUS, 0x03);
	}

	if (CHARGE_USB_20 == axp_usbvolflag) {
		if (axp_debug)
			DBG_PSY_MSG("set usbvol_pc %d mV\n",
				axp259_config.pmu_usbvol_pc);
		if (axp259_config.pmu_usbvol_pc) {
			axp_set_bits(charger->master, AXP259_CHARGE_VBUS, 0x40);
			var = axp259_config.pmu_usbvol_pc * 1000;
			if (var >= 4000000 && var <= 4700000) {
				tmp = (var - 4000000) / 100000;
				axp_read(charger->master,
					AXP259_CHARGE_VBUS, &val);
				val &= 0xC7;
				val |= tmp << 3;
				axp_write(charger->master,
					AXP259_CHARGE_VBUS, val);
			} else
				DBG_PSY_MSG(
					"set usb limit voltage error,%d mV\n",
					axp259_config.pmu_usbvol_pc);
		} else
			axp_clr_bits(charger->master, AXP259_CHARGE_VBUS, 0x40);
	} else if (CHARGE_USB_30 == axp_usbvolflag) {
		axp_read(charger->master, AXP259_CHARGE_VBUS, &val);
		val &= 0xC7;
		val |= 7 << 3;
		axp_write(charger->master, AXP259_CHARGE_VBUS, val);
	} else {
		if (axp_debug)
			DBG_PSY_MSG("set usbvol %d mV\n",
				axp259_config.pmu_usbvol);
		if ((axp259_config.pmu_usbvol) &&
			(axp259_config.pmu_usbvol_limit)) {
			axp_set_bits(charger->master, AXP259_CHARGE_VBUS, 0x40);
			var = axp259_config.pmu_usbvol * 1000;
			if (var >= 4000000 && var <= 4700000) {
				tmp = (var - 4000000) / 100000;
				axp_read(charger->master,
					AXP259_CHARGE_VBUS, &val);
				val &= 0xC7;
				val |= tmp << 3;
				axp_write(charger->master,
					AXP259_CHARGE_VBUS, val);
			} else
				DBG_PSY_MSG(
					"set usb limit voltage error,%d mV\n",
					axp259_config.pmu_usbvol);
		} else
			axp_clr_bits(charger->master, AXP259_CHARGE_VBUS, 0x40);
	}
}
#endif
static int axp_battery_probe(struct platform_device *pdev)
{
	struct axp_charger *charger;
	struct axp_supply_init_data *pdata = pdev->dev.platform_data;
	int ret;
	uint8_t val2, val;
	uint8_t ocv_cap[63];
	int Cur_CoulombCounter, rdc;

	/*axp gpio init */
	powerkeydev = input_allocate_device();
	if (!powerkeydev) {
		kfree(powerkeydev);
		return -ENODEV;
	}
	powerkeydev->name = pdev->name;
	powerkeydev->phys = "m1kbd/input2";
	powerkeydev->id.bustype = BUS_HOST;
	powerkeydev->id.vendor = 0x0001;
	powerkeydev->id.product = 0x0001;
	powerkeydev->id.version = 0x0100;
	powerkeydev->open = NULL;
	powerkeydev->close = NULL;
	powerkeydev->dev.parent = &pdev->dev;
	set_bit(EV_KEY, powerkeydev->evbit);
	set_bit(EV_REL, powerkeydev->evbit);
	set_bit(KEY_POWER, powerkeydev->keybit);
	ret = input_register_device(powerkeydev);
	if (ret)
		printk(KERN_ERR "Unable to Register the power key\n");
	if (pdata == NULL)
		return -EINVAL;
	if (pdata->chgcur > 2550000 || pdata->chgvol < 4100000
		|| pdata->chgvol > 4240000) {
		printk(KERN_ERR "charger milliamp is too high or target voltage is over range\n");
		return -EINVAL;
	}
	if (pdata->chgpretime < 40 || pdata->chgpretime > 70
		|| pdata->chgcsttime < 360 || pdata->chgcsttime > 720) {
		printk(KERN_ERR "prechaging time or constant current charging time is over range\n");
		return -EINVAL;
	}
	/*axp charger init */
	charger = kzalloc(sizeof(*charger), GFP_KERNEL);
	if (charger == NULL)
		return -ENOMEM;
	charger->master = pdev->dev.parent;
	charger->chgcur = pdata->chgcur;
	charger->chgvol = pdata->chgvol;
	charger->chgend = pdata->chgend;
	charger->sample_time = pdata->sample_time;
	charger->chgen = pdata->chgen;
	charger->chgpretime = pdata->chgpretime;
	charger->chgcsttime = pdata->chgcsttime;
	charger->battery_info = pdata->battery_info;
	charger->disvbat = 0;
	charger->disibat = 0;
	axp_charger = charger;
	ret = axp_battery_first_init(charger);
	if (ret)
		goto err_charger_init;

	axp_battery_setup_psy(charger);
	ret = power_supply_register(&pdev->dev, &charger->batt);
	if (ret)
		goto err_charger_init;

	ret = power_supply_register(&pdev->dev, &charger->ac);
	if (ret) {
		power_supply_unregister(&charger->batt);
		goto err_charger_init;
	}
#ifdef AXP259_WITH_USB
	ret = power_supply_register(&pdev->dev, &charger->usb);
	if (ret) {
		power_supply_unregister(&charger->ac);
		power_supply_unregister(&charger->batt);
		goto err_charger_init;
	}
#endif
	ret = axp_charger_create_attrs(&charger->batt);
	if (ret) {
		printk(KERN_ERR "cat notaxp_charger_create_attrs!!!===\n ");
		return ret;
	}
	platform_set_drvdata(pdev, charger);
#ifdef AXP259_WITH_USB
	/* usb voltage limit */
	if ((axp259_config.pmu_usbvol) && (axp259_config.pmu_usbvol_limit)) {
		axp_set_bits(charger->master, AXP259_CHARGE_VBUS, 0x40);
		var = axp259_config.pmu_usbvol * 1000;
		if (var >= 4000000 && var <= 4700000) {
			tmp = (var - 4000000) / 100000;
			axp_read(charger->master, AXP259_CHARGE_VBUS, &val);
			val &= 0xC7;
			val |= tmp << 3;
			axp_write(charger->master, AXP259_CHARGE_VBUS, val);
		}
	} else
		axp_clr_bits(charger->master, AXP259_CHARGE_VBUS, 0x40);
#endif
#ifdef AXP259_WITH_USB
	/*usb current limit */
	if ((axp259_config.pmu_usbcur) && (axp259_config.pmu_usbcur_limit)) {
		axp_clr_bits(charger->master, AXP259_CHARGE_VBUS, 0x02);
		var = axp259_config.pmu_usbcur * 1000;
		if (var == 900000)
			axp_clr_bits(charger->master, AXP259_CHARGE_VBUS, 0x03);
		else if (var == 500000)
			axp_set_bits(charger->master, AXP259_CHARGE_VBUS, 0x01);
	} else
		axp_set_bits(charger->master, AXP259_CHARGE_VBUS, 0x03);
#endif
	/* set lowe power warning/shutdown level */
	axp_write(charger->master, AXP259_WARNING_LEVEL,
		((axp259_config.pmu_battery_warning_level1 - 5) << 4)\
		+ axp259_config.pmu_battery_warning_level2);
	/*set target volatage */
	/*TODO*/

	/*set ocv para */
	ocv_cap[0] = axp259_config.pmu_bat_para1;
	ocv_cap[1] = 0xC1;
	ocv_cap[2] = axp259_config.pmu_bat_para2;
	ocv_cap[3] = 0xC2;
	ocv_cap[4] = axp259_config.pmu_bat_para3;
	ocv_cap[5] = 0xC3;
	ocv_cap[6] = axp259_config.pmu_bat_para4;
	ocv_cap[7] = 0xC4;
	ocv_cap[8] = axp259_config.pmu_bat_para5;
	ocv_cap[9] = 0xC5;
	ocv_cap[10] = axp259_config.pmu_bat_para6;
	ocv_cap[11] = 0xC6;
	ocv_cap[12] = axp259_config.pmu_bat_para7;
	ocv_cap[13] = 0xC7;
	ocv_cap[14] = axp259_config.pmu_bat_para8;
	ocv_cap[15] = 0xC8;
	ocv_cap[16] = axp259_config.pmu_bat_para9;
	ocv_cap[17] = 0xC9;
	ocv_cap[18] = axp259_config.pmu_bat_para10;
	ocv_cap[19] = 0xCA;
	ocv_cap[20] = axp259_config.pmu_bat_para11;
	ocv_cap[21] = 0xCB;
	ocv_cap[22] = axp259_config.pmu_bat_para12;
	ocv_cap[23] = 0xCC;
	ocv_cap[24] = axp259_config.pmu_bat_para13;
	ocv_cap[25] = 0xCD;
	ocv_cap[26] = axp259_config.pmu_bat_para14;
	ocv_cap[27] = 0xCE;
	ocv_cap[28] = axp259_config.pmu_bat_para15;
	ocv_cap[29] = 0xCF;
	ocv_cap[30] = axp259_config.pmu_bat_para16;
	ocv_cap[31] = 0xD0;
	ocv_cap[32] = axp259_config.pmu_bat_para17;
	ocv_cap[33] = 0xD1;
	ocv_cap[34] = axp259_config.pmu_bat_para18;
	ocv_cap[35] = 0xD2;
	ocv_cap[36] = axp259_config.pmu_bat_para19;
	ocv_cap[37] = 0xD3;
	ocv_cap[38] = axp259_config.pmu_bat_para20;
	ocv_cap[39] = 0xD4;
	ocv_cap[40] = axp259_config.pmu_bat_para21;
	ocv_cap[41] = 0xD5;
	ocv_cap[42] = axp259_config.pmu_bat_para22;
	ocv_cap[43] = 0xD6;
	ocv_cap[44] = axp259_config.pmu_bat_para23;
	ocv_cap[45] = 0xD7;
	ocv_cap[46] = axp259_config.pmu_bat_para24;
	ocv_cap[47] = 0xD8;
	ocv_cap[48] = axp259_config.pmu_bat_para25;
	ocv_cap[49] = 0xD9;
	ocv_cap[50] = axp259_config.pmu_bat_para26;
	ocv_cap[51] = 0xDA;
	ocv_cap[52] = axp259_config.pmu_bat_para27;
	ocv_cap[53] = 0xDB;
	ocv_cap[54] = axp259_config.pmu_bat_para28;
	ocv_cap[55] = 0xDC;
	ocv_cap[56] = axp259_config.pmu_bat_para29;
	ocv_cap[57] = 0xDD;
	ocv_cap[58] = axp259_config.pmu_bat_para30;
	ocv_cap[59] = 0xDE;
	ocv_cap[60] = axp259_config.pmu_bat_para31;
	ocv_cap[61] = 0xDF;
	ocv_cap[62] = axp259_config.pmu_bat_para32;
	axp_writes(charger->master, 0xC0, 63, ocv_cap);
	/* pok open time set */
	axp_read(charger->master, AXP259_POK_SET, &val);
	/*128ms, 1s, 2s, 3s */
	if (axp259_config.pmu_pekon_time < 1000)
		val &= 0x3f;
	else if (axp259_config.pmu_pekon_time < 2000) {
		val &= 0x3f;
		val |= 0x40;
	} else if (axp259_config.pmu_pekon_time < 3000) {
		val &= 0x3f;
		val |= 0x80;
	} else {
		val &= 0x3f;
		val |= 0xc0;
	}
	axp_write(charger->master, AXP259_POK_SET, val);
	/* pok long time set */
	/* 1s,1.5s,2s,2.5s */
	if (axp259_config.pmu_peklong_time < 1000)
		axp259_config.pmu_peklong_time = 1000;
	if (axp259_config.pmu_peklong_time > 2500)
		axp259_config.pmu_peklong_time = 2500;
	axp_read(charger->master, AXP259_POK_SET, &val);
	val &= 0xcf;
	val |= (((axp259_config.pmu_peklong_time - 1000) / 500) << 4);
	axp_write(charger->master, AXP259_POK_SET, val);
	/* pek offlevel poweroff en set */
	if (axp259_config.pmu_pekoff_en)
		axp259_config.pmu_pekoff_en = 1;
	else
		axp259_config.pmu_pekoff_en = 0;
	axp_read(charger->master, AXP259_POK_SET, &val);
	val &= 0xf7;
	val |= (axp259_config.pmu_pekoff_en << 3);
	axp_write(charger->master, AXP259_POK_SET, val);
	/*Init offlevel restart or not */
	if (axp259_config.pmu_pekoff_func)
		axp_set_bits(charger->master,
			AXP259_POK_SET, 0x04);/*restart*/
	else
		axp_clr_bits(charger->master,
			AXP259_POK_SET, 0x04);/*not restart*/
#if 0
	/* pek delay set */
	axp_read(charger->master, AXP259_OFF_CTL, &val);
	val &= 0xfc;
	if (axp259_config.pmu_pwrok_time < 32)
		val |= ((axp259_config.pmu_pwrok_time / 8) - 1);
	else
		val |= ((axp259_config.pmu_pwrok_time / 32) + 1);
	axp_write(charger->master, AXP259_OFF_CTL, val);
#endif

	/* pek offlevel time set */
	if (axp259_config.pmu_pekoff_time < 4000)
		axp259_config.pmu_pekoff_time = 4000;
	if (axp259_config.pmu_pekoff_time > 10000)
		axp259_config.pmu_pekoff_time = 10000;
	axp259_config.pmu_pekoff_time = (axp259_config.pmu_pekoff_time - 4000)\
					/ 2000;
	axp_read(charger->master, AXP259_POK_SET, &val);
	val &= 0xfc;
	val |= axp259_config.pmu_pekoff_time;
	axp_write(charger->master, AXP259_POK_SET, val);
	/*Init 16's Reset PMU en */
	if (axp259_config.pmu_reset)
		axp_set_bits(charger->master, 0x29, 0x04);	/*enable*/
	else
		axp_clr_bits(charger->master, 0x29, 0x04);	/*disable*/
	/*Init IRQ wakeup en */
	if (axp259_config.pmu_IRQ_wakeup)
		axp_set_bits(charger->master, 0x26, 0x40);	/*enable*/
	else
		axp_clr_bits(charger->master, 0x26, 0x40);	/*disable*/

	/*Init CHGLED function */
        if (axp259_config.pmu_chgled_func) {
                switch (axp259_config.pmu_chgled_type) {
                        case 0:
                        default:
                                /* type A */
                                axp_clr_bits(charger->master, 0x90, 0x07);
                                break;
                        case 1:
                                /* type B */
                                axp_set_bits(charger->master, 0x90, 0x01);
                                break;
                        case 2:
                                /* breath */
                                axp_set_bits(charger->master, 0x90, 0x02);
                                break;
                        case 3:
                                /* three state */
                                axp_set_bits(charger->master, 0x90, 0x04);
                                break;
                }
        } else {
                switch (axp259_config.pmu_chgled_type) {
                        case 0:
                        default:
                                /* software, detail config in reg90[5:3] */
                                axp_set_bits(charger->master, 0x90, 0x07);
                                break;
                        case 1:
                                /* breath, detail config in reg91~9A */
                                axp_set_bits(charger->master, 0x90, 0x03);
                                break;
                        case 2:
                                /* pwm, detail config in reg95~99 */
                                axp_set_bits(charger->master, 0x90, 0x05);
                                break;
                }
        }

	/*Init PMU Over Temperature protection */
	if (axp259_config.pmu_hot_shutdowm)
		axp_set_bits(charger->master, 0xf3, 0x08);	/*enable*/
	else
		axp_clr_bits(charger->master, 0xf3, 0x08);	/*disable*/

	/*enable SW_5V */
	axp_set_bits(charger->master, 0x10, 0x02);
	/*dc voltage setting to default value when wakeup */
	axp_set_bits(charger->master, 0x28, 0x20);
	/*disable restart pmic when pwrok drive low */
	axp_set_bits(charger->master, 0x29, 0x10);

/*TODO*/
	/*Init battery capacity correct function */
	if (axp259_config.pmu_batt_cap_correct)
		axp_set_bits(charger->master, 0xb8, 0x20);	/*enable*/
	else
		axp_clr_bits(charger->master, 0xb8, 0x20);	/*disable*/
#if 0
	/* Init battery regulator enable or not when charge finish */
	if (axp259_config.pmu_bat_regu_en)
		axp_set_bits(charger->master, 0x34, 0x20);	/*enable*/
	else
		axp_clr_bits(charger->master, 0x34, 0x20);	/*disable*/
#endif
	if (!axp259_config.pmu_batdeten)
		axp_clr_bits(charger->master, 0x22, 0x80);
	else
		axp_set_bits(charger->master, 0x22, 0x80);
	/* RDC initial */
	axp_read(charger->master, AXP259_RDC0, &val2);
	if ((axp259_config.pmu_battery_rdc) && (!(val2 & 0x40))) {
		rdc = (axp259_config.pmu_battery_rdc * 10000 + 5371) / 10742;
		axp_write(charger->master,
			AXP259_RDC0, ((rdc >> 8) & 0x1F) | 0x80);
		axp_write(charger->master, AXP259_RDC1, rdc & 0x00FF);
	}

	axp_read(charger->master, AXP259_BATCAP0, &val2);
	if ((axp259_config.pmu_battery_cap) && (!(val2 & 0x80))) {
		Cur_CoulombCounter = axp259_config.pmu_battery_cap * \
					1000 / 1456;
		axp_write(charger->master,
			AXP259_BATCAP0, ((Cur_CoulombCounter >> 8) | 0x80));
		axp_write(charger->master,
			AXP259_BATCAP1, Cur_CoulombCounter & 0x00FF);
	} else if (!axp259_config.pmu_battery_cap) {
		axp_write(charger->master, AXP259_BATCAP0, 0x00);
		axp_write(charger->master, AXP259_BATCAP1, 0x00);
	}
	axp_charger_update_state((struct axp_charger *)charger);
	axp_read(charger->master, AXP259_CAP, &val2);
	charger->rest_vol = (int)(val2 & 0x7F);
	if (axp_debug)
		DBG_PSY_MSG("now_rest_vol = %d\n", (val2 & 0x7F));
	charger->interval = msecs_to_jiffies(10 * 1000);
	INIT_DELAYED_WORK(&charger->work, axp_charging_monitor);
	schedule_delayed_work(&charger->work, charger->interval);
#ifdef AXP259_WITH_USB
	setup_timer(&charger->usb_status_timer,
		axp_charger_update_usb_state,
		(unsigned long)charger);
	/* set usb cur-vol limit */
	INIT_DELAYED_WORK(&usbwork, axp_usb);
	axp_usb_ac_check_status(charger);
	power_supply_changed(&charger->ac);
	power_supply_changed(&charger->usb);
	schedule_delayed_work(&usbwork, msecs_to_jiffies(30 * 1000));
#else
	power_supply_changed(&charger->ac);
#endif

#ifdef CONFIG_HAS_WAKELOCK
	wake_lock_init(&axp_wakeup_lock, WAKE_LOCK_SUSPEND, "axp_wakeup_lock");
#endif

	charger->nb.notifier_call = axp_battery_event;
	ret = axp_register_notifier(charger->master,
		&charger->nb, AXP259_NOTIFIER_ON);
	if (ret) {
		printk(KERN_ERR "axp register irq notifier fialed\n");
		goto err_notifier;
	}
#ifdef CONFIG_HAS_EARLYSUSPEND
	axp_early_suspend.suspend = axp_earlysuspend;
	axp_early_suspend.resume = axp_lateresume;
	axp_early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 2;
	register_early_suspend(&axp_early_suspend);
#endif

	class_register(&axppower_class);

	if (0 != axp259_config.pmu_temp_enable) {
		axp_write(charger->master,
			AXP259_VLTF_CHARGE,
			axp259_config.pmu_charge_ltf * 10 / 128);
		axp_write(charger->master,
			AXP259_VHTF_CHARGE,
			axp259_config.pmu_charge_htf * 10 / 128);
		axp_write(charger->master,
			AXP259_VLTF_WORK,
			axp259_config.pmu_charge_ltf * 10 / 128);
		axp_write(charger->master,
			AXP259_VHTF_WORK,
			axp259_config.pmu_charge_htf * 10 / 128);
	}

	return ret;

err_notifier:
	cancel_delayed_work_sync(&charger->work);

err_charger_init:
	kfree(charger);
	input_unregister_device(powerkeydev);
	kfree(powerkeydev);
	return ret;
}

static int axp_battery_remove(struct platform_device *dev)
{
	struct axp_charger *charger = platform_get_drvdata(dev);

	if (main_task) {
		kthread_stop(main_task);
		main_task = NULL;
	}
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&axp_early_suspend);
#endif
	axp_unregister_notifier(charger->master,
				&charger->nb, AXP259_NOTIFIER_ON);
#ifdef CONFIG_HAS_WAKELOCK
	wake_lock_destroy(&axp_wakeup_lock);
#endif
	cancel_delayed_work_sync(&charger->work);
	power_supply_unregister(&charger->ac);
	power_supply_unregister(&charger->batt);
	kfree(charger);
	input_unregister_device(powerkeydev);
	kfree(powerkeydev);
	return 0;
}

static int axp259_suspend(struct platform_device *dev, pm_message_t state)
{
	uint8_t irq_w[9];
	struct axp_charger *charger = platform_get_drvdata(dev);

#ifdef CONFIG_HAS_WAKELOCK
	if (wake_lock_active(&axp_wakeup_lock)) {
		printk(KERN_ERR "AXP:axp_wakeup_lock wakeup system\n");
		return -EPERM;
	}
#endif
	cancel_delayed_work_sync(&charger->work);
	/*clear all irqs events */
	irq_w[0] = 0xff;
	irq_w[1] = AXP259_INTSTS2;
	irq_w[2] = 0xff;
	irq_w[3] = AXP259_INTSTS3;
	irq_w[4] = 0xff;
	irq_w[5] = AXP259_INTSTS4;
	irq_w[6] = 0xff;
	irq_w[7] = AXP259_INTSTS5;
	irq_w[8] = 0xff;
	axp_writes(charger->master, AXP259_INTSTS1, 9, irq_w);
	/* close all irqs */
	axp_unregister_notifier(charger->master,
				&charger->nb, AXP259_NOTIFIER_ON);
#ifdef CONFIG_ARCH_SUN8IW5P1
	if (axp259_config.pmu_chg_temp_en) {
		axp259x_chg_current_limit(axp259_config.pmu_suspend_chgcur / 2);
		axp259x_chg_current_temp_limit(
			axp259_config.pmu_suspend_chgcur,
			axp259_config.pmu_suspend_chg_temp);
	} else
		axp259x_chg_current_limit(axp259_config.pmu_suspend_chgcur);
#else
	axp259x_chg_current_limit(axp259_config.pmu_suspend_chgcur);
#endif

	return 0;
}

static int axp259_resume(struct platform_device *dev)
{
	struct axp_charger *charger = platform_get_drvdata(dev);
	int pre_rest_vol;
	uint8_t val;

	/*wakeup IQR notifier work sequence */
	axp_register_notifier(charger->master,
			&charger->nb, AXP259_NOTIFIER_ON);
	axp_charger_update_state(charger);
	pre_rest_vol = charger->rest_vol;
	axp_read(charger->master, AXP259_CAP, &val);
	charger->rest_vol = val & 0x7f;
	if (charger->rest_vol - pre_rest_vol) {
		printk(KERN_DEBUG"battery vol change: %d->%d\n",
			pre_rest_vol, charger->rest_vol);
		pre_rest_vol = charger->rest_vol;
		axp_write(charger->master,
			AXP259_DATA_BUFFER0, charger->rest_vol | 0x80);
	}
#ifdef CONFIG_ARCH_SUN8IW5P1
	if (axp_debug)
		DBG_PSY_MSG("pmu_runtime_chgcur = %d\n",
			axp259_config.pmu_runtime_chgcur);
	if (axp259_config.pmu_chg_temp_en) {
		axp259x_chg_current_limit(
			axp259_config.pmu_runtime_chgcur / 2);
		axp259x_chg_current_temp_limit(
			axp259_config.pmu_runtime_chgcur,
			axp259_config.pmu_runtime_chg_temp);
	} else
		axp259x_chg_current_limit(axp259_config.pmu_runtime_chgcur);
#else
	axp259x_chg_current_limit(axp259_config.pmu_runtime_chgcur);
#endif

	charger->disvbat = 0;
	charger->disibat = 0;
	axp_change(charger);
	schedule_delayed_work(&charger->work, charger->interval);
	return 0;
}

static void axp259_shutdown(struct platform_device *dev)
{
	struct axp_charger *charger = platform_get_drvdata(dev);

	cancel_delayed_work_sync(&charger->work);

#ifdef CONFIG_ARCH_SUN8IW5P1
	printk(KERN_DEBUG"pmu_shutdown_chgcur = %d\n",
		axp259_config.pmu_shutdown_chgcur);
	if (axp259_config.pmu_chg_temp_en) {
		axp259x_chg_current_limit(
			axp259_config.pmu_shutdown_chgcur);
		axp259x_chg_current_temp_limit(
			axp259_config.pmu_shutdown_chgcur, 0);
	} else
		axp259x_chg_current_limit(axp259_config.pmu_shutdown_chgcur);
#else
	axp259x_chg_current_limit(axp259_config.pmu_shutdown_chgcur);
#endif

}

static struct platform_driver axp_battery_driver = {
	.driver = {
		   .name = "axp259-supplyer",
		   .owner = THIS_MODULE,
		   },
	.probe = axp_battery_probe,
	.remove = axp_battery_remove,
	.suspend = axp259_suspend,
	.resume = axp259_resume,
	.shutdown = axp259_shutdown,
};

static int axp_battery_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&axp_battery_driver);
	return ret;
}

static void axp_battery_exit(void)
{
	platform_driver_unregister(&axp_battery_driver);
}

subsys_initcall(axp_battery_init);
module_exit(axp_battery_exit);

MODULE_DESCRIPTION("AXP259 battery charger driver");
MODULE_AUTHOR("Forever Cai");
MODULE_LICENSE("GPL");
