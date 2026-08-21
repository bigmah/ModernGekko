// SPDX-License-Identifier: GPL-3.0-or-later
//
// Runs a recompiled game that is data rather than code. The C and LLVM backends
// end at host machine code and the chassis jumps into it; this one ends at a
// bytecode module the chassis interprets, so nothing the recompiler produced is
// ever mapped executable. Same DolIR, same analysis, same module ABI -- the
// only difference the chassis can see is that dispatch() takes longer.
//
// This file is the only part of the chassis compiled against GXRuntime's
// CPUState header, which is why the interface it exposes mentions none of it.

#include "dolvm_bridge.h"

#include "core/cpu.h"
#include "vm/dolvm.h"
#include "vm/dolvm_interp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DolVMModule s_module;
static bool s_open;
static DolVMBridgeRange* s_regions;
static char s_game_id[8];

int dolvm_bridge_open(const char* path, DolVMBridgeInfo* info, char* error,
                      size_t error_size)
{
    dolvm_bridge_close();
    if (!dolvm_module_load_file(&s_module, path, error, error_size))
        return 0;

    s_regions = (DolVMBridgeRange*)malloc(
        (size_t)(s_module.region_count ? s_module.region_count : 1u) *
        sizeof(*s_regions));
    if (!s_regions)
    {
        dolvm_module_close(&s_module);
        if (error && error_size)
            snprintf(error, error_size, "dolvm: out of memory listing regions");
        return 0;
    }
    for (u32 i = 0; i < s_module.region_count; i++)
    {
        s_regions[i].start = s_module.regions[i].guest_start;
        s_regions[i].end = s_module.regions[i].guest_end;
    }
    memcpy(s_game_id, s_module.header->game_id, sizeof(s_game_id));
    s_game_id[sizeof(s_game_id) - 1u] = '\0';
    s_open = true;

    memset(info, 0, sizeof(*info));
    info->game_id = s_game_id;
    info->entry_point = s_module.header->entry_point;
    info->state_layout_hash = s_module.header->state_layout_hash;
    info->regions = s_regions;
    info->region_hashes = s_module.region_hashes;
    info->region_count = s_module.region_count;
    // DolVMRange and DolVMBridgeRange are both two u32s in the same order; the
    // cast keeps the container's table in place instead of copying it.
    info->smc_ranges = (const DolVMBridgeRange*)s_module.smc_ranges;
    info->smc_count = s_module.smc_count;
    info->direct_calls = (s_module.flags & DOLVM_FLAG_DIRECT_CALLS) ? 1 : 0;
    return 1;
}

void dolvm_bridge_close(void)
{
#ifdef DOLVM_PROFILE
    if (s_open)
        dolvm_profile_report(stderr);
#endif
    if (s_open)
        dolvm_module_close(&s_module);
    free(s_regions);
    s_regions = NULL;
    s_open = false;
}

int dolvm_bridge_dispatch(struct CPUState* ctx, uint32_t address)
{
    if (!s_open)
        return 0;
    return dolvm_dispatch(&s_module, ctx, address);
}

void dolvm_bridge_on_state_loaded(struct CPUState* ctx)
{
    // Re-arm host FP rounding and flush-to-zero from the guest FPSCR the
    // chassis just loaded, exactly as a native module's glue does.
    ppc_fpscr_updated(ctx);
}
