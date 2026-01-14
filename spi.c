// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012 - 2018 Microchip Technology Inc., and its subsidiaries.
 * All rights reserved.
 */

#include <linux/clk.h>
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/crc7.h>
#include <linux/crc-itu-t.h>
#include <linux/version.h>

#include "netdev.h"
#include "cfg80211.h"

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 15, 0))
/*
 * Table for CRC-7 (polynomial x^7 + x^3 + 1).
 * This is a big-endian CRC (msbit is highest power of x),
 * aligned so the msbit of the byte is the x^6 coefficient
 * and the lsbit is not used.
 */
static const u8 crc7_be_syndrome_table[256] = {
    0x00, 0x12, 0x24, 0x36, 0x48, 0x5a, 0x6c, 0x7e,
    0x90, 0x82, 0xb4, 0xa6, 0xd8, 0xca, 0xfc, 0xee,
    0x32, 0x20, 0x16, 0x04, 0x7a, 0x68, 0x5e, 0x4c,
    0xa2, 0xb0, 0x86, 0x94, 0xea, 0xf8, 0xce, 0xdc,
    0x64, 0x76, 0x40, 0x52, 0x2c, 0x3e, 0x08, 0x1a,
    0xf4, 0xe6, 0xd0, 0xc2, 0xbc, 0xae, 0x98, 0x8a,
    0x56, 0x44, 0x72, 0x60, 0x1e, 0x0c, 0x3a, 0x28,
    0xc6, 0xd4, 0xe2, 0xf0, 0x8e, 0x9c, 0xaa, 0xb8,
    0xc8, 0xda, 0xec, 0xfe, 0x80, 0x92, 0xa4, 0xb6,
    0x58, 0x4a, 0x7c, 0x6e, 0x10, 0x02, 0x34, 0x26,
    0xfa, 0xe8, 0xde, 0xcc, 0xb2, 0xa0, 0x96, 0x84,
    0x6a, 0x78, 0x4e, 0x5c, 0x22, 0x30, 0x06, 0x14,
    0xac, 0xbe, 0x88, 0x9a, 0xe4, 0xf6, 0xc0, 0xd2,
    0x3c, 0x2e, 0x18, 0x0a, 0x74, 0x66, 0x50, 0x42,
    0x9e, 0x8c, 0xba, 0xa8, 0xd6, 0xc4, 0xf2, 0xe0,
    0x0e, 0x1c, 0x2a, 0x38, 0x46, 0x54, 0x62, 0x70,
    0x82, 0x90, 0xa6, 0xb4, 0xca, 0xd8, 0xee, 0xfc,
    0x12, 0x00, 0x36, 0x24, 0x5a, 0x48, 0x7e, 0x6c,
    0xb0, 0xa2, 0x94, 0x86, 0xf8, 0xea, 0xdc, 0xce,
    0x20, 0x32, 0x04, 0x16, 0x68, 0x7a, 0x4c, 0x5e,
    0xe6, 0xf4, 0xc2, 0xd0, 0xae, 0xbc, 0x8a, 0x98,
    0x76, 0x64, 0x52, 0x40, 0x3e, 0x2c, 0x1a, 0x08,
    0xd4, 0xc6, 0xf0, 0xe2, 0x9c, 0x8e, 0xb8, 0xaa,
    0x44, 0x56, 0x60, 0x72, 0x0c, 0x1e, 0x28, 0x3a,
    0x4a, 0x58, 0x6e, 0x7c, 0x02, 0x10, 0x26, 0x34,
    0xda, 0xc8, 0xfe, 0xec, 0x92, 0x80, 0xb6, 0xa4,
    0x78, 0x6a, 0x5c, 0x4e, 0x30, 0x22, 0x14, 0x06,
    0xe8, 0xfa, 0xcc, 0xde, 0xa0, 0xb2, 0x84, 0x96,
    0x2e, 0x3c, 0x0a, 0x18, 0x66, 0x74, 0x42, 0x50,
    0xbe, 0xac, 0x9a, 0x88, 0xf6, 0xe4, 0xd2, 0xc0,
    0x1c, 0x0e, 0x38, 0x2a, 0x54, 0x46, 0x70, 0x62,
    0x8c, 0x9e, 0xa8, 0xba, 0xc4, 0xd6, 0xe0, 0xf2
};

/**
 * crc7_be - update the CRC7 for the data buffer
 * @crc:     previous CRC7 value
 * @buffer:  data pointer
 * @len:     number of bytes in the buffer
 * Context: any
 *
 * Returns the updated CRC7 value.
 * The CRC7 is left-aligned in the byte (the lsbit is always 0), as that
 * makes the computation easier, and all callers want it in that form.
 *
 */
u8 crc7_be(u8 crc, const u8 *buffer, size_t len)
{
    while (len--)
	crc = crc7_be_syndrome_table[crc ^ *buffer++];
    return crc;
}

/* CRC table for the CRC ITU-T V.41 0x1021 (x^16 + x^12 + x^5 + 1) */
const u16 crc_itu_t_table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
    0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
    0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
    0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
    0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
    0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
    0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
    0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
    0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
    0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
    0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
    0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
    0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
    0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
    0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
    0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
    0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
    0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
    0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
    0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
    0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
    0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
    0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
};

/**
 * crc_itu_t - Compute the CRC-ITU-T for the data buffer
 *
 * @crc:     previous CRC value
 * @buffer:  data pointer
 * @len:     number of bytes in the buffer
 *
 * Returns the updated CRC value
 */
u16 crc_itu_t(u16 crc, const u8 *buffer, size_t len)
{
    while (len--)
	crc = crc_itu_t_byte(crc, *buffer++);
    return crc;
}
#endif

static bool enable_crc7;	/* protect SPI commands with CRC7 */
module_param(enable_crc7, bool, 0644);
MODULE_PARM_DESC(enable_crc7,
		 "Enable CRC7 checksum to protect command transfers\n"
		 "\t\t\tagainst corruption during the SPI transfer.\n"
		 "\t\t\tCommand transfers are short and the CPU-cycle cost\n"
		 "\t\t\tof enabling this is small.");

static bool enable_crc16;	/* protect SPI data with CRC16 */
module_param(enable_crc16, bool, 0644);
MODULE_PARM_DESC(enable_crc16,
		 "Enable CRC16 checksum to protect data transfers\n"
		 "\t\t\tagainst corruption during the SPI transfer.\n"
		 "\t\t\tData transfers can be large and the CPU-cycle cost\n"
		 "\t\t\tof enabling this may be substantial.");

/*
 * For CMD_SINGLE_READ and CMD_INTERNAL_READ, WILC may insert one or
 * more zero bytes between the command response and the DATA Start tag
 * (0xf3).  This behavior appears to be undocumented in "ATWILC1000
 * USER GUIDE" (https://tinyurl.com/4hhshdts) but we have observed 1-4
 * zero bytes when the SPI bus operates at 48MHz and none when it
 * operates at 1MHz.
 */
#define WILC_SPI_RSP_HDR_EXTRA_DATA	3

struct wilc_spi {
	bool probing_crc;	/* true if we're probing chip's CRC config */
	bool crc7_enabled;	/* true if crc7 is currently enabled */
	bool crc16_enabled;	/* true if crc16 is currently enabled */
	bool is_init;
};

static const struct wilc_hif_func wilc_hif_spi;

static int wilc_spi_reset(struct wilc *wilc);

/********************************************
 *
 *      Spi protocol Function
 *
 ********************************************/

#define CMD_DMA_WRITE				0xc1
#define CMD_DMA_READ				0xc2
#define CMD_INTERNAL_WRITE			0xc3
#define CMD_INTERNAL_READ			0xc4
#define CMD_TERMINATE				0xc5
#define CMD_REPEAT				0xc6
#define CMD_DMA_EXT_WRITE			0xc7
#define CMD_DMA_EXT_READ			0xc8
#define CMD_SINGLE_WRITE			0xc9
#define CMD_SINGLE_READ				0xca
#define CMD_RESET				0xcf

#define SPI_RESP_RETRY_COUNT			(10)
#define SPI_RETRY_COUNT				(10)
#define SPI_ENABLE_VMM_RETRY_LIMIT		2

/* SPI response fields (section 11.1.2 in ATWILC1000 User Guide): */
#define RSP_START_FIELD				GENMASK(7, 4)
#define RSP_TYPE_FIELD				GENMASK(3, 0)

/* SPI response values for the response fields: */
#define RSP_START_TAG				0xc
#define RSP_TYPE_FIRST_PACKET			0x1
#define RSP_TYPE_INNER_PACKET			0x2
#define RSP_TYPE_LAST_PACKET			0x3
#define RSP_STATE_NO_ERROR			0x00

#define PROTOCOL_REG_PKT_SZ_MASK		GENMASK(6, 4)
#define PROTOCOL_REG_CRC16_MASK			GENMASK(3, 3)
#define PROTOCOL_REG_CRC7_MASK			GENMASK(2, 2)

/*
 * The SPI data packet size may be any integer power of two in the
 * range from 256 to 8192 bytes.
 */
#define DATA_PKT_LOG_SZ_MIN			8	/* 256 B */
#define DATA_PKT_LOG_SZ_MAX			13	/* 8 KiB */

/*
 * Select the data packet size (log2 of number of bytes): Use the
 * maximum data packet size.  We only retransmit complete packets, so
 * there is no benefit from using smaller data packets.
 */
#define DATA_PKT_LOG_SZ				DATA_PKT_LOG_SZ_MAX
#define DATA_PKT_SZ				(1 << DATA_PKT_LOG_SZ)

#define USE_SPI_DMA				0

#define WILC_SPI_COMMAND_STAT_SUCCESS		0
#define WILC_GET_RESP_HDR_START(h)		(((h) >> 4) & 0xf)

struct wilc_spi_cmd {
	u8 cmd_type;
	union {
		struct {
			u8 addr[3];
			u8 crc[];
		} __packed simple_cmd;
		struct {
			u8 addr[3];
			u8 size[2];
			u8 crc[];
		} __packed dma_cmd;
		struct {
			u8 addr[3];
			u8 size[3];
			u8 crc[];
		} __packed dma_cmd_ext;
		struct {
			u8 addr[2];
			__be32 data;
			u8 crc[];
		} __packed internal_w_cmd;
		struct {
			u8 addr[3];
			__be32 data;
			u8 crc[];
		} __packed w_cmd;
	} u;
} __packed;

struct wilc_spi_read_rsp_data {
	u8 header;
	u8 data[4];
	u8 crc[];
} __packed;

struct wilc_spi_rsp_data {
	u8 rsp_cmd_type;
	u8 status;
	u8 data[];
} __packed;

struct wilc_spi_special_cmd_rsp {
	u8 skip_byte;
	u8 rsp_cmd_type;
	u8 status;
} __packed;

static int wilc_bus_probe(struct spi_device *spi)
{
	int ret;
	static bool init_power;
	struct wilc *wilc;
	struct device *dev = &spi->dev;
	struct wilc_spi *spi_priv;

	dev_info(&spi->dev, "spiModalias: %s, spiMax-Speed: %d\n",
			spi->modalias, spi->max_speed_hz);

	spi_priv = kzalloc(sizeof(*spi_priv), GFP_KERNEL);
	if (!spi_priv)
		return -ENOMEM;

	ret = wilc_cfg80211_init(&wilc, dev, WILC_HIF_SPI, &wilc_hif_spi);
	if (ret)
		goto free;

	spi_set_drvdata(spi, wilc);
	wilc->dev = &spi->dev;
	wilc->bus_data = spi_priv;
	wilc->dt_dev = &spi->dev;
	wilc->dev_irq_num = spi->irq;

	wilc->rtc_clk = devm_clk_get(&spi->dev, "rtc");
	if (PTR_ERR_OR_ZERO(wilc->rtc_clk) == -EPROBE_DEFER)
		goto netdev_cleanup;
	else if (!IS_ERR(wilc->rtc_clk))
		clk_prepare_enable(wilc->rtc_clk);

	ret = wilc_of_parse_power_pins(wilc);
	if (ret)
		goto disable_rtc_clk;

	if (!init_power) {
		wilc_wlan_power(wilc, false);
		init_power = 1;
		wilc_wlan_power(wilc, true);
	}

	wilc_bt_init(wilc);

	dev_info(dev, "WILC SPI probe success\n");
	return 0;

disable_rtc_clk:
	if (!IS_ERR(wilc->rtc_clk))
		clk_disable_unprepare(wilc->rtc_clk);
netdev_cleanup:
	wilc_netdev_cleanup(wilc);
free:
	kfree(spi_priv);
	return ret;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,18,0))
static void wilc_bus_remove(struct spi_device *spi)
#else
static int wilc_bus_remove(struct spi_device *spi)
#endif
{
	struct wilc *wilc = spi_get_drvdata(spi);

	if (!IS_ERR(wilc->rtc_clk))
		clk_disable_unprepare(wilc->rtc_clk);

	wilc_netdev_cleanup(wilc);
	wilc_bt_deinit();

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5,18,0))
	return 0;
#endif
}

static int wilc_spi_suspend(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);
	struct wilc *wilc = spi_get_drvdata(spi);

	dev_info(&spi->dev, "\n\n << SUSPEND >>\n\n");
	mutex_lock(&wilc->hif_cs);
	chip_wakeup(wilc, DEV_WIFI);

	if (mutex_is_locked(&wilc->hif_cs))
		mutex_unlock(&wilc->hif_cs);

	/* notify the chip that host will sleep */
	host_sleep_notify(wilc, DEV_WIFI);
	chip_allow_sleep(wilc, DEV_WIFI);
	mutex_lock(&wilc->hif_cs);

	return 0;
}

static int wilc_spi_resume(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);
	struct wilc *wilc = spi_get_drvdata(spi);

	dev_info(&spi->dev, "\n\n  <<RESUME>>\n\n");

	/* wake the chip to compelete the re-initialization */
	chip_wakeup(wilc, DEV_WIFI);

	if (mutex_is_locked(&wilc->hif_cs))
		mutex_unlock(&wilc->hif_cs);

	host_wakeup_notify(wilc, DEV_WIFI);

	mutex_lock(&wilc->hif_cs);

	chip_allow_sleep(wilc, DEV_WIFI);

	if (mutex_is_locked(&wilc->hif_cs))
		mutex_unlock(&wilc->hif_cs);

	return 0;
}

static const struct of_device_id wilc_of_match[] = {
	{ .compatible = "microchip,wilc1000", },
	{ .compatible = "microchip,wilc3000", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, wilc_of_match);

static const struct spi_device_id wilc_spi_id[] = {
	{ "wilc1000", 0 },
	{ "wilc3000", 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(spi, wilc_spi_id);

static const struct dev_pm_ops wilc_spi_pm_ops = {
	.suspend = wilc_spi_suspend,
	.resume = wilc_spi_resume,
};

static struct spi_driver wilc_spi_driver = {
	.driver = {
		.name = MODALIAS,
		.of_match_table = wilc_of_match,
		.pm = &wilc_spi_pm_ops,
	},
	.id_table = wilc_spi_id,
	.probe =  wilc_bus_probe,
	.remove = wilc_bus_remove,
};
module_spi_driver(wilc_spi_driver);
MODULE_LICENSE("GPL");
MODULE_VERSION("15.6");

static int wilc_spi_tx(struct wilc *wilc, u8 *b, u32 len)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	int ret;
	struct spi_message msg;

	if (len > 0 && b) {
		struct spi_transfer tr = {
			.tx_buf = b,
			.len = len,
#if KERNEL_VERSION(5, 5, 0) <= LINUX_VERSION_CODE
			.delay = {
			.value = 0,
			.unit = SPI_DELAY_UNIT_USECS
			},
#else
			.delay_usecs = 0,
#endif
		};
		char *r_buffer = kzalloc(len, GFP_KERNEL);

		if (!r_buffer)
			return -ENOMEM;

		tr.rx_buf = r_buffer;
		dev_dbg(&spi->dev, "Request writing %d bytes\n", len);

		memset(&msg, 0, sizeof(msg));
		spi_message_init(&msg);
		msg.spi = spi;
#if KERNEL_VERSION(6, 10, 0) > LINUX_VERSION_CODE
		msg.is_dma_mapped = USE_SPI_DMA;
#endif
		spi_message_add_tail(&tr, &msg);

		ret = spi_sync(spi, &msg);
		if (ret < 0)
			dev_err(&spi->dev, "SPI transaction failed\n");

		kfree(r_buffer);
	} else {
		dev_err(&spi->dev,
			"can't write data with the following length: %d\n",
			len);
		ret = -EINVAL;
	}

	return ret;
}

static int wilc_spi_rx(struct wilc *wilc, u8 *rb, u32 rlen)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	int ret;

	if (rlen > 0) {
		struct spi_message msg;
		struct spi_transfer tr = {
			.rx_buf = rb,
			.len = rlen,
#if KERNEL_VERSION(5, 5, 0) <= LINUX_VERSION_CODE
						.delay = {
						.value = 0,
						.unit = SPI_DELAY_UNIT_USECS
						},
#else
			.delay_usecs = 0,
#endif
		};
		char *t_buffer = kzalloc(rlen, GFP_KERNEL);

		if (!t_buffer)
			return -ENOMEM;

		tr.tx_buf = t_buffer;

		memset(&msg, 0, sizeof(msg));
		spi_message_init(&msg);
		msg.spi = spi;
#if KERNEL_VERSION(6, 10, 0) > LINUX_VERSION_CODE
		msg.is_dma_mapped = USE_SPI_DMA;
#endif
		spi_message_add_tail(&tr, &msg);

		ret = spi_sync(spi, &msg);
		if (ret < 0)
			dev_err(&spi->dev, "SPI transaction failed\n");
		kfree(t_buffer);
	} else {
		dev_err(&spi->dev,
			"can't read data with the following length: %u\n",
			rlen);
		ret = -EINVAL;
	}

	return ret;
}

static int wilc_spi_tx_rx(struct wilc *wilc, u8 *wb, u8 *rb, u32 rlen)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	int ret;

	if (rlen > 0) {
		struct spi_message msg;
		struct spi_transfer tr = {
			.rx_buf = rb,
			.tx_buf = wb,
			.len = rlen,
			.bits_per_word = 8,
#if KERNEL_VERSION(5, 5, 0) <= LINUX_VERSION_CODE
						.delay = {
						.value = 0,
						.unit = SPI_DELAY_UNIT_USECS
						},
#else
			.delay_usecs = 0,
#endif
		};

		memset(&msg, 0, sizeof(msg));
		spi_message_init(&msg);
		msg.spi = spi;
#if KERNEL_VERSION(6, 10, 0) > LINUX_VERSION_CODE
		msg.is_dma_mapped = USE_SPI_DMA;
#endif

		spi_message_add_tail(&tr, &msg);
		ret = spi_sync(spi, &msg);
		if (ret < 0)
			dev_err(&spi->dev, "SPI transaction failed\n");
	} else {
		dev_err(&spi->dev,
			"can't read data with the following length: %u\n",
			rlen);
		ret = -EINVAL;
	}

	return ret;
}

static int spi_data_write(struct wilc *wilc, u8 *b, u32 sz)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	struct wilc_spi *spi_priv = wilc->bus_data;
	int ix, nbytes;
	int result = 0;
	u8 cmd, order, crc[2];
	u16 crc_calc;

	/*
	 * Data
	 */
	ix = 0;
	do {
		if (sz <= DATA_PKT_SZ) {
			nbytes = sz;
			order = 0x3;
		} else {
			nbytes = DATA_PKT_SZ;
			if (ix == 0)
				order = 0x1;
			else
				order = 0x02;
		}

		/*
		 * Write command
		 */
		cmd = 0xf0;
		cmd |= order;

		if (wilc_spi_tx(wilc, &cmd, 1)) {
			dev_err(&spi->dev,
				"Failed data block cmd write, bus error...\n");
			result = -EINVAL;
			break;
		}

		/*
		 * Write data
		 */
		if (wilc_spi_tx(wilc, &b[ix], nbytes)) {
			dev_err(&spi->dev,
				"Failed data block write, bus error...\n");
			result = -EINVAL;
			break;
		}

		/*
		 * Write CRC
		 */
		if (spi_priv->crc16_enabled) {
			crc_calc = crc_itu_t(0xffff, &b[ix], nbytes);
			crc[0] = crc_calc >> 8;
			crc[1] = crc_calc;
			if (wilc_spi_tx(wilc, crc, 2)) {
				dev_err(&spi->dev, "Failed data block crc write, bus error...\n");
				result = -EINVAL;
				break;
			}
		}

		/*
		 * No need to wait for response
		 */
		ix += nbytes;
		sz -= nbytes;
	} while (sz);

	return result;
}

/********************************************
 *
 *      Spi Internal Read/Write Function
 *
 ********************************************/
static u8 wilc_get_crc7(u8 *buffer, u32 len)
{
	return crc7_be(0xfe, buffer, len);
}

static int wilc_spi_single_read(struct wilc *wilc, u8 cmd, u32 adr, void *b,
				u8 clockless)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	struct wilc_spi *spi_priv = wilc->bus_data;
	u8 wb[32], rb[32];
	int cmd_len, resp_len, i;
	u16 crc_calc, crc_recv;
	struct wilc_spi_cmd *c;
	struct wilc_spi_rsp_data *r;
	struct wilc_spi_read_rsp_data *r_data;

	memset(wb, 0x0, sizeof(wb));
	memset(rb, 0x0, sizeof(rb));
	c = (struct wilc_spi_cmd *)wb;
	c->cmd_type = cmd;
	if (cmd == CMD_SINGLE_READ) {
		c->u.simple_cmd.addr[0] = adr >> 16;
		c->u.simple_cmd.addr[1] = adr >> 8;
		c->u.simple_cmd.addr[2] = adr;
	} else if (cmd == CMD_INTERNAL_READ) {
		c->u.simple_cmd.addr[0] = adr >> 8;
		if (clockless == 1)
			c->u.simple_cmd.addr[0] |= BIT(7);
		c->u.simple_cmd.addr[1] = adr;
		c->u.simple_cmd.addr[2] = 0x0;
	} else {
		dev_err(&spi->dev, "cmd [%x] not supported\n", cmd);
		return -EINVAL;
	}

	cmd_len = offsetof(struct wilc_spi_cmd, u.simple_cmd.crc);
	resp_len = sizeof(*r) + sizeof(*r_data) + WILC_SPI_RSP_HDR_EXTRA_DATA;

	if (spi_priv->crc7_enabled) {
		c->u.simple_cmd.crc[0] = wilc_get_crc7(wb, cmd_len);
		cmd_len += 1;
		resp_len += 2;
	}

	if (cmd_len + resp_len > ARRAY_SIZE(wb)) {
		dev_err(&spi->dev,
			"spi buffer size too small (%d) (%d) (%zu)\n",
			cmd_len, resp_len, ARRAY_SIZE(wb));
		return -EINVAL;
	}

	if (wilc_spi_tx_rx(wilc, wb, rb, cmd_len + resp_len)) {
		dev_err(&spi->dev, "Failed cmd write, bus error...\n");
		return -EINVAL;
	}

	r = (struct wilc_spi_rsp_data *)&rb[cmd_len];
	/*
	 * Clockless registers operations might return unexptected responses,
	 * even if successful.
	 */
	if (r->rsp_cmd_type != cmd && !clockless) {
		if (!spi_priv->probing_crc)
			dev_err(&spi->dev,
				"Failed cmd response, cmd (%02x), resp (%02x)\n",
				cmd, r->rsp_cmd_type);
		return -EINVAL;
	}

	if (r->status != WILC_SPI_COMMAND_STAT_SUCCESS && !clockless) {
		dev_err(&spi->dev, "Failed cmd state response state (%02x)\n",
			r->status);
		return -EINVAL;
	}

	for (i = 0; i < SPI_RESP_RETRY_COUNT; ++i)
		if (WILC_GET_RESP_HDR_START(r->data[i]) == 0xf)
			break;

	if (i >= SPI_RESP_RETRY_COUNT) {
		dev_err(&spi->dev, "Error, data start missing\n");
		return -EINVAL;
	}

	r_data = (struct wilc_spi_read_rsp_data *)&r->data[i];

	if (b)
		memcpy(b, r_data->data, 4);

	if (!clockless && spi_priv->crc16_enabled) {
		crc_recv = (r_data->crc[0] << 8) | r_data->crc[1];
		crc_calc = crc_itu_t(0xffff, r_data->data, 4);
		if (crc_recv != crc_calc) {
			dev_err(&spi->dev, "%s: bad CRC 0x%04x "
				"(calculated 0x%04x)\n", __func__,
				crc_recv, crc_calc);
			return -EINVAL;
		}
	}

	return 0;
}

static int wilc_spi_write_cmd(struct wilc *wilc, u8 cmd, u32 adr, u32 data,
			      u8 clockless)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	struct wilc_spi *spi_priv = wilc->bus_data;
	u8 wb[32], rb[32];
	int cmd_len, resp_len;
	struct wilc_spi_cmd *c;
	struct wilc_spi_rsp_data *r;

	memset(wb, 0x0, sizeof(wb));
	memset(rb, 0x0, sizeof(rb));
	c = (struct wilc_spi_cmd *)wb;
	c->cmd_type = cmd;
	if (cmd == CMD_INTERNAL_WRITE) {
		c->u.internal_w_cmd.addr[0] = adr >> 8;
		if (clockless == 1)
			c->u.internal_w_cmd.addr[0] |= BIT(7);

		c->u.internal_w_cmd.addr[1] = adr;
		c->u.internal_w_cmd.data = cpu_to_be32(data);
		cmd_len = offsetof(struct wilc_spi_cmd, u.internal_w_cmd.crc);
		if (spi_priv->crc7_enabled)
			c->u.internal_w_cmd.crc[0] = wilc_get_crc7(wb, cmd_len);
	} else if (cmd == CMD_SINGLE_WRITE) {
		c->u.w_cmd.addr[0] = adr >> 16;
		c->u.w_cmd.addr[1] = adr >> 8;
		c->u.w_cmd.addr[2] = adr;
		c->u.w_cmd.data = cpu_to_be32(data);
		cmd_len = offsetof(struct wilc_spi_cmd, u.w_cmd.crc);
		if (spi_priv->crc7_enabled)
			c->u.w_cmd.crc[0] = wilc_get_crc7(wb, cmd_len);
	} else {
		dev_err(&spi->dev, "write cmd [%x] not supported\n", cmd);
		return -EINVAL;
	}

	if (spi_priv->crc7_enabled)
		cmd_len += 1;

	resp_len = sizeof(*r);

	if (cmd_len + resp_len > ARRAY_SIZE(wb)) {
		dev_err(&spi->dev,
			"spi buffer size too small (%d) (%d) (%zu)\n",
			cmd_len, resp_len, ARRAY_SIZE(wb));
		return -EINVAL;
	}

	if (wilc_spi_tx_rx(wilc, wb, rb, cmd_len + resp_len)) {
		dev_err(&spi->dev, "Failed cmd write, bus error...\n");
		return -EINVAL;
	}

	r = (struct wilc_spi_rsp_data *)&rb[cmd_len];
	/*
	 * Clockless registers operations might return unexptected responses,
	 * even if successful.
	 */
	if (r->rsp_cmd_type != cmd && !clockless) {
		dev_err(&spi->dev,
			"Failed cmd response, cmd (%02x), resp (%02x)\n",
			cmd, r->rsp_cmd_type);
		return -EINVAL;
	}

	if (r->status != WILC_SPI_COMMAND_STAT_SUCCESS && !clockless) {
		dev_err(&spi->dev, "Failed cmd state response state (%02x)\n",
			r->status);
		return -EINVAL;
	}

	return 0;
}

static int wilc_spi_dma_rw(struct wilc *wilc, u8 cmd, u32 adr, u8 *b, u32 sz)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	struct wilc_spi *spi_priv = wilc->bus_data;
	u16 crc_recv, crc_calc;
	u8 wb[32], rb[32];
	int cmd_len, resp_len;
	int retry, ix = 0;
	u8 crc[2];
	struct wilc_spi_cmd *c;
	struct wilc_spi_rsp_data *r;

	memset(wb, 0x0, sizeof(wb));
	memset(rb, 0x0, sizeof(rb));
	c = (struct wilc_spi_cmd *)wb;
	c->cmd_type = cmd;
	if (cmd == CMD_DMA_WRITE || cmd == CMD_DMA_READ) {
		c->u.dma_cmd.addr[0] = adr >> 16;
		c->u.dma_cmd.addr[1] = adr >> 8;
		c->u.dma_cmd.addr[2] = adr;
		c->u.dma_cmd.size[0] = sz >> 8;
		c->u.dma_cmd.size[1] = sz;
		cmd_len = offsetof(struct wilc_spi_cmd, u.dma_cmd.crc);
		if (spi_priv->crc7_enabled)
			c->u.dma_cmd.crc[0] = wilc_get_crc7(wb, cmd_len);
	} else if (cmd == CMD_DMA_EXT_WRITE || cmd == CMD_DMA_EXT_READ) {
		c->u.dma_cmd_ext.addr[0] = adr >> 16;
		c->u.dma_cmd_ext.addr[1] = adr >> 8;
		c->u.dma_cmd_ext.addr[2] = adr;
		c->u.dma_cmd_ext.size[0] = sz >> 16;
		c->u.dma_cmd_ext.size[1] = sz >> 8;
		c->u.dma_cmd_ext.size[2] = sz;
		cmd_len = offsetof(struct wilc_spi_cmd, u.dma_cmd_ext.crc);
		if (spi_priv->crc7_enabled)
			c->u.dma_cmd_ext.crc[0] = wilc_get_crc7(wb, cmd_len);
	} else {
		dev_err(&spi->dev, "dma read write cmd [%x] not supported\n",
			cmd);
		return -EINVAL;
	}
	if (spi_priv->crc7_enabled)
		cmd_len += 1;

	resp_len = sizeof(*r);

	if (cmd_len + resp_len > ARRAY_SIZE(wb)) {
		dev_err(&spi->dev, "spi buffer size too small (%d)(%d) (%zu)\n",
			cmd_len, resp_len, ARRAY_SIZE(wb));
		return -EINVAL;
	}

	if (wilc_spi_tx_rx(wilc, wb, rb, cmd_len + resp_len)) {
		dev_err(&spi->dev, "Failed cmd write, bus error...\n");
		return -EINVAL;
	}

	r = (struct wilc_spi_rsp_data *)&rb[cmd_len];
	if (r->rsp_cmd_type != cmd) {
		dev_err(&spi->dev,
			"Failed cmd response, cmd (%02x), resp (%02x)\n",
			cmd, r->rsp_cmd_type);
		return -EINVAL;
	}

	if (r->status != WILC_SPI_COMMAND_STAT_SUCCESS) {
		dev_err(&spi->dev, "Failed cmd state response state (%02x)\n",
			r->status);
		return -EINVAL;
	}

	if (cmd == CMD_DMA_WRITE || cmd == CMD_DMA_EXT_WRITE)
		return 0;

	while (sz > 0) {
		int nbytes;
		u8 rsp;

		if (sz <= DATA_PKT_SZ)
			nbytes = sz;
		else
			nbytes = DATA_PKT_SZ;

		/*
		 * Data Response header
		 */
		retry = SPI_RESP_RETRY_COUNT;
		do {
			if (wilc_spi_rx(wilc, &rsp, 1)) {
				dev_err(&spi->dev,
					"Failed resp read, bus err\n");
				return -EINVAL;
			}
			if (WILC_GET_RESP_HDR_START(rsp) == 0xf)
				break;
		} while (retry--);

		/*
		 * Read bytes
		 */
		if (wilc_spi_rx(wilc, &b[ix], nbytes)) {
			dev_err(&spi->dev,
				"Failed block read, bus err\n");
			return -EINVAL;
		}

		/*
		 * Read CRC
		 */
		if (spi_priv->crc16_enabled) {
			if (wilc_spi_rx(wilc, crc, 2)) {
				dev_err(&spi->dev,
					"Failed block CRC read, bus err\n");
				return -EINVAL;
			}
			crc_recv = (crc[0] << 8) | crc[1];
			crc_calc = crc_itu_t(0xffff, &b[ix], nbytes);
			if (crc_recv != crc_calc) {
				dev_err(&spi->dev, "%s: bad CRC 0x%04x "
					"(calculated 0x%04x)\n", __func__,
					crc_recv, crc_calc);
				return -EINVAL;
			}
		}

		ix += nbytes;
		sz -= nbytes;
	}
	return 0;
}

static int wilc_spi_special_cmd(struct wilc *wilc, u8 cmd)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	struct wilc_spi *spi_priv = wilc->bus_data;
	u8 wb[32], rb[32];
	int cmd_len, resp_len = 0;
	struct wilc_spi_cmd *c;
	struct wilc_spi_special_cmd_rsp *r;

	if (cmd != CMD_TERMINATE && cmd != CMD_REPEAT && cmd != CMD_RESET)
		return -EINVAL;

	memset(wb, 0x0, sizeof(wb));
	memset(rb, 0x0, sizeof(rb));
	c = (struct wilc_spi_cmd *)wb;
	c->cmd_type = cmd;

	if (cmd == CMD_RESET)
		memset(c->u.simple_cmd.addr, 0xFF, 3);

	cmd_len = offsetof(struct wilc_spi_cmd, u.simple_cmd.crc);
	resp_len = sizeof(*r);

	if (spi_priv->crc7_enabled) {
		c->u.simple_cmd.crc[0] = wilc_get_crc7(wb, cmd_len);
		cmd_len += 1;
	}
	if (cmd_len + resp_len > ARRAY_SIZE(wb)) {
		dev_err(&spi->dev, "spi buffer size too small (%d) (%d) (%zu)\n",
			cmd_len, resp_len, ARRAY_SIZE(wb));
		return -EINVAL;
	}

	if (wilc_spi_tx_rx(wilc, wb, rb, cmd_len + resp_len)) {
		dev_err(&spi->dev, "Failed cmd write, bus error...\n");
		return -EINVAL;
	}

	r = (struct wilc_spi_special_cmd_rsp *)&rb[cmd_len];
	if (r->rsp_cmd_type != cmd) {
		if (!spi_priv->probing_crc)
			dev_err(&spi->dev,
				"Failed cmd response, cmd (%02x), resp (%02x)\n",
				cmd, r->rsp_cmd_type);
		return -EINVAL;
	}

	if (r->status != WILC_SPI_COMMAND_STAT_SUCCESS) {
		dev_err(&spi->dev, "Failed cmd state response state (%02x)\n",
			r->status);
		return -EINVAL;
	}
	return 0;
}

static int wilc_spi_read_reg(struct wilc *wilc, u32 addr, u32 *data)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	u8 retry = SPI_RETRY_COUNT;
	int result;
	u8 cmd = CMD_SINGLE_READ;
	u8 clockless = 0;

retry:
	if (addr <= WILC_SPI_CLOCKLESS_ADDR_LIMIT) {
		/* Clockless register */
		cmd = CMD_INTERNAL_READ;
		clockless = 1;
	} else {
		cmd = CMD_SINGLE_READ;
		clockless = 0;
	}

	result = wilc_spi_single_read(wilc, cmd, addr, data, clockless);
	if (result) {
		dev_err(&spi->dev, "Failed cmd, read reg (%08x)...\n", addr);
		goto fail;
	}

	le32_to_cpus(data);

fail:
	if (result && !clockless) {
		usleep_range(1000, 1100);
		wilc_spi_reset(wilc);
		dev_warn(&spi->dev, "Reset and retry %d %x\n", retry, addr);
		usleep_range(1000, 1100);
		retry--;
		if (retry)
			goto retry;
	}
	return result;
}

static int wilc_spi_read(struct wilc *wilc, u32 addr, u8 *buf, u32 size)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	int result;
	u8 retry = SPI_RETRY_COUNT;

	if (size <= 4)
		return -EINVAL;

retry:
	result = wilc_spi_dma_rw(wilc, CMD_DMA_EXT_READ, addr, buf, size);
	if (result) {
		dev_err(&spi->dev, "Failed cmd, read block (%08x)...\n", addr);
		goto fail;
	}

fail:
	if (result) {
		usleep_range(1000, 1100);
		wilc_spi_reset(wilc);
		dev_warn(&spi->dev, "Reset and retry %d %x %d\n", retry, addr,
			 size);
		usleep_range(1000, 1100);
		retry--;
		if (retry)
			goto retry;
	}
	return result;
}

static int spi_internal_write(struct wilc *wilc, u32 adr, u32 dat)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	int result;
	u8 retry = SPI_RETRY_COUNT;

retry:
	result = wilc_spi_write_cmd(wilc, CMD_INTERNAL_WRITE, adr, dat, 0);
	if (result) {
		dev_err(&spi->dev, "Failed internal write cmd...\n");
		goto fail;
	}

fail:
	if (result) {
		usleep_range(1000, 1100);
		wilc_spi_reset(wilc);
		dev_err(&spi->dev, "Reset and retry %d %x\n", retry, adr);
		usleep_range(1000, 1100);
		retry--;
		if (retry)
			goto retry;
	}
	return result;
}

static int spi_internal_read(struct wilc *wilc, u32 adr, u32 *data)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	struct wilc_spi *spi_priv = wilc->bus_data;
	int result;
	u8 retry = SPI_RETRY_COUNT;

retry:
	result = wilc_spi_single_read(wilc, CMD_INTERNAL_READ, adr, data, 0);
	if (result) {
		if (!spi_priv->probing_crc)
			dev_err(&spi->dev, "Failed internal read cmd...\n");
		goto fail;
	}

	le32_to_cpus(data);

fail:
	if (result) {
		usleep_range(1000, 1100);
		wilc_spi_reset(wilc);
		/* ignore while probing with CRC enable and disabled */
		if (!spi_priv->probing_crc)
			dev_err(&spi->dev, "Reset and retry %d %x\n", retry, adr);
		usleep_range(1000, 1100);
		retry--;
		if (retry)
			goto retry;
	}
	return result;
}

/********************************************
 *
 *      Spi interfaces
 *
 ********************************************/

static int wilc_spi_write_reg(struct wilc *wilc, u32 addr, u32 data)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	u8 retry = SPI_RETRY_COUNT;
	int result;
	u8 cmd = CMD_SINGLE_WRITE;
	u8 clockless = 0;

retry:
	if (addr <= WILC_SPI_CLOCKLESS_ADDR_LIMIT) {
		/* Clockless register */
		cmd = CMD_INTERNAL_WRITE;
		clockless = 1;
	} else {
		cmd = CMD_SINGLE_WRITE;
		clockless = 0;
	}

	result = wilc_spi_write_cmd(wilc, cmd, addr, data, clockless);
	if (result) {
		dev_err(&spi->dev, "Failed cmd, write reg (%08x)...\n", addr);
		goto fail;
	}

fail:
	if (result && !clockless) {
		usleep_range(1000, 1100);
		wilc_spi_reset(wilc);
		dev_err(&spi->dev,
			"Reset and retry %d %x %d\n", retry, addr, data);
		usleep_range(1000, 1100);
		retry--;
		if (retry)
			goto retry;
	}
	return result;
}

static int spi_data_rsp(struct wilc *wilc, u8 cmd)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	int result, i;
	u8 rsp[4];

	/*
	 * The response to data packets is two bytes long.  For
	 * efficiency's sake, wilc_spi_write() wisely ignores the
	 * responses for all packets but the final one.  The downside
	 * of that optimization is that when the final data packet is
	 * short, we may receive (part of) the response to the
	 * second-to-last packet before the one for the final packet.
	 * To handle this, we always read 4 bytes and then search for
	 * the last byte that contains the "Response Start" code (0xc
	 * in the top 4 bits).  We then know that this byte is the
	 * first response byte of the final data packet.
	 */
	result = wilc_spi_rx(wilc, rsp, sizeof(rsp));
	if (result) {
		dev_err(&spi->dev, "Failed bus error...\n");
		return result;
	}

	for (i = sizeof(rsp) - 2; i >= 0; --i)
		if (FIELD_GET(RSP_START_FIELD, rsp[i]) == RSP_START_TAG)
			break;

	if (i < 0) {
		dev_err(&spi->dev,
			"Data packet response missing (%02x %02x %02x %02x)\n",
			rsp[0], rsp[1], rsp[2], rsp[3]);
		return -1;
	}

	/* rsp[i] is the last response start byte */

	if (FIELD_GET(RSP_TYPE_FIELD, rsp[i]) != RSP_TYPE_LAST_PACKET
	    || rsp[i + 1] != RSP_STATE_NO_ERROR) {
		dev_err(&spi->dev, "Data response error (%02x %02x)\n",
			rsp[i], rsp[i + 1]);
		return -1;
	}
	return 0;
}

static int wilc_spi_write(struct wilc *wilc, u32 addr, u8 *buf, u32 size)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	int result;
	u8 retry = SPI_RETRY_COUNT;

	/*
	 * has to be greated than 4
	 */
	if (size <= 4)
		return -EINVAL;

retry:
	result = wilc_spi_dma_rw(wilc, CMD_DMA_EXT_WRITE, addr, NULL, size);
	if (result) {
		dev_err(&spi->dev,
			"Failed cmd, write block (%08x)...\n", addr);
		goto fail;
	}

	/*
	 * Data
	 */
	result = spi_data_write(wilc, buf, size);
	if (result) {
		dev_err(&spi->dev, "Failed block data write...\n");
		goto fail;
	}
	/*
	 * Data response
	 */
	result = spi_data_rsp(wilc, CMD_DMA_EXT_WRITE);
	if (result) {
		dev_err(&spi->dev, "Failed block data write...\n");
		goto fail;
	}

fail:
	if (result) {
		usleep_range(1000, 1100);
		wilc_spi_reset(wilc);
		dev_err(&spi->dev,
			"Reset and retry %d %x %d\n", retry, addr, size);
		usleep_range(1000, 1100);
		retry--;
		if (retry)
			goto retry;
	}
	return result;
}

/********************************************
 *
 *      Bus interfaces
 *
 ********************************************/

static int wilc_spi_reset(struct wilc *wilc)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	struct wilc_spi *spi_priv = wilc->bus_data;
	int result;

	result = wilc_spi_special_cmd(wilc, CMD_RESET);
	if (result && !spi_priv->probing_crc)
		dev_err(&spi->dev, "Failed cmd reset\n");

	return result;
}

static bool wilc_spi_is_init(struct wilc *wilc)
{
	struct wilc_spi *spi_priv = wilc->bus_data;

	return spi_priv->is_init;
}

static int wilc_spi_deinit(struct wilc *wilc)
{
	struct wilc_spi *spi_priv = wilc->bus_data;

	spi_priv->is_init = false;
	wilc_wlan_power(wilc, false);

	return 0;
}

static int wilc_spi_clear_init(struct wilc *wilc)
{
	struct wilc_spi *spi_priv = wilc->bus_data;

	spi_priv->is_init = false;

	return 0;
}

static int wilc_spi_init(struct wilc *wilc, bool resume)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	struct wilc_spi *spi_priv = wilc->bus_data;
	u32 reg;
	u32 chipid;
	int ret, i;

	if (spi_priv->is_init) {
		ret = wilc_spi_read_reg(wilc, WILC_CHIPID, &chipid);
		if (ret)
			dev_err(&spi->dev, "Fail cmd read chip id...\n");

		return ret;
	}

	/*
	 * configure protocol
	 */

	/*
	 * Infer the CRC settings that are currently in effect.  This
	 * is necessary because we can't be sure that the chip has
	 * been RESET (e.g, after module unload and reload).
	 */
	spi_priv->probing_crc = true;
	spi_priv->crc7_enabled = enable_crc7;
	spi_priv->crc16_enabled = false; /* don't check CRC16 during probing */
	for (i = 0; i < 2; ++i) {
		ret = spi_internal_read(wilc, WILC_SPI_PROTOCOL_OFFSET, &reg);
		if (ret == 0)
			break;
		spi_priv->crc7_enabled = !enable_crc7;
	}
	if (ret) {
		dev_err(&spi->dev, "Failed with CRC7 on and off.\n");
		return ret;
	}

	/* set up the desired CRC configuration: */
	reg &= ~(PROTOCOL_REG_CRC7_MASK | PROTOCOL_REG_CRC16_MASK);
	if (enable_crc7)
		reg |= PROTOCOL_REG_CRC7_MASK;
	if (enable_crc16)
		reg |= PROTOCOL_REG_CRC16_MASK;

	/* set up the data packet size: */
	BUILD_BUG_ON(DATA_PKT_LOG_SZ < DATA_PKT_LOG_SZ_MIN
		     || DATA_PKT_LOG_SZ > DATA_PKT_LOG_SZ_MAX);
	reg &= ~PROTOCOL_REG_PKT_SZ_MASK;
	reg |= FIELD_PREP(PROTOCOL_REG_PKT_SZ_MASK,
			  DATA_PKT_LOG_SZ - DATA_PKT_LOG_SZ_MIN);

	/* establish the new setup: */
	ret = spi_internal_write(wilc, WILC_SPI_PROTOCOL_OFFSET, reg);
	if (ret) {
		dev_err(&spi->dev,
			"[wilc spi %d]: Failed internal write reg\n",
			__LINE__);
		return ret;
	}
	/* update our state to match new protocol settings: */
	spi_priv->crc7_enabled = enable_crc7;
	spi_priv->crc16_enabled = enable_crc16;

	/* re-read to make sure new settings are in effect: */
	spi_internal_read(wilc, WILC_SPI_PROTOCOL_OFFSET, &reg);

	spi_priv->probing_crc = false;

	/*
	 * make sure can read back chip id correctly
	 */
	ret = wilc_spi_read_reg(wilc, WILC_CHIPID, &chipid);
	if (ret) {
		dev_err(&spi->dev, "Fail cmd read chip id...\n");
		return ret;
	}

	if (!resume) {
		chipid = wilc_get_chipid(wilc, true);
		if (is_wilc3000(chipid)) {
			wilc->chip = WILC_3000;
		} else if (is_wilc1000(chipid)) {
			wilc->chip = WILC_1000;
		} else {
			dev_err(&spi->dev, "Unsupported chipid: %x\n", chipid);
			return -EINVAL;
		}
		dev_dbg(&spi->dev, "chipid %08x\n", chipid);
	}

	spi_priv->is_init = true;
	return 0;
}

static int wilc_spi_read_size(struct wilc *wilc, u32 *size)
{
	int ret;

	ret = spi_internal_read(wilc,
				WILC_SPI_INT_STATUS - WILC_SPI_REG_BASE, size);
	*size = FIELD_GET(IRQ_DMA_WD_CNT_MASK, *size);

	return ret;
}

static int wilc_spi_read_int(struct wilc *wilc, u32 *int_status)
{
	return spi_internal_read(wilc, WILC_SPI_INT_STATUS - WILC_SPI_REG_BASE,
				 int_status);
}

static int wilc_spi_clear_int_ext(struct wilc *wilc, u32 val)
{
	int ret;
	int retry = SPI_ENABLE_VMM_RETRY_LIMIT;
	u32 check;

	while (retry) {
		ret = spi_internal_write(wilc,
					 WILC_SPI_INT_CLEAR - WILC_SPI_REG_BASE,
					 val);
		if (ret)
			break;

		ret = spi_internal_read(wilc,
					WILC_SPI_INT_CLEAR - WILC_SPI_REG_BASE,
					&check);
		if (ret || ((check & EN_VMM) == (val & EN_VMM)))
			break;

		retry--;
	}
	return ret;
}

static int wilc_spi_sync_ext(struct wilc *wilc, int nint)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	u32 reg;
	int ret, i;

	if (nint > MAX_NUM_INT) {
		dev_err(&spi->dev, "Too many interrupts (%d)...\n", nint);
		return -EINVAL;
	}

	/*
	 * interrupt pin mux select
	 */
	ret = wilc_spi_read_reg(wilc, WILC_PIN_MUX_0, &reg);
	if (ret) {
		dev_err(&spi->dev, "Failed read reg (%08x)...\n",
			WILC_PIN_MUX_0);
		return ret;
	}
	reg |= BIT(8);
	ret = wilc_spi_write_reg(wilc, WILC_PIN_MUX_0, reg);
	if (ret) {
		dev_err(&spi->dev, "Failed write reg (%08x)...\n",
			WILC_PIN_MUX_0);
		return ret;
	}

	/*
	 * interrupt enable
	 */
	ret = wilc_spi_read_reg(wilc, WILC_INTR_ENABLE, &reg);
	if (ret) {
		dev_err(&spi->dev, "Failed read reg (%08x)...\n",
			WILC_INTR_ENABLE);
		return ret;
	}

	for (i = 0; (i < 5) && (nint > 0); i++, nint--)
		reg |= (BIT((27 + i)));

	ret = wilc_spi_write_reg(wilc, WILC_INTR_ENABLE, reg);
	if (ret) {
		dev_err(&spi->dev, "Failed write reg (%08x)...\n",
			WILC_INTR_ENABLE);
		return ret;
	}
	if (nint) {
		ret = wilc_spi_read_reg(wilc, WILC_INTR2_ENABLE, &reg);
		if (ret) {
			dev_err(&spi->dev, "Failed read reg (%08x)...\n",
				WILC_INTR2_ENABLE);
			return ret;
		}

		for (i = 0; (i < 3) && (nint > 0); i++, nint--)
			reg |= BIT(i);

		ret = wilc_spi_write_reg(wilc, WILC_INTR2_ENABLE, reg);
		if (ret) {
			dev_err(&spi->dev, "Failed write reg (%08x)...\n",
				WILC_INTR2_ENABLE);
			return ret;
		}
	}

	return 0;
}

/* Global spi HIF function table */
static const struct wilc_hif_func wilc_hif_spi = {
	.hif_init = wilc_spi_init,
	.hif_deinit = wilc_spi_deinit,
	.hif_read_reg = wilc_spi_read_reg,
	.hif_write_reg = wilc_spi_write_reg,
	.hif_block_rx = wilc_spi_read,
	.hif_block_tx = wilc_spi_write,
	.hif_read_int = wilc_spi_read_int,
	.hif_clear_int_ext = wilc_spi_clear_int_ext,
	.hif_read_size = wilc_spi_read_size,
	.hif_block_tx_ext = wilc_spi_write,
	.hif_block_rx_ext = wilc_spi_read,
	.hif_sync_ext = wilc_spi_sync_ext,
	.hif_reset = wilc_spi_reset,
	.hif_is_init = wilc_spi_is_init,
	.hif_clear_init = wilc_spi_clear_init,
};
