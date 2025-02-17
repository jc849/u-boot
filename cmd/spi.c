// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2002
 * Gerald Van Baren, Custom IDEAS, vanbaren@cideas.com
 */

/*
 * SPI Read/Write Utilities
 */

#include <common.h>
#include <command.h>
#include <dm.h>
#include <errno.h>
#include <spi.h>

/*-----------------------------------------------------------------------
 * Definitions
 */

#ifndef MAX_SPI_BYTES
#   define MAX_SPI_BYTES 32	/* Maximum number of bytes we can handle */
#endif

/*
 * Values from last command.
 */
static unsigned int	bus;
static unsigned int	cs;
static unsigned int	mode;
static unsigned int	freq;
static int   		tx_bitlen;
static int   		rx_bitlen;
static uchar 		dout[MAX_SPI_BYTES];
static uchar 		din[MAX_SPI_BYTES];

static int do_spi_xfer(int bus, int cs)
{
	struct spi_slave *slave;
	int ret = 0;

#if CONFIG_IS_ENABLED(DM_SPI)
	char name[30], *str;
	struct udevice *dev;

	snprintf(name, sizeof(name), "generic_%d:%d", bus, cs);
	str = strdup(name);
	if (!str)
		return -ENOMEM;
	ret = spi_get_bus_and_cs(bus, cs, freq, mode, "spi_generic_drv",
				 str, &dev, &slave);
	if (ret)
		return ret;
#else
	slave = spi_setup_slave(bus, cs, freq, mode);
	if (!slave) {
		printf("Invalid device %d:%d\n", bus, cs);
		return -EINVAL;
	}
#endif

	ret = spi_claim_bus(slave);
	if (ret)
		goto done;

	if (rx_bitlen) {
		/* rx_bitlen is specified, half-duplex transfer */
		spi_xfer(slave, tx_bitlen, dout, NULL, SPI_XFER_BEGIN);
		spi_xfer(slave, rx_bitlen, NULL, din, SPI_XFER_END);
	} else
		spi_xfer(slave, tx_bitlen, dout, din, SPI_XFER_BEGIN | SPI_XFER_END);
#if !CONFIG_IS_ENABLED(DM_SPI)
	/* We don't get an error code in this case */
	if (ret)
		ret = -EIO;
#endif
	if (ret) {
		printf("Error %d during SPI transaction\n", ret);
	} else {
		int j;

		/* rx_bitlen is not specified, full-duplex transfer */
		if (!rx_bitlen)
			rx_bitlen = tx_bitlen;
		for (j = 0; j < ((rx_bitlen + 7) / 8); j++)
			printf("%02X", din[j]);
		printf("\n");
	}
done:
	spi_release_bus(slave);
#if !CONFIG_IS_ENABLED(DM_SPI)
	spi_free_slave(slave);
#endif

	return ret;
}

/*
 * SPI read/write
 *
 * Syntax:
 *   spi {dev} {num_bits} {dout}
 *     {dev} is the device number for controlling chip select (see TBD)
 *     {num_bits} is the number of bits to send & receive (base 10)
 *     {dout} is a hexadecimal string of data to send
 * The command prints out the hexadecimal string received via SPI.
 */

int do_spi(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	char  *cp = 0;
	uchar tmp;
	int   j;

	rx_bitlen = 0;
	/*
	 * We use the last specified parameters, unless new ones are
	 * entered.
	 */
	if (freq == 0)
		freq = 1000000;

	if ((flag & CMD_FLAG_REPEAT) == 0)
	{
		if (argc >= 2) {
			mode = CONFIG_DEFAULT_SPI_MODE;
			bus = simple_strtoul(argv[1], &cp, 10);
			if (*cp == ':') {
				cs = simple_strtoul(cp+1, &cp, 10);
			} else {
				cs = bus;
				bus = CONFIG_DEFAULT_SPI_BUS;
			}
			if (*cp == '.')
				mode = simple_strtoul(cp+1, &cp, 10);
			if (*cp == '@')
				freq = simple_strtoul(cp+1, &cp, 10);
		}
		if (argc >= 3)
			tx_bitlen = simple_strtoul(argv[2], NULL, 10);
		if (argc >= 4) {
			cp = argv[3];
			for(j = 0; *cp; j++, cp++) {
				tmp = *cp - '0';
				if(tmp > 9)
					tmp -= ('A' - '0') - 10;
				if(tmp > 15)
					tmp -= ('a' - 'A');
				if(tmp > 15) {
					printf("Hex conversion error on %c\n", *cp);
					return 1;
				}
				if((j % 2) == 0)
					dout[j / 2] = (tmp << 4);
				else
					dout[j / 2] |= tmp;
			}
		}
		if (argc >= 5)
			rx_bitlen = simple_strtoul(argv[4], NULL, 10);
	}

	if ((tx_bitlen < 0) || (tx_bitlen >  (MAX_SPI_BYTES * 8))) {
		printf("Invalid tx_bitlen %d\n", tx_bitlen);
		return 1;
	}
	if ((rx_bitlen < 0) || (rx_bitlen >  (MAX_SPI_BYTES * 8))) {
		printf("Invalid rx_bitlen %d\n", rx_bitlen);
		return 1;
	}

	if (do_spi_xfer(bus, cs))
		return 1;

	return 0;
}

/***************************************************/

U_BOOT_CMD(
	sspi,	6,	1,	do_spi,
	"SPI utility command",
	"[<bus>:]<cs>[.<mode>][@<freq>] <tx_bitlen> <dout> <rx_bitlen> - Send and receive bits\n"
	"<bus>     - Identifies the SPI bus\n"
	"<cs>      - Identifies the chip select\n"
	"<mode>    - Identifies the SPI mode to use\n"
	"<freq>    - Identifies the SPI bus frequency in Hz\n"
	"<tx_bitlen> - Number of bits to send (base 10)\n"
	"<dout>    - Hexadecimal string that gets sent\n"
	"<rx_bitlen> - Number of bits to receive (base 10)\n"
	"if rx_bitlen is specified, do the half-duplex trasnfer (tx and then rx)\n"
	"if rx_bitlen is not specified, do the full-duplex transfer"
);
