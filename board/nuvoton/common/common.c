// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2023 Nuvoton Technology Corp.
 */

#include <common.h>
#include <clk.h>
#include <dm.h>
#include <env.h>
#include <serial.h>
#include <fdtdec.h>
#include <linux/delay.h>

#define UART_DLL	0x0
#define UART_DLM	0x4
#define UART_LCR	0xc
#define LCR_DLAB	BIT(7)

#if defined(CONFIG_CMD_SAVEENV)
/* allow save the environment data, the same handle in board_r.c */
static int should_save_env(void)
{
	if (IS_ENABLED(CONFIG_OF_CONTROL))
		return fdtdec_get_config_int(gd->fdt_blob,
						"load-environment", 1);

	if (IS_ENABLED(CONFIG_DELAY_ENVIRONMENT))
		return 0;

	return 1;
}

int board_save_default_env(void)
{
	if (should_save_env()) {
		/* env data ready */
		if (gd->flags & GD_FLG_ENV_READY) {
			/* use default environment, save it */
			if (gd->flags & GD_FLG_ENV_DEFAULT) {
				env_save();
			}
		}
	}

	return 0;
}
#else
int board_save_default_env(void)
{
	return 0;
}
#endif

int board_set_console(void)
{
	const unsigned long baudrate_table[] = CFG_SYS_BAUDRATE_TABLE;
	struct udevice *dev = gd->cur_serial_dev;
	unsigned int baudrate, max_delta;
	void __iomem *uart_reg;
	struct clk clk;
	char string[32];
	u32 uart_clk;
	u8 dll, dlm;
	u16 divisor;
	int ret, i;
	char *bootargs = NULL;
	char *tty_offset = NULL;
	char *tty_console = NULL;
	bool tty_configured = false;

	/* If uart init is skipped, update the actual baudrate in boot arguments */
	if (!IS_ENABLED(CONFIG_SYS_SKIP_UART_INIT))
		return 0;
	if (!dev)
		return 0;
	uart_reg = dev_read_addr_ptr(dev);
	ret = clk_get_by_index(dev, 0, &clk);
	if (ret)
		return 0;
	uart_clk = clk_get_rate(&clk);
	setbits_8(uart_reg + UART_LCR, LCR_DLAB);
	dll = readb(uart_reg + UART_DLL);
	dlm = readb(uart_reg + UART_DLM);
	clrbits_8(uart_reg + UART_LCR, LCR_DLAB);
	divisor = dll | (dlm << 8);
	baudrate =  uart_clk / ((16 * (divisor + 2)));
	for (i = 0; i < ARRAY_SIZE(baudrate_table); ++i) {
		max_delta = baudrate_table[i] / 20;
		if (abs(baudrate - baudrate_table[i]) < max_delta) {
			/* The baudrate is supported */
			gd->baudrate = baudrate_table[i];
			break;
		}
	}
	if (i == ARRAY_SIZE(baudrate_table)) {
		/* current baudrate is not suitable, set to default */
		divisor = DIV_ROUND_CLOSEST(uart_clk, 16 * gd->baudrate) - 2;
		setbits_8(uart_reg + UART_LCR, LCR_DLAB);
		writeb(divisor & 0xff, uart_reg + UART_DLL);
		writeb(divisor >> 8, uart_reg + UART_DLM);
		clrbits_8(uart_reg + UART_LCR, LCR_DLAB);
		udelay(100);
		printf("\r\nUART(source %u): change baudrate from %u to %u\n",
		       uart_clk, baudrate, uart_clk / ((16 * (divisor + 2))));
	}
	debug("Set env baudrate=%u\n", gd->baudrate);

	bootargs = env_get("bootargs");
	if (bootargs) {
		bootargs = strdup(bootargs);
		if (bootargs) {
			tty_offset = strstr(bootargs, "tty");
			if (tty_offset)
				tty_console = strsep(&tty_offset, ",");

			if (tty_console) {
				snprintf(string, sizeof(string), "%s,%un8", tty_console, gd->baudrate);
				tty_configured = true;
			}

			free(bootargs);
		}
	}

	if (!tty_configured)
		snprintf(string, sizeof(string), "ttyS0,%un8", gd->baudrate);

	env_set("console", string);

	return 0;
}
