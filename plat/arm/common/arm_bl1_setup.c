/*
 * Copyright (c) 2015-2018, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <arch.h>
#include <arm_def.h>
#include <arm_xlat_tables.h>
#include <assert.h>
#include <bl1.h>
#include <bl_common.h>
#include <plat_arm.h>
#include <platform.h>
#include <platform_def.h>
#include <sp805.h>
#include <utils.h>
#include "../../../bl1/bl1_private.h"

/* Weak definitions may be overridden in specific ARM standard platform */
#pragma weak bl1_early_platform_setup
#pragma weak bl1_plat_arch_setup
#pragma weak bl1_platform_setup
#pragma weak bl1_plat_sec_mem_layout
#pragma weak bl1_plat_prepare_exit

#define MAP_BL1_TOTAL		MAP_REGION_FLAT(			\
					bl1_tzram_layout.total_base,	\
					bl1_tzram_layout.total_size,	\
					MT_MEMORY | MT_RW | MT_SECURE)
/*
 * If SEPARATE_CODE_AND_RODATA=1 we define a region for each section
 * otherwise one region is defined containing both
 */
#if SEPARATE_CODE_AND_RODATA
#define MAP_BL1_RO		MAP_REGION_FLAT(			\
					BL_CODE_BASE,			\
					BL1_CODE_END - BL_CODE_BASE,	\
					MT_CODE | MT_SECURE),		\
				MAP_REGION_FLAT(			\
					BL1_RO_DATA_BASE,		\
					BL1_RO_DATA_END			\
						- BL_RO_DATA_BASE,	\
					MT_RO_DATA | MT_SECURE)
#else
#define MAP_BL1_RO		MAP_REGION_FLAT(			\
					BL_CODE_BASE,			\
					BL1_CODE_END - BL_CODE_BASE,	\
					MT_CODE | MT_SECURE)
#endif

/* Data structure which holds the extents of the trusted SRAM for BL1*/
static meminfo_t bl1_tzram_layout;

struct meminfo *bl1_plat_sec_mem_layout(void)
{
	return &bl1_tzram_layout;
}

/*******************************************************************************
 * BL1 specific platform actions shared between ARM standard platforms.
 ******************************************************************************/
void arm_bl1_early_platform_setup(void)
{

#if !ARM_DISABLE_TRUSTED_WDOG
	/* Enable watchdog */
	sp805_start(ARM_SP805_TWDG_BASE, ARM_TWDG_LOAD_VAL);
#endif

	/* Initialize the console to provide early debug support */
	arm_console_boot_init();

	/* Allow BL1 to see the whole Trusted RAM */
	bl1_tzram_layout.total_base = ARM_BL_RAM_BASE;
	bl1_tzram_layout.total_size = ARM_BL_RAM_SIZE;

#if !LOAD_IMAGE_V2
	/* Calculate how much RAM BL1 is using and how much remains free */
	bl1_tzram_layout.free_base = ARM_BL_RAM_BASE;
	bl1_tzram_layout.free_size = ARM_BL_RAM_SIZE;
	reserve_mem(&bl1_tzram_layout.free_base,
		    &bl1_tzram_layout.free_size,
		    BL1_RAM_BASE,
		    BL1_RAM_LIMIT - BL1_RAM_BASE);
#endif /* LOAD_IMAGE_V2 */
}

void bl1_early_platform_setup(void)
{
	arm_bl1_early_platform_setup();

	/*
	 * Initialize Interconnect for this cluster during cold boot.
	 * No need for locks as no other CPU is active.
	 */
	plat_arm_interconnect_init();
	/*
	 * Enable Interconnect coherency for the primary CPU's cluster.
	 */
	plat_arm_interconnect_enter_coherency();
}

/******************************************************************************
 * Perform the very early platform specific architecture setup shared between
 * ARM standard platforms. This only does basic initialization. Later
 * architectural setup (bl1_arch_setup()) does not do anything platform
 * specific.
 *****************************************************************************/
void arm_bl1_plat_arch_setup(void)
{
#if USE_COHERENT_MEM
	/* ARM platforms dont use coherent memory in BL1 */
	assert((BL_COHERENT_RAM_END - BL_COHERENT_RAM_BASE) == 0U);
#endif

	const mmap_region_t bl_regions[] = {
		MAP_BL1_TOTAL,
		MAP_BL1_RO,
#if USE_ROMLIB
		ARM_MAP_ROMLIB_CODE,
		ARM_MAP_ROMLIB_DATA,
 #endif
		{0}
	};

	arm_setup_page_tables(bl_regions, plat_arm_get_mmap());
#ifdef AARCH32
	enable_mmu_svc_mon(0);
#else
	enable_mmu_el3(0);
#endif /* AARCH32 */

	arm_setup_romlib();
}

void bl1_plat_arch_setup(void)
{
	arm_bl1_plat_arch_setup();
}

/*
 * Perform the platform specific architecture setup shared between
 * ARM standard platforms.
 */
void arm_bl1_platform_setup(void)
{
	/* Initialise the IO layer and register platform IO devices */
	plat_arm_io_setup();
#if LOAD_IMAGE_V2
	arm_load_tb_fw_config();
#endif
	/*
	 * Allow access to the System counter timer module and program
	 * counter frequency for non secure images during FWU
	 */
	arm_configure_sys_timer();
	write_cntfrq_el0(plat_get_syscnt_freq2());
}

void bl1_platform_setup(void)
{
	arm_bl1_platform_setup();
}

void bl1_plat_prepare_exit(entry_point_info_t *ep_info)
{
#if !ARM_DISABLE_TRUSTED_WDOG
	/* Disable watchdog before leaving BL1 */
	sp805_stop(ARM_SP805_TWDG_BASE);
#endif

#ifdef EL3_PAYLOAD_BASE
	/*
	 * Program the EL3 payload's entry point address into the CPUs mailbox
	 * in order to release secondary CPUs from their holding pen and make
	 * them jump there.
	 */
	plat_arm_program_trusted_mailbox(ep_info->pc);
	dsbsy();
	sev();
#endif
}

/*******************************************************************************
 * The following function checks if Firmware update is needed,
 * by checking if TOC in FIP image is valid or not.
 ******************************************************************************/
unsigned int bl1_plat_get_next_image_id(void)
{
	if (!arm_io_is_toc_valid())
		return NS_BL1U_IMAGE_ID;

	return BL2_IMAGE_ID;
}
