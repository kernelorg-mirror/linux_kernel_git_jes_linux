/*
 * RTL8XXXU mac80211 USB driver - 8188e specific subdriver
 *
 * Copyright (c) 2014 - 2016 Jes Sorensen <Jes.Sorensen@redhat.com>
 *
 * Portions, notably calibration code:
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This driver was written as a replacement for the vendor provided
 * rtl8723au driver. As the Realtek 8xxx chips are very similar in
 * their programming interface, I have started adding support for
 * additional 8xxx chips like the 8192cu, 8188cus, etc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/usb.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/wireless.h>
#include <linux/firmware.h>
#include <linux/moduleparam.h>
#include <net/mac80211.h>
#include "rtl8xxxu.h"
#include "rtl8xxxu_regs.h"

static int rtl8188eu_parse_efuse(struct rtl8xxxu_priv *priv)
{
	return 0;
}

static int rtl8188eu_load_firmware(struct rtl8xxxu_priv *priv)
{
	char *fw_name;
	int ret;

	fw_name = "rtlwifi/rtl8188eufw.bin";

	ret = rtl8xxxu_load_firmware(priv, fw_name);

	return -EINVAL;
	return ret;
}

struct rtl8xxxu_fileops rtl8188eu_fops = {
	.parse_efuse = rtl8188eu_parse_efuse,
	.load_firmware = rtl8188eu_load_firmware,
	.reset_8051 = rtl8xxxu_reset_8051,
};
