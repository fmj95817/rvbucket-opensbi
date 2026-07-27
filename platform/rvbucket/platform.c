// SPDX-License-Identifier: BSD-2-Clause

#include <sbi/riscv_io.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_const.h>
#include <sbi/sbi_domain.h>
#include <sbi/sbi_ecall_interface.h>
#include <sbi/sbi_platform.h>
#include <sbi/sbi_system.h>
#include <sbi_utils/ipi/aclint_mswi.h>
#include <sbi_utils/irqchip/plic.h>
#include <sbi_utils/timer/aclint_mtimer.h>
#include "regs/sysctrl.h"
#include "regs/uart.h"
#include "spec/mmap.h"

#define RVBUCKET_HART_COUNT          2

#define RVBUCKET_PERI_DOMAIN_SIZE    0x01000000UL
#define RVBUCKET_ACLINT_MTIME_FREQ   10000UL
#define RVBUCKET_MTIMER_TICK_CYCLES  10000UL

#define RVBUCKET_PLIC_CONTEXT_SIZE   (0x200000UL + \
					  RVBUCKET_HART_COUNT * 0x1000UL)
#define RVBUCKET_PLIC_NUM_SOURCES    32

static void rvbucket_uart_putc(char ch)
{
	writel((u32)ch & RVB_UART_TX_REG_DATA_MASK,
	       (void *)(RVB_UART_BASE + RVB_UART_TX_REG_OFFSET));
}

static int rvbucket_uart_getc(void)
{
	if (!(readl((void *)(RVB_UART_BASE +
			    RVB_UART_STATUS_REG_OFFSET)) &
	      RVB_UART_STATUS_REG_RX_VALID_MASK))
		return -1;

	return readl((void *)(RVB_UART_BASE + RVB_UART_RX_REG_OFFSET)) &
	       RVB_UART_RX_REG_DATA_MASK;
}

static struct sbi_console_device rvbucket_console = {
	.name = "rvbucket-uart",
	.console_putc = rvbucket_uart_putc,
	.console_getc = rvbucket_uart_getc,
};

static struct plic_data plic = {
	.unique_id = 0,
	.addr = RVB_PLIC_BASE,
	.size = RVBUCKET_PLIC_CONTEXT_SIZE,
	.num_src = RVBUCKET_PLIC_NUM_SOURCES,
	.context_map = {
#if RVBUCKET_HART_COUNT >= 1
		[0] = { -1, 0 },
#endif
#if RVBUCKET_HART_COUNT >= 2
		[1] = { -1, 1 },
#endif
#if RVBUCKET_HART_COUNT >= 3
		[2] = { -1, 2 },
#endif
#if RVBUCKET_HART_COUNT >= 4
		[3] = { -1, 3 },
#endif
	},
};

static struct aclint_mswi_data mswi = {
	.addr = RVB_MSWI_BASE,
	.size = RVB_MSWI_SIZE,
	.first_hartid = 0,
	.hart_count = RVBUCKET_HART_COUNT,
};

static struct aclint_mtimer_data mtimer = {
	.mtime_freq = RVBUCKET_ACLINT_MTIME_FREQ,
	.mtime_addr = RVB_MTIMER_BASE,
	.mtime_size = RVB_MTIMER_SIZE,
	.mtimecmp_addr = RVB_MTIMECMP_BASE,
	.mtimecmp_size = 0x8 * RVBUCKET_HART_COUNT,
	.first_hartid = 0,
	.hart_count = RVBUCKET_HART_COUNT,
	.has_64bit_mmio = false,
};

static int rvbucket_system_reset_check(u32 type, u32 reason)
{
	switch (type) {
	case SBI_SRST_RESET_TYPE_SHUTDOWN:
	case SBI_SRST_RESET_TYPE_COLD_REBOOT:
	case SBI_SRST_RESET_TYPE_WARM_REBOOT:
		return 1;
	default:
		return 0;
	}
}

static void rvbucket_system_reset(u32 type, u32 reason)
{
	rvb_sysctrl_pwr_mngm_req_reg_t pwr_req = { .raw = 0 };

	if (type == SBI_SRST_RESET_TYPE_SHUTDOWN) {
		pwr_req.fields.cmd =
			RVB_SYSCTRL_PWR_MNGM_REQ_REG_CMD_POWEROFF;
		pwr_req.fields.arg =
			RVB_SYSCTRL_PWR_MNGM_REQ_REG_ARG_NORMAL;
		writel(pwr_req.raw,
		       (void *)(RVB_SYSCTRL_BASE +
				RVB_SYSCTRL_PWR_MNGM_REQ_REG_OFFSET));
	} else {
		writel(RVB_SYSCTRL_RESET_REQ_REG_VALUE_MAGIC,
		       (void *)(RVB_SYSCTRL_BASE +
				RVB_SYSCTRL_RESET_REQ_REG_OFFSET));
	}
	while (1)
		wfi();
}

static struct sbi_system_reset_device rvbucket_reset = {
	.name = "rvbucket-reset",
	.system_reset_check = rvbucket_system_reset_check,
	.system_reset = rvbucket_system_reset,
};

static int rvbucket_nascent_init(void)
{
	sbi_console_set_device(&rvbucket_console);
	return 0;
}

static bool rvbucket_cold_boot_allowed(u32 hartid)
{
	return hartid == 0;
}

static int rvbucket_early_init(bool cold_boot)
{
	int rc;

	if (!cold_boot)
		return 0;

	sbi_system_reset_add_device(&rvbucket_reset);

	rc = sbi_domain_root_add_memrange(RVB_PERI_BASE,
					  RVBUCKET_PERI_DOMAIN_SIZE,
					  RVBUCKET_PERI_DOMAIN_SIZE,
					  SBI_DOMAIN_MEMREGION_MMIO |
					  SBI_DOMAIN_MEMREGION_SHARED_SURW_MRW);
	if (rc)
		return rc;

	return aclint_mswi_cold_init(&mswi);
}

static int rvbucket_irqchip_init(void)
{
	return plic_cold_irqchip_init(&plic);
}

static int rvbucket_timer_init(void)
{
	int rc;

	writel(RVBUCKET_MTIMER_TICK_CYCLES,
	       (void *)RVB_MTIME_CTRL_BASE);

	rc = sbi_domain_root_add_memrange(RVB_MTIME_CTRL_BASE,
					  RVB_MTIME_CTRL_SIZE,
					  RVB_MTIME_CTRL_SIZE,
					  SBI_DOMAIN_MEMREGION_MMIO |
					  SBI_DOMAIN_MEMREGION_M_READABLE |
					  SBI_DOMAIN_MEMREGION_M_WRITABLE);
	if (rc)
		return rc;

	return aclint_mtimer_cold_init(&mtimer, NULL);
}

const struct sbi_platform_operations platform_ops = {
	.nascent_init = rvbucket_nascent_init,
	.cold_boot_allowed = rvbucket_cold_boot_allowed,
	.early_init = rvbucket_early_init,
	.irqchip_init = rvbucket_irqchip_init,
	.timer_init = rvbucket_timer_init,
};

const struct sbi_platform platform = {
	.opensbi_version = OPENSBI_VERSION,
	.platform_version = SBI_PLATFORM_VERSION(0x0, 0x01),
	.name = "RVBucket",
	.features = SBI_PLATFORM_DEFAULT_FEATURES,
	.hart_count = RVBUCKET_HART_COUNT,
	.hart_stack_size = SBI_PLATFORM_DEFAULT_HART_STACK_SIZE,
	.heap_size = SBI_PLATFORM_DEFAULT_HEAP_SIZE(RVBUCKET_HART_COUNT),
	.platform_ops_addr = (unsigned long)&platform_ops,
};
