#include "mods/hook.hpp"
#include "mods/service.hpp"
#include "mods/svc/hook.h"
#include "mods/svc/log.h"

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
DEFINE_HOOK(&daAlink_c::setLight, SetLight);

namespace {

enum daAlink_ItemProc {
    ITEM_PROC_NONE = 0,
    ITEM_PROC_COMMON_CHANGE_ITEM = 12,
};

bool unrestricted_items_active(const daAlink_c* player) {
    return player->checkNoResetFlg0(daAlink_c::FLG0_WATER_IN_MOVE);
}

bool lantern_in_water(const daAlink_c* player) {
    return unrestricted_items_active(player) &&
           const_cast<daAlink_c*>(player)->checkWaterInKandelaarOffset(player->mWaterY);
}

HookAction on_check_water_in_kandelaar_pre(ModContext*, void* args, void*, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);
    const f32 water_y = mods::arg<f32>(args, 1);
    if (player->mEquipItem == dItemNo_KANTERA_e &&
        unrestricted_items_active(player) &&
        player->checkNoResetFlg2(daAlink_c::FLG2_UNK_1) &&
        player->checkWaterInKandelaarOffset(water_y))
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

void on_check_new_item_change_post(ModContext*, void* args, void* retval, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);
    const u8 selected_slot = mods::arg<u8>(args, 1);
    auto& result = *static_cast<int*>(retval);

    if (result != ITEM_PROC_NONE || player->mLinkAcch.ChkGroundHit()) {
        return;
    }

    const u16 selected_item = dComIfGp_getSelectItem(selected_slot);
    if (selected_item != dItemNo_KANTERA_e ||
        player->mEquipItem == selected_item ||
        !unrestricted_items_active(player) ||
        player->checkEndResetFlg1(daAlink_c::ERFLG1_UNK_4) ||
        player->checkSpinnerRide() ||
        player->checkWaterInKandelaarOffset(player->mWaterY) ||
        (player->checkCanoeRide() && daAlink_c::checkStageName("F_SP127")) ||
        daAlink_c::checkCloudSea() ||
        !daAlink_c::checkCastleTownUseItem(selected_item) ||
        player->checkBoardRide() ||
        player->checkMagneBootsOn())
    {
        return;
    }

    result = ITEM_PROC_COMMON_CHANGE_ITEM;
}

bool g_restore_light_off_flag = false;

HookAction on_set_light_pre(ModContext*, void* args, void*, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);
    g_restore_light_off_flag = false;
    if (lantern_in_water(player) &&
        !player->checkNoResetFlg2(daAlink_c::FLG2_KANDELAAR_LIGHT_OFF))
    {
        player->onNoResetFlg2(daAlink_c::FLG2_KANDELAAR_LIGHT_OFF);
        g_restore_light_off_flag = true;
    }
    return HOOK_CONTINUE;
}

void on_set_light_post(ModContext*, void* args, void*, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);
    if (g_restore_light_off_flag) {
        player->offNoResetFlg2(daAlink_c::FLG2_KANDELAAR_LIGHT_OFF);
        g_restore_light_off_flag = false;
    }
}

void replace_check_accept_use_item_in_water(ModContext*, void* args, void* retval, void*) {
    auto* player = mods::arg<const daAlink_c*>(args, 0);
    const u16 item_no = mods::arg<u16>(args, 1);
    auto& result = *static_cast<BOOL*>(retval);

    if (unrestricted_items_active(player)) {
        result = true;
        return;
    }

    result = CheckAcceptUseItemInWater::g_orig(player, item_no);
}

void replace_swim_delete_item(ModContext*, void* args, void*, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);
    const bool keep_lantern_out =
        player->mEquipItem == dItemNo_KANTERA_e && unrestricted_items_active(player);

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

void replace_init_kandelaar_swing(ModContext*, void* args, void*, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);

    if (player->mEquipItem == dItemNo_KANTERA_e) {
        if (dComIfGs_getOil() != 0) {
            player->mZ2Link.getKantera().startSound(
                Z2SE_AL_KANTERA_SWING, 0, player->mVoiceReverbIntensity);
        } else {
            player->mZ2Link.getKantera().startSound(
                Z2SE_AL_KANTERA_OFF_SWING, 0, player->mVoiceReverbIntensity);
        }
    }

    player->voiceStart(Z2SE_AL_V_SWING_BOTTLE);
    player->mAtSph.ResetAtHit();

    if (!player->checkEventRun() && !lantern_in_water(player)) {
        dComIfGp_setItemOilCount(-player->mpHIO->mItem.mLantern.m.mShakeOilLoss);
    }

    player->mAtSph.OffAtSetBit();
    player->mAtSph.SetR(50.0f);
    player->mAtSph.SetAtType(AT_TYPE_LANTERN_SWING);
    player->mAtSph.SetAtHitMark(0);
    player->mAtSph.SetAtSe(dCcD_SE_NONE);
    player->mAtSph.SetAtAtp(0);
    player->mAtSph.SetAtMtrl(dCcD_MTRL_FIRE);
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
        mods::hook_replace<InitKandelaarSwing>(svc_hook, replace_init_kandelaar_swing),
        "failed to install InitKandelaarSwing");
    if (result != MOD_OK) {
        return result;
    }

    result = install_hook(
        mods::hook_add_post<CheckNewItemChange>(
            svc_hook, on_check_new_item_change_post),
        "failed to install CheckNewItemChange");
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
