#include "mods/hook.hpp"
#include "mods/service.hpp"
#include "mods/svc/hook.h"
#include "mods/svc/log.h"

#include <vector>

#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "d/d_item_data.h"

DEFINE_MOD();

IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(HookService, svc_hook);

DEFINE_HOOK(&daAlink_c::checkAcceptUseItemInWater, CheckAcceptUseItemInWater);
DEFINE_HOOK(&daAlink_c::swimDeleteItem, SwimDeleteItem);
DEFINE_HOOK(&daAlink_c::checkWaterInKandelaar, CheckWaterInKandelaar);
DEFINE_HOOK(&daAlink_c::checkKandelaarSwing, CheckKandelaarSwing);
DEFINE_HOOK(&daAlink_c::initKandelaarSwing, InitKandelaarSwing);
DEFINE_HOOK(&daAlink_c::checkNewItemChange, CheckNewItemChange);
DEFINE_HOOK(&daAlink_c::checkNoSubjectModeCamera, CheckNoSubjectModeCamera);
DEFINE_HOOK(&daAlink_c::checkNotHeavyBootsStage, CheckNotHeavyBootsStage);
DEFINE_HOOK(&daAlink_c::setLight, SetLight);

namespace {

enum daAlink_ItemProc {
    ITEM_PROC_NONE = 0,
    ITEM_PROC_COMMON_CHANGE_ITEM = 12,
};

bool unrestricted_items_enabled() {
    return true;
}

bool unrestricted_items_water_active(const daAlink_c* player) {
    return unrestricted_items_enabled() &&
           player->checkNoResetFlg0(daAlink_c::FLG0_WATER_IN_MOVE);
}

bool water_in_kandelaar_offset(const daAlink_c* player, f32 water_y) {
    const f32 base_y_pos =
        player->checkModeFlg(0x40) ? player->mRightFootPos.y : player->current.pos.y;
    return water_y > 65.0f + base_y_pos;
}

bool lantern_in_water(const daAlink_c* player) {
    return unrestricted_items_water_active(player) &&
           water_in_kandelaar_offset(player, player->mWaterY);
}

HookAction on_check_water_in_kandelaar_pre(ModContext*, void* args, void*, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);
    const f32 water_y = mods::arg<f32>(args, 1);
    if (player->mEquipItem == dItemNo_KANTERA_e &&
        unrestricted_items_water_active(player) &&
        player->checkNoResetFlg2(daAlink_c::FLG2_UNK_1) &&
        water_in_kandelaar_offset(player, water_y))
    {
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

void on_check_kandelaar_swing_post(ModContext*, void* args, void* retval, void*) {
    auto* player = mods::arg<const daAlink_c*>(args, 0);
    const int allow_unlit = mods::arg<int>(args, 1);
    auto& result = *static_cast<bool*>(retval);
    if (allow_unlit != 0 || !result) {
        return;
    }

    if (lantern_in_water(player)) {
        result = false;
    }
}

void replace_check_new_item_change(ModContext*, void* args, void* retval, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);
    const u8 selected_slot = mods::arg<u8>(args, 1);
    const u16 selected_item = dComIfGp_getSelectItem(selected_slot);
    const bool bypass_water_checks = unrestricted_items_water_active(player);
    const bool bypass_lantern_water_check =
        unrestricted_items_enabled() &&
        (selected_item == dItemNo_KANTERA_e || daAlink_c::checkOilBottleItem(selected_item));
    const f32 saved_water_y = player->mWaterY;

    if (bypass_water_checks) {
        player->offNoResetFlg0(daAlink_c::FLG0_WATER_IN_MOVE);
    }
    if (bypass_lantern_water_check) {
        player->mWaterY = player->current.pos.y;
    }

    auto& result = *static_cast<int*>(retval);
    result = CheckNewItemChange::g_orig(player, selected_slot);

    if (bypass_water_checks) {
        player->onNoResetFlg0(daAlink_c::FLG0_WATER_IN_MOVE);
    }
    player->mWaterY = saved_water_y;
}

std::vector<daAlink_c*> g_set_light_restore_stack;
struct SavedOilCount {
    daAlink_c* player;
    s32 oil_count;
};
std::vector<SavedOilCount> g_init_kandelaar_swing_oil_stack;

HookAction on_set_light_pre(ModContext*, void* args, void*, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);
    if (lantern_in_water(player) &&
        !player->checkNoResetFlg2(daAlink_c::FLG2_KANDELAAR_LIGHT_OFF))
    {
        player->onNoResetFlg2(daAlink_c::FLG2_KANDELAAR_LIGHT_OFF);
        g_set_light_restore_stack.push_back(player);
    }
    return HOOK_CONTINUE;
}

void on_set_light_post(ModContext*, void* args, void*, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);
    if (!g_set_light_restore_stack.empty() &&
        g_set_light_restore_stack.back() == player)
    {
        player->offNoResetFlg2(daAlink_c::FLG2_KANDELAAR_LIGHT_OFF);
        g_set_light_restore_stack.pop_back();
    }
}

void replace_check_accept_use_item_in_water(ModContext*, void* args, void* retval, void*) {
    auto* player = mods::arg<const daAlink_c*>(args, 0);
    const u16 item_no = mods::arg<u16>(args, 1);
    auto& result = *static_cast<BOOL*>(retval);

    if (unrestricted_items_water_active(player)) {
        result = true;
        return;
    }

    result = CheckAcceptUseItemInWater::g_orig(player, item_no) != FALSE;
}

void replace_swim_delete_item(ModContext*, void* args, void*, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);
    const bool keep_lantern_out =
        player->mEquipItem == dItemNo_KANTERA_e && unrestricted_items_water_active(player);

    if (!player->checkHookshotItem(player->mEquipItem) &&
        !keep_lantern_out &&
        (player->mEquipItem != 0x103 || !player->checkBootsOrArmorHeavy()))
    {
        player->deleteEquipItem(TRUE, TRUE);
    }

    if (!keep_lantern_out && player->checkNoResetFlg2(daAlink_c::FLG2_UNK_1)) {
        player->offKandelaarModel();
    }
}

void replace_check_no_subject_mode_camera(ModContext*, void* args, void* retval, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);
    auto& result = *static_cast<bool*>(retval);
    if (unrestricted_items_enabled() && daAlink_c::checkStageName("F_SP116")) {
        result = player->checkCargoCarry();
        return;
    }

    result = CheckNoSubjectModeCamera::g_orig(player);
}

void replace_check_not_heavy_boots_stage(ModContext*, void*, void* retval, void*) {
    auto& result = *static_cast<bool*>(retval);
    if (unrestricted_items_enabled()) {
        result = false;
        return;
    }

    result = CheckNotHeavyBootsStage::g_orig();
}

HookAction on_init_kandelaar_swing_pre(ModContext*, void* args, void*, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);
    if (player->mEquipItem == dItemNo_KANTERA_e &&
        lantern_in_water(player) &&
        !player->checkEventRun())
    {
        g_init_kandelaar_swing_oil_stack.push_back({player, dComIfGs_getOil()});
    }
    return HOOK_CONTINUE;
}

void on_init_kandelaar_swing_post(ModContext*, void* args, void*, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);
    if (!g_init_kandelaar_swing_oil_stack.empty() &&
        g_init_kandelaar_swing_oil_stack.back().player == player)
    {
        const s32 saved_oil = g_init_kandelaar_swing_oil_stack.back().oil_count;
        g_init_kandelaar_swing_oil_stack.pop_back();
        const s32 oil_delta = saved_oil - dComIfGs_getOil();
        if (oil_delta > 0) {
            dComIfGp_setItemOilCount(oil_delta);
        }
    }
}

ModResult install_hook(ModResult result, const char* name) {
    if (result != MOD_OK) {
        svc_log->error(mod_ctx, name);
    }
    return result;
}

}  // namespace

extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError*) {
    ModResult result = MOD_OK;

    result = install_hook(
        mods::hook_replace<CheckAcceptUseItemInWater>(
            svc_hook, replace_check_accept_use_item_in_water),
        "failed to install CheckAcceptUseItemInWater");
    if (result != MOD_OK) {
        return result;
    }

    result = install_hook(
        mods::hook_replace<SwimDeleteItem>(svc_hook, replace_swim_delete_item),
        "failed to install SwimDeleteItem");
    if (result != MOD_OK) {
        return result;
    }

    result = install_hook(
        mods::hook_add_pre<CheckWaterInKandelaar>(
            svc_hook, on_check_water_in_kandelaar_pre),
        "failed to install CheckWaterInKandelaar");
    if (result != MOD_OK) {
        return result;
    }

    result = install_hook(
        mods::hook_add_post<CheckKandelaarSwing>(
            svc_hook, on_check_kandelaar_swing_post),
        "failed to install CheckKandelaarSwing");
    if (result != MOD_OK) {
        return result;
    }

    result = install_hook(
        mods::hook_add_pre<InitKandelaarSwing>(svc_hook, on_init_kandelaar_swing_pre),
        "failed to install InitKandelaarSwing pre-hook");
    if (result != MOD_OK) {
        return result;
    }

    result = install_hook(
        mods::hook_add_post<InitKandelaarSwing>(svc_hook, on_init_kandelaar_swing_post),
        "failed to install InitKandelaarSwing post-hook");
    if (result != MOD_OK) {
        return result;
    }

    result = install_hook(
        mods::hook_replace<CheckNewItemChange>(
            svc_hook, replace_check_new_item_change),
        "failed to install CheckNewItemChange");
    if (result != MOD_OK) {
        return result;
    }

    result = install_hook(
        mods::hook_replace<CheckNoSubjectModeCamera>(
            svc_hook, replace_check_no_subject_mode_camera),
        "failed to install CheckNoSubjectModeCamera");
    if (result != MOD_OK) {
        return result;
    }

    result = install_hook(
        mods::hook_replace<CheckNotHeavyBootsStage>(
            svc_hook, replace_check_not_heavy_boots_stage),
        "failed to install CheckNotHeavyBootsStage");
    if (result != MOD_OK) {
        return result;
    }

    result = install_hook(
        mods::hook_add_pre<SetLight>(svc_hook, on_set_light_pre),
        "failed to install SetLight pre-hook");
    if (result != MOD_OK) {
        return result;
    }

    result = install_hook(
        mods::hook_add_post<SetLight>(svc_hook, on_set_light_post),
        "failed to install SetLight post-hook");
    if (result != MOD_OK) {
        return result;
    }

    svc_log->info(mod_ctx, "unrestricted_items initialized");
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    return MOD_OK;
}
}
