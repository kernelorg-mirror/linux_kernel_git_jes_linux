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
	struct rtl8188eu_efuse *efuse = &priv->efuse_wifi.efuse8188eu;
	int i;

	if (efuse->rtl_id != cpu_to_le16(0x8129))
		return -EINVAL;

	ether_addr_copy(priv->mac_addr, efuse->mac_addr);

	memcpy(priv->cck_tx_power_index_A, efuse->cck_tx_power_index_A,
	       sizeof(efuse->cck_tx_power_index_A));
	memcpy(priv->cck_tx_power_index_B, efuse->cck_tx_power_index_B,
	       sizeof(efuse->cck_tx_power_index_B));

	memcpy(priv->ht40_1s_tx_power_index_A,
	       priv->efuse_wifi.efuse8188eu.ht40_1s_tx_power_index_A,
	       sizeof(priv->ht40_1s_tx_power_index_A));
	memcpy(priv->ht40_1s_tx_power_index_B,
	       priv->efuse_wifi.efuse8188eu.ht40_1s_tx_power_index_B,
	       sizeof(priv->ht40_1s_tx_power_index_B));

	dev_info(&priv->udev->dev, "Vendor: %.7s\n", efuse->vendor_name);
	dev_info(&priv->udev->dev, "Product: %.11s\n", efuse->device_name);
	dev_info(&priv->udev->dev, "Serial: %.11s\n", efuse->serial);

	if (rtl8xxxu_debug & RTL8XXXU_DEBUG_EFUSE) {
		unsigned char *raw = priv->efuse_wifi.raw;

		dev_info(&priv->udev->dev,
			 "%s: dumping efuse (0x%02zx bytes):\n",
			 __func__, sizeof(struct rtl8188eu_efuse));
		for (i = 0; i < sizeof(struct rtl8188eu_efuse); i += 8)
			dev_info(&priv->udev->dev, "%02x: %8ph\n", i, &raw[i]);
	}

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

static void rtl8188e_disabled_to_emu(struct rtl8xxxu_priv *priv)
{
	u16 val16;

	val16 = rtl8xxxu_read16(priv, REG_APS_FSMCO);
	val16 &= ~(APS_FSMCO_PFM_WOWL | APS_FSMCO_ENABLE_POWERDOWN);
	rtl8xxxu_write16(priv, REG_APS_FSMCO, val16);
}

static int rtl8188e_emu_to_active(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	u32 val32;
	u16 val16;
	int count, ret = 0;

	/* wait till 0x04[17] = 1 power ready*/
	for (count = RTL8XXXU_MAX_REG_POLL; count; count--) {
		val32 = rtl8xxxu_read32(priv, REG_APS_FSMCO);
		if (val32 & BIT(17))
			break;

		udelay(10);
	}

	if (!count) {
		ret = -EBUSY;
		goto exit;
	}

	/* reset baseband */
	val8 = rtl8xxxu_read8(priv, REG_SYS_FUNC);
	val8 &= ~(SYS_FUNC_BBRSTB | SYS_FUNC_BB_GLB_RSTN);
	rtl8xxxu_write8(priv, REG_SYS_FUNC, val8);

	/*0x24[23] = 2b'01 schmit trigger */
	val32 = rtl8xxxu_read32(priv, REG_AFE_XTAL_CTRL);
	val32 |= BIT(23);
	rtl8xxxu_write32(priv, REG_AFE_XTAL_CTRL, val32);

	/* 0x04[15] = 0 disable HWPDN (control by DRV)*/
	val16 = rtl8xxxu_read16(priv, REG_APS_FSMCO);
	val16 &= ~APS_FSMCO_HW_POWERDOWN;
	rtl8xxxu_write16(priv, REG_APS_FSMCO, val16);

	/*0x04[12:11] = 2b'00 disable WL suspend*/
	val16 = rtl8xxxu_read16(priv, REG_APS_FSMCO);
	val16 &= ~(APS_FSMCO_HW_SUSPEND | APS_FSMCO_PCIE);
	rtl8xxxu_write16(priv, REG_APS_FSMCO, val16);

	/* set, then poll until 0 */
	val32 = rtl8xxxu_read32(priv, REG_APS_FSMCO);
	val32 |= APS_FSMCO_MAC_ENABLE;
	rtl8xxxu_write32(priv, REG_APS_FSMCO, val32);

	for (count = RTL8XXXU_MAX_REG_POLL; count; count--) {
		val32 = rtl8xxxu_read32(priv, REG_APS_FSMCO);
		if ((val32 & APS_FSMCO_MAC_ENABLE) == 0) {
			ret = 0;
			break;
		}
		udelay(10);
	}

	if (!count) {
		ret = -EBUSY;
		goto exit;
	}

	/* LDO normal mode*/
	val8 = rtl8xxxu_read8(priv, REG_LPLDO_CTRL);
	val8 &= ~BIT(4);
	rtl8xxxu_write8(priv, REG_LPLDO_CTRL, val8);

exit:
	return ret;
}

static int rtl8188eu_power_on(struct rtl8xxxu_priv *priv)
{
	u16 val16;
	int ret;

	rtl8188e_disabled_to_emu(priv);

	ret = rtl8188e_emu_to_active(priv);
	if (ret)
		goto exit;

	/*
	 * Enable MAC DMA/WMAC/SCHEDULE/SEC block
	 * Set CR bit10 to enable 32k calibration.
	 * We do not set CR_MAC_TX_ENABLE | CR_MAC_RX_ENABLE here
	 * due to a hardware bug in the 88E, requiring those to be
	 * set after REG_TRXFF_BNDY is set. If not the RXFF bundary
	 * will get set to a larger buffer size than the real buffer
	 * size.
	 */
	val16 = (CR_HCI_TXDMA_ENABLE | CR_HCI_RXDMA_ENABLE |
		 CR_TXDMA_ENABLE | CR_RXDMA_ENABLE |
		 CR_PROTOCOL_ENABLE | CR_SCHEDULE_ENABLE |
		 CR_SECURITY_ENABLE | CR_CALTIMER_ENABLE);
	rtl8xxxu_write16(priv, REG_CR, val16);

exit:
	return ret;
}

static void rtl8188e_usb_quirks(struct rtl8xxxu_priv *priv)
{
	u16 val16;
	u32 val32;

	/*
	 * Technically this is not a USB quirk, but a chip quirk.
	 * This has to be done after REG_TRXFF_BNDY is set, see
	 * rtl8188eu_power_on() for details.
	 */
	val16 = rtl8xxxu_read16(priv, REG_CR);
	val16 |= (CR_MAC_TX_ENABLE | CR_MAC_RX_ENABLE);
	rtl8xxxu_write16(priv, REG_CR, val16);

	val32 = rtl8xxxu_read32(priv, REG_TXDMA_OFFSET_CHK);
	val32 |= TXDMA_OFFSET_DROP_DATA_EN;
	rtl8xxxu_write32(priv, REG_TXDMA_OFFSET_CHK, val32);
}

struct rtl8xxxu_fileops rtl8188eu_fops = {
	.parse_efuse = rtl8188eu_parse_efuse,
	.load_firmware = rtl8188eu_load_firmware,
	.power_on = rtl8188eu_power_on,
	.reset_8051 = rtl8xxxu_reset_8051,
	.usb_quirks = rtl8188e_usb_quirks,
	/*
	 * Use 9K for 8188e normal chip
	 * Max RX buffer = 10K - max(TxReportSize(64*8), WOLPattern(16*24))
	 */
	.trxff_boundary = 0x23ff,
	.total_page_num = TX_TOTAL_PAGE_NUM_8188E,
	.page_num_hi = TX_PAGE_NUM_HI_PQ_8188E,
	.page_num_lo = TX_PAGE_NUM_LO_PQ_8188E,
	.page_num_norm = TX_PAGE_NUM_NORM_PQ_8188E,
};
