# SPDX-License-Identifier: BSD-2-Clause

platform-cppflags-y =
platform-cflags-y =
platform-asflags-y =
platform-ldflags-y =

PLATFORM_RISCV_XLEN = 32
PLATFORM_RISCV_ABI = ilp32
PLATFORM_RISCV_ISA = rv32ima_zicsr_zifencei
PLATFORM_RISCV_CODE_MODEL = medany

platform-objs-y += platform.o

FW_PAYLOAD = y
FW_PAYLOAD_ALIGN = 0x1000
FW_PAYLOAD_OFFSET = 0x50000
FW_TEXT_START = 0x40000000

firmware-genflags-y += -DFW_PAYLOAD_OFFSET=$(FW_PAYLOAD_OFFSET)
