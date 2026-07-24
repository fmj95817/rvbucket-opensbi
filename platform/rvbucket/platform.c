// SPDX-License-Identifier: BSD-2-Clause

#include <sbi/riscv_io.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_const.h>
#include <sbi/sbi_domain.h>
#include <sbi/sbi_platform.h>
#include <sbi/sbi_system.h>
#include <sbi_utils/ipi/aclint_mswi.h>
#include <sbi_utils/irqchip/plic.h>
#include <sbi_utils/timer/aclint_mtimer.h>

#define RVBUCKET_HART_COUNT          2

#define RVBUCKET_PERI_BASE           0x30000000UL
#define RVBUCKET_PERI_SIZE           0x01000000UL
#define RVBUCKET_UART_BASE           0x30000000UL
#define RVBUCKET_UART_REG_TX         0x04UL
#define RVBUCKET_UART_REG_RX         0x08UL
#define RVBUCKET_UART_REG_STS        0x0cUL
#define RVBUCKET_UART_STS_RX_VALID   (1U << 0)
#define RVBUCKET_SIM_END_CHAR        0x10

#define RVBUCKET_ACLINT_MTIME        0x31000000UL
#define RVBUCKET_ACLINT_MTIMECMP     0x31010000UL
#define RVBUCKET_ACLINT_MSWI         0x31020000UL
#define RVBUCKET_ACLINT_MTIME_FREQ   10000UL

#define RVBUCKET_PLIC_BASE           0x31100000UL
#define RVBUCKET_PLIC_SIZE           (0x200000UL + \
					  RVBUCKET_HART_COUNT * 0x1000UL)
#define RVBUCKET_PLIC_NUM_SOURCES    32

static void rvbucket_uart_putc(char ch)
{
	writel((u32)ch, (void *)(RVBUCKET_UART_BASE + RVBUCKET_UART_REG_TX));
}

static int rvbucket_uart_getc(void)
{
	if (!(readl((void *)(RVBUCKET_UART_BASE + RVBUCKET_UART_REG_STS)) &
	      RVBUCKET_UART_STS_RX_VALID))
		return -1;

	return readl((void *)(RVBUCKET_UART_BASE + RVBUCKET_UART_REG_RX)) & 0xff;
}

static struct sbi_console_device rvbucket_console = {
	.name = "rvbucket-uart",
	.console_putc = rvbucket_uart_putc,
	.console_getc = rvbucket_uart_getc,
};

static struct plic_data plic = {
	.unique_id = 0,
	.addr = RVBUCKET_PLIC_BASE,
	.size = RVBUCKET_PLIC_SIZE,
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
	.addr = RVBUCKET_ACLINT_MSWI,
	.size = ACLINT_MSWI_SIZE,
	.first_hartid = 0,
	.hart_count = RVBUCKET_HART_COUNT,
};

static struct aclint_mtimer_data mtimer = {
	.mtime_freq = RVBUCKET_ACLINT_MTIME_FREQ,
	.mtime_addr = RVBUCKET_ACLINT_MTIME,
	.mtime_size = 0x8,
	.mtimecmp_addr = RVBUCKET_ACLINT_MTIMECMP,
	.mtimecmp_size = 0x8 * RVBUCKET_HART_COUNT,
	.first_hartid = 0,
	.hart_count = RVBUCKET_HART_COUNT,
	.has_64bit_mmio = false,
};

static int rvbucket_system_reset_check(u32 type, u32 reason)
{
	return 1;
}

static void rvbucket_system_reset(u32 type, u32 reason)
{
	rvbucket_uart_putc(RVBUCKET_SIM_END_CHAR);
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

	rc = sbi_domain_root_add_memrange(RVBUCKET_PERI_BASE,
					  RVBUCKET_PERI_SIZE,
					  RVBUCKET_PERI_SIZE,
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
