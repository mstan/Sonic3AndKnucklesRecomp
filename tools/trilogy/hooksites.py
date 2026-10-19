"""Resolve Trilogy hook sites to retail-cartridge PCs.

Each hook is named by (routine label, instruction regex, 'at'|'after').  The
address comes from the verified listing of the RETAIL cartridge halves (the
listings rebuild byte-identical; they are an address oracle, nothing is built
from them).  Emits:
  game/trilogy/trilogy_hooks.h   #define TH_<NAME> 0xPC
  game/sonic3k/trilogy_hooks.toml-snippet appended into game.toml between markers
"""
import re
import sys
from pathlib import Path

CRLF, LF = chr(13) + chr(10), chr(10)
ROOT = Path(__file__).resolve().parents[2]
DIS = ROOT / 'build' / 'disassembly' / 'src'
LINE = re.compile(r"^\s*(?:\(\d+\))?\s*\d+/\s*([0-9A-F]+) : ((?:[0-9A-F]{4} )*[0-9A-F]{0,4})\s*(.*)$")


def load(lst, base):
    rows = []          # (addr, src, is_insn)
    labels = {}
    for raw in open(lst, encoding='utf-8', errors='replace'):
        m = LINE.match(raw.rstrip('\n'))
        if not m:
            continue
        addr = int(m[1], 16) + base
        code = m[2].strip()
        src = m[3]
        lab = re.match(r"^([A-Za-z_][\w]*):", src)
        if lab:
            labels.setdefault(lab[1], len(rows))
            src = src[lab.end():]
        src = src.split(';')[0].strip()
        src = re.sub(r"\s+", " ", src)
        rows.append((addr, src, bool(code) and bool(src)))
    return rows, labels


SK = None
S3 = None


def find(label, pattern, where='after', nth=0, half='sk'):
    global SK, S3
    if SK is None:
        SK = load(DIS / 'sonic3k.lst', 0)
        S3 = load(DIS / 's3.lst', 0x200000)
    rows, labels = SK if half == 'sk' else S3
    if label not in labels:
        raise KeyError(label)
    i = labels[label]
    rx = re.compile(pattern)
    hits = 0
    for j in range(i, min(i + 4000, len(rows))):
        a, src, ins = rows[j]
        if ins and rx.search(src):
            if hits == nth:
                if where == 'at':
                    return a
                for k in range(j + 1, len(rows)):
                    if rows[k][2]:
                        return rows[k][0]
            hits += 1
    raise LookupError(f"{label}: /{pattern}/ #{nth} not found")


# name, label, regex, where, nth, half
HOOKS = [
    # --- level load -------------------------------------------------------
    ("LEVEL_ENTRY",     "Level",                 r"^bset #7,\(Game_mode\)\.w", 'at', 0, 'sk'),
    ("LVL_PLC1",        "Level",                 r"^move\.b \(a2,d1\.w\),d0", 'at', 0, 'sk'),
    ("LVL_MUSIC",       "loc_61FC",              r"^move\.b \(a1,d0\.w\),d0", 'at', 0, 'sk'),
    ("LLB1",            "LoadLevelLoadBlock",    r"^lea \(a4,d0\.w\),a4", 'after', 0, 'sk'),
    ("LLB2",            "LoadLevelLoadBlock2",   r"^lea \(a2,d0\.w\),a2", 'after', 0, 'sk'),
    ("LOAD_LEVEL",      "Load_Level",            r"^movea\.l \(a0,d0\.w\),a0", 'after', 0, 'sk'),
    ("LOAD_SOLIDS",     "LoadSolids",            r"^adda\.l d0,a1", 'after', 0, 'sk'),
    ("LEVEL_SIZES",     "Get_LevelSizeStart",    r"^lea LevelSizes\(pc,d0\.w\),a0", 'after', 0, 'sk'),
    ("START_LOC",       "loc_1BE7A",             r"^lea \(a1,d0\.w\),a1", 'after', 0, 'sk'),
    ("RESIZE",          "Do_ResizeEvents",       r"^moveq #0,d0", 'at', 0, 'sk'),
    ("SETUP_FG",        "LevelSetup",            r"^jsr \(a1\)", 'at', 0, 'sk'),
    ("SETUP_BG",        "LevelSetup",            r"^jsr \(a1\)", 'at', 1, 'sk'),
    ("EVENT_FG",        "ScreenEvents",          r"^jsr \(a1\)", 'at', 0, 'sk'),
    ("EVENT_BG",        "ScreenEvents",          r"^jsr \(a1\)", 'at', 1, 'sk'),
    ("PALPOINT",        "LoadPalette",           r"^adda\.w d0,a1", 'after', 0, 'sk'),
    ("PALPOINT_I",      "LoadPalette_Immediate", r"^adda\.w d0,a1", 'after', 0, 'sk'),
    ("PALPOINT2",       "LoadPalette2",          r"^adda\.w d0,a1", 'after', 0, 'sk'),
    ("PALPOINT2_I",     "LoadPalette2_Immediate", r"^adda\.w d0,a1", 'after', 0, 'sk'),
    ("LOAD_PLC_ID",     "Load_PLC",              r"^add\.w d0,d0", 'at', 0, 'sk'),
    ("LOAD_PLC_PTR",    "Load_PLC",              r"^lea \(a1,d0\.w\),a1", 'after', 0, 'sk'),
    ("ANIM_TILES",      "Animate_Tiles",         r"^move\.w \(Current_zone_and_act\)\.w,d0", 'at', 0, 'sk'),
    ("ANPAL_LOAD",      "AnPal_Load",            r"^move\.w \(Current_zone_and_act\)\.w,d0", 'at', 0, 'sk'),
    ("TC_LEVELGFX",     "loc_2D746",             r"^movea\.l \(a1,d0\.w\),a1", 'after', 0, 'sk'),
    ("TC_BONUS",        "Obj_TitleCardCreate",   r"^bhs\.s loc_2D7AC", 'at', 0, 'sk'),
    ("TC_NAME",         "Obj_TitleCardName",     r"^add\.b d0,mapping_frame\(a0\)", 'after', 0, 'sk'),
    ("LVL_ARTLOAD",     "loc_6310",              r"^jsr \(LoadLevelLoadBlock2\)\.l", 'at', 0, 'sk'),
    ("RESULTS_DONE",    "loc_2DCE2",             r"^move\.b \(Current_zone\)\.w,d0", 'at', 0, 'sk'),
    ("ANIMAL_TYPE",     "loc_2C940",             r"^lea byte_2C7BA\(pc\),a1", 'after', 0, 'sk'),
    ("ANIMAL_PLC",      "PLCLoad_AnimalsAndExplosion", r"^lea PLCLoad_Animals_Index\(pc,d0\.w\),a1", 'after', 0, 'sk'),
    ("ENEMY_ART",       "loc_2F7A2",             r"^adda\.w \(a6,d0\.w\),a6", 'after', 0, 'sk'),
    # loc_2D8DC: every title-card exit path ends here (zones >= $13 skip loc_2D8CA)
    ("TC_DONE",         "loc_2D8DC",             r"^jmp \(Delete_Current_Sprite\)\.l", 'at', 0, 'sk'),
    ("TC_ACTNUM",       "loc_2D716",             r"^move\.w #tiles_to_bytes\(\$53D\),d2", 'at', 0, 'sk'),
    ("TC_ACT",          "Obj_TitleCardAct",      r"^move\.l #Obj_TitleCardElement,\(a0\)", 'at', 0, 'sk'),
    ("RESULTS_ACTNUM",  "loc_2DB1C",             r"^move\.w d0,subtype\(a0\)", 'at', 0, 'sk'),
    # oscillators: each game keeps its own baselines/frequencies
    ("OSC_INIT",        "OscillateNumInit",      r"^lea \(Osc_Data\)\.l,a2", 'after', 0, 'sk'),
    ("OSC_DO",          "OscillateNumDo",        r"^lea \(Osc_Data2\)\.l,a2", 'after', 0, 'sk'),
    # giant ring: enter the special stage only after the act-clear tally
    ("SS_GO",           "SSEntryFlash_GoSS",     r"^moveq #signextendB\(sfx_EnterSS\),d0", 'at', 0, 'sk'),
    # starpost: S1/S2 had no star-circle bonus stages
    ("STARPOST_STARS",  "sub_2D028",             r"^blo\.s loc_2D0D0", 'at', 0, 'sk'),
    # water (S1 Labyrinth / SBZ3): set-up, dynamic height, oscillating surface
    ("CHK_WATER",       "CheckLevelForWater",    r"^cmpi\.w #0,\(Current_zone_and_act\)\.w", 'at', 0, 'sk'),
    ("WATER_PAL",       "LoadWaterPalette",      r"^tst\.b \(Water_flag\)\.w", 'at', 0, 'sk'),
    ("DYN_WATER",       "DynamicWaterHeight",    r"^moveq #0,d0", 'at', 0, 'sk'),
    ("WATER_LEVEL",     "Handle_Onscreen_Water_Height", r"^move\.w d0,\(Water_level\)\.w", 'at', 0, 'sk'),
    ("OBJ_INDEX",       "loc_1B6CA",             r"^move\.l d0,\(Object_index_addr\)\.w", 'at', 0, 'sk'),
    ("OBJ_LAYOUT",      "loc_1B6E8",             r"^movea\.l \(a0,d0\.w\),a0", 'after', 0, 'sk'),
    ("RING_LAYOUT",     "loc_EB52",              r"^movea\.l \(a1,d0\.w\),a1", 'after', 0, 'sk'),
    # pause menu, achievements, abilities and unlocks (trilogy_menu.c)
    ("PAUSE_CHK",       "Pause_ChkStart",        r"^move\.b \(Ctrl_1_pressed\)\.w,d0", 'at', 0, 'sk'),
    ("GAMELOOP",        "GameLoop",              r"^jsr \(a0\)", 'at', 0, 'sk'),      # native scenes (Sonic 2's ending)
    ("LVL_PRE_SPRITES", "LevelLoop",             r"^jsr \(Process_Sprites\)\.l", 'at', 0, 'sk'),
    ("LVL_POST_SPRITES", "LevelLoop",            r"^tst\.w \(Restart_level_flag\)\.w", 'at', 0, 'sk'),
    ("RING_ATTRACT",    "Test_Ring_Collisions",  r"^beq\.s Test_Ring_Collisions_NoAttraction", 'at', 0, 'sk'),
    ("RING_ATTRACT2",   "loc_EABE",              r"^bne\.s Test_Ring_Collisions_AttractRing", 'at', 0, 'sk'),
    ("RING_ATTRACT3",   "Obj_Attracted_Ring",    r"^bne\.s Obj_Attracted_RingAnimate", 'at', 0, 'sk'),
    ("KILL_ENEMY",      "Touch_EnemyNormal",     r"^btst #2,status\(a1\)", 'at', 0, 'sk'),
    ("BOSS_HIT",        "Touch_Enemy",           r"^neg\.w x_vel\(a0\)", 'at', 0, 'sk'),   # S1/S2 bosses: no $1C/$25 writes
    ("MONITOR_BREAK",   "Obj_MonitorBreak",      r"^move\.b status\(a0\),d0", 'at', 0, 'sk'),
    ("SUPER_CHK",       "Sonic_CheckTransform",  r"^blo\.s Sonic_InstaShield", 'at', 0, 'sk'),
    ("SUPER_CHK_K",     "Knux_Test_For_Glide",   r"^blo\.s loc_1786C", 'at', 0, 'sk'),      # (the Chaos Emerald count)
    ("SUPER_CHK_T",     "Tails_Test_For_Flight", r"^blo\.s loc_1515C", 'at', 0, 'sk'),      # (the Super Emerald count)
    ("MON_SOLID",       "SolidObject_Monitor_SonicKnux", r"^beq\.s locret_1D6BC", 'at', 0, 'sk'),   # (rolling: no solidity)
    # --- palette fades: the extended sprite lines fade step for step with CRAM
    ("PAL_FROMBLACK",   "Pal_FromBlack",         r"^moveq #0,d0", 'at', 0, 'sk'),
    ("PAL_TOBLACK",     "Pal_ToBlack",           r"^moveq #0,d0", 'at', 0, 'sk'),
    ("PAL_FROMWHITE",   "Pal_FromWhite",         r"^moveq #0,d0", 'at', 0, 'sk'),
    ("PAL_TOWHITE",     "Pal_ToWhite",           r"^moveq #0,d0", 'at', 0, 'sk'),
    ("PAL_FILLBLACK",   "Pal_FillBlack",         r"^moveq #0,d0", 'at', 0, 'sk'),
    ("PAL_FILLWHITE",   "Pal_FillWhite",         r"^moveq #0,d0", 'at', 0, 'sk'),
    # --- Data Select: trilogy level codes, live level thumbnails (trilogy_save.c, trilogy_thumbs.c)
    ("SAVEGAME",        "SaveGame",              r"^tst\.w \(SK_alone_flag\)\.w", 'at', 0, 'sk'),
    ("DS_NOSAVE_START", "Obj_SaveScreen_NoSave_Slot", r"^jsr \(Set_Lives_and_Continues\)\.l", 'at', 0, 'sk'),
    ("DS_NEWGAME",      "loc_D5FE",              r"^jsr \(Set_Lives_and_Continues\)\.l", 'at', 0, 'sk'),
    ("DS_ZONE_MAX",     "loc_D4EE",              r"^moveq #0,d2", 'at', 0, 'sk'),
    ("DS_CODE_TO_ZONE", "sub_DA4E",              r"^cmpi\.w #3,\$34\(a0\)", 'at', 0, 'sk'),
    ("DS_ZONE_TENS",    "loc_C912",              r"^move\.b DataSelect_Zone_Nums\(pc,d0\.w\),d1", 'after', 0, 'sk'),
    ("DS_ZONE_UNITS",   "loc_C912",              r"^move\.b DataSelect_Zone_Nums\+1\(pc,d0\.w\),d1", 'after', 0, 'sk'),
    ("STARS_TOUCH",     "loc_2D47E",             r"^move\.b #2,\(Special_bonus_entry_flag\)\.w", 'at', 0, 'sk'),   # S2 starpost stars -> special stage
    ("RENDER_HUD",      "Render_HUD",            r"^cmpi\.b #\$16,\(Current_zone\)\.w", 'at', 0, 'sk'),   # (hidden while recording zone cards)
    ("DS_ZONE_ICON",    "loc_D9A2",              r"^move\.w #tiles_to_bytes", 'at', 0, 'sk'),
]


def find_all(pattern):
    """Every instruction matching pattern in both halves -> list of 'after' PCs."""
    global SK, S3
    if SK is None:
        SK = load(DIS / 'sonic3k.lst', 0)
        S3 = load(DIS / 's3.lst', 0x200000)
    rx = re.compile(pattern)
    res = []
    for rows, _ in (SK, S3):
        for j, (a, src, ins) in enumerate(rows):
            if ins and rx.search(src):
                for k in range(j + 1, len(rows)):
                    if rows[k][2]:
                        res.append(rows[k][0])
                        break
    return sorted(set(res))


GLOBAL_HOOKS = [
    ("COL_ANGLE", r"^lea \(AngleArray\)\.l,a2$"),
    ("COL_HEIGHT", r"^lea \(HeightMaps\)\.l,a2$"),
    ("COL_HEIGHTROT", r"^lea \(HeightMapsRot\)\.l,a2$"),
]


def main():
    out = []
    for name, label, rx, where, nth, half in HOOKS:
        a = find(label, rx, where, nth, half)
        out.append((name, a, label))
    groups = []
    for name, rx in GLOBAL_HOOKS:
        pcs = find_all(rx)
        groups.append((name, pcs))
        for i, a in enumerate(pcs):
            out.append((f"{name}_{i}", a, "global"))
    hdr = ["/* Generated by tools/trilogy/hooksites.py from the retail-cartridge",
           " * listings. Do not edit; edit HOOKS in the tool. */",
           "#pragma once"]
    for name, a, label in out:
        hdr.append(f"#define TH_{name:<16} 0x{a:06X}u  /* {label} */")
    for name, pcs in groups:
        hdr.append(f"#define TH_{name}_CASES " + " ".join(f"case 0x{a:06X}u:" for a in pcs))
    hdr.append("/* every site above: the trilogy owns these PCs */")
    hdr.append("#define TH_ALL_CASES " + " ".join(f"case 0x{a:06X}u:" for a in sorted({a for _, a, _ in out})))
    (ROOT / 'game' / 'trilogy' / 'trilogy_hooks.h').write_text('\n'.join(hdr) + '\n', newline='\n')
    toml = ["# >>> trilogy hooks (generated by tools/trilogy/hooksites.py)"]
    for name, a, label in out:
        toml += ["[[widescreen_site]]", f"addr = 0x{a:06X} # trilogy {name} ({label})", 'kind = "game_hook"']
    toml.append("# <<< trilogy hooks")
    gt = ROOT / 'game' / 'sonic3k' / 'game.toml'
    s = gt.read_bytes().decode('utf-8').replace(CRLF, LF)
    s = re.sub(r"\n# >>> trilogy hooks.*?# <<< trilogy hooks\n?", "\n", s, flags=re.S)
    s = s.rstrip('\n') + '\n\n' + '\n'.join(toml) + '\n'
    gt.write_bytes(s.replace(LF, CRLF).encode('utf-8'))
    for name, a, label in out:
        print(f"{name:<16} ${a:06X}  {label}")


if __name__ == '__main__':
    main()


# S3K symbols the C layer references (routines from sk.syms labels, RAM from sk.equ)
SYMS = [
    "Reset_TileOffsetPositionActual", "Reset_TileOffsetPositionEff", "Refresh_PlaneFull",
    "DrawTilesAsYouMove", "Draw_TileColumn", "Draw_TileRow", "PlainDeformation",
    "ApplyDeformation", "Load_PLC", "LoadPalette", "Queue_Kos_Module", "Add_To_DMA_Queue",
    "Draw_Sprite", "Delete_Current_Sprite", "MoveSprite", "MoveSprite2", "ObjectFall",
    "AllocateObject", "AllocateObjectAfterCurrent", "Animate_Sprite", "Sprite_OnScreen_Test",
    "Play_SFX", "Play_Music", "Obj_TitleCard", "Obj_Ring", "Obj_Monitor", "Obj_Spring",
    "Obj_Spikes", "Obj_StarPost", "Obj_PathSwap", "Obj_EndSign", "Obj_EggCapsule",
    "Obj_SSEntryRing", "SolidObjectFull", "SolidObjectTop", "Touch_ChkHurt", "HurtCharacter",
    "Obj_Explosion", "Find_SonicTails", "Obj_Wait", "Load_Sprites", "Load_Rings",
    "ObjCheckFloorDist", "Obj_TensionBridge", "Obj_Bubbler", "SolidObjectFull_1P", "SolidObjectFull2_1P", "GiveRing", "Map_Ring", "Add_SpriteToCollisionResponseList", "Random_Number",
    "GetSineCosine", "GetArcTan", "Sprite_CheckDeleteTouch", "Obj_Animal", "Obj_LevelResults",
    "Sprite_CheckDelete", "Delete_Sprite_If_Not_In_Range", "CreateNewSprite4", "Kill_Character",
    "SolidObjectTopSloped", "Sprite_OnScreen_Test2", "HUD_AddToScore",
    "Map_Ring", "Set_PlayerEndingPose", "Check_TailsEndPose", "Restore_PlayerControl2", "Obj_FireShield", "Obj_LightningShield", "Obj_BubbleShield", "Change_Music_Tempo", "RideObject_SetRide",
    "PLCLoad_AnimalsAndExplosion", "Load_PLC_Raw", "PLC_SpikesSprings",
    "ObjCheckCeilingDist", "ObjCheckRightWallDist", "ObjCheckLeftWallDist", "Obj_EnemyScore",
    "Clear_SpriteRingMem", "Go_Delete_SpriteSlotted2",
    "Obj_InvisibleBlock", "Obj_InvisibleLavaBlock", "Touch_Enemy",
    "HInt3", "HInt4", "LoadPalette2", "LoadPalette2_Immediate",
    "LevelLoadBlock", "LevelPtrs", "PalPoint", "Sonic_Start_Locations", "Knux_Start_Locations", "LevelSizes",
    "Pal_SonicTails", "Pal_Knuckles", "Write_SaveGame", "Save_Level_Data2",
    "Wait_VSync", "Map_Sonic", "Map_SuperSonic", "PLC_Sonic", "PLC_SuperSonic", "ArtUnc_Sonic",
    "ArtUnc_Sonic_Extra", "AniSonic", "AniSuperSonic", "Map_Tails", "PLC_Tails", "ArtUnc_Tails",
    "ArtUnc_Tails_Extra", "AniTails",
    "Kos_Decomp", "Eni_Decomp", "Clear_DisplayData", "Init_SpriteTable", "RAM_Map_Data_To_VDP",
    "Process_Sprites", "Render_Sprites", "SKTitle_DeformBG", "Obj_SKTitle_DeathEgg", "Obj_SKTitle_Mountain",
    "ArtKos_SKTitleScreenBG", "ArtKos_SKTitle_DeathEgg", "ArtKos_SKTitle_Mountain", "MapEni_SKTitle_Background",
    "Pal_SKTitle_Sonic", "Pal_FromWhite", "Pal_ToBlack", "Process_Kos_Queue",
    "ArtKos_SaveScreenMisc", "ArtKos_SaveScreen", "Map_SaveScreen", "Pal_Save_Chars", "Pal_SaveMenuBG",
]
RAM = [
    "Current_zone_and_act", "Current_zone", "Current_act", "Apparent_zone_and_act", "Apparent_zone",
    "Player_mode", "Game_mode", "Camera_X_pos", "Camera_Y_pos", "Camera_X_pos_copy", "Camera_Y_pos_copy",
    "Camera_X_pos_BG_copy", "Camera_Y_pos_BG_copy", "Camera_X_pos_BG_rounded", "Camera_Y_pos_BG_rounded",
    "Camera_X_pos_rounded", "Camera_Y_pos_rounded", "Camera_min_X_pos", "Camera_max_X_pos",
    "Camera_min_Y_pos", "Camera_max_Y_pos", "Camera_target_max_Y_pos", "Camera_target_min_Y_pos",
    "Camera_target_max_X_pos", "Camera_target_min_X_pos", "Camera_Y_pos_mask", "Layout_row_index_mask",
    "Screen_X_wrap_value", "Screen_Y_wrap_value", "H_scroll_buffer", "HScroll_table", "Plane_buffer",
    "Block_table", "Level_layout_header", "Level_layout_main", "Normal_palette", "Target_palette", "Water_palette",
    "Player_1", "Player_2", "Object_RAM", "Ring_count", "Chaos_emerald_count", "Super_emerald_count", "Tails_CPU_routine", "RNG_seed",
    "Collected_emeralds_array", "Current_special_stage", "Events_bg", "Events_fg_0", "Events_fg_1",
    "Level_frame_counter", "Oscillating_table", "V_scroll_value", "V_scroll_value_BG",
    "Camera_max_Y_pos_changing", "Dynamic_resize_routine", "Last_star_post_hit", "Water_flag",
    "Debug_placement_mode", "Kos_modules_left", "Collision_addr", "Primary_collision_addr",
    "Secondary_collision_addr", "Super_Sonic_Knux_flag", "Timer", "Life_count", "Boss_flag",
    "Level_started_flag", "Ctrl_1", "Ctrl_1_held", "Ctrl_1_pressed", "Saved_zone_and_act",
    "Apparent_act", "Current_music", "Palette_cycle_counter0", "Palette_cycle_counter1",
    "Anim_Counters", "Object_load_routine", "Rings_manager_routine",
    "Update_HUD_timer", "Restart_level_flag", "End_of_level_flag", "Signpost_addr", "Ctrl_1_locked",
    "Scroll_lock", "V_blank_cycles", "H_int_jump", "H_int_addr", "Water_palette_data_addr",
    "Water_entered_counter", "Water_full_screen_flag", "Water_speed", "Competition_mode",
    "Saved_water_full_screen_flag", "Ctrl_1_held_logical", "Ctrl_2_held_logical", "Water_level", "Mean_water_level", "Target_water_level", "Water_flag",
    "_unkFABE", "Ctrl_2_locked", "Update_HUD_ring_count", "Level_trigger_array",
    "Chain_bonus_counter", "Special_bonus_entry_flag", "SK_special_stage_flag",
    "Respawn_table_keep", "Collected_special_ring_array", "Disable_death_plane", "_unkFAA8", "Shield", "Dust", "Max_speed", "Acceleration", "Deceleration", "Update_HUD_life_count", "Life_count", "Game_paused", "Super_emerald_count", "Tails_CPU_routine", "RNG_seed",
    "V_int_routine", "Sprite_table", "Ctrl_1_pressed_logical", "Lag_frame_count",
    "Events_routine_fg", "Screen_shake_flag", "Camera_X_pos_P2", "Camera_Y_pos_P2", "Palette_fade_info",
    "Palette_fade_timer", "Reserved_object_3", "Demo_timer", "Sprite_table_input", "Kos_decomp_buffer",
    "Camera_RAM", "Pal_fade_delay2", "Normal_palette_line_2",
]


def emit_syms():
    d = ROOT / 'build' / 'disassembly'
    labs = {}
    for f in ('sk.syms', 's3.syms'):
        for l in open(d / f):
            a, n = l.split()
            labs.setdefault(n, int(a, 16))
    equ = {}
    for l in open(d / 'sk.equ'):
        n, v = l.split()
        equ[n] = int(v, 16)
    # `Name = *` aliases take the address of the next real RAM symbol
    pending = []
    for l in open(DIS / 'sonic3k.constants.asm', encoding='utf-8', errors='replace'):
        m = re.match(r"^([A-Za-z_]\w*)\s*=\s*\*", l)
        if m:
            pending.append(m[1])
            continue
        m = re.match(r"^([A-Za-z_]\w*)\s+ds\.", l)
        if m and pending:
            if m[1] in equ:
                for n in pending:
                    equ.setdefault(n, equ[m[1]])
            pending = []
    out = ["/* Generated by tools/trilogy/hooksites.py (retail S3K addresses). */", "#pragma once"]
    for n in SYMS:
        if n in labs:
            out.append(f"#define S3K_{n:<34} 0x{labs[n]:06X}u")
        else:
            out.append(f"/* missing: {n} */")
    for n in RAM:
        if n in equ:
            out.append(f"#define RAM_{n:<34} 0x{equ[n] & 0xFFFFFF:06X}u")
        else:
            out.append(f"/* missing RAM: {n} */")
    (ROOT / 'game' / 'trilogy' / 'trilogy_syms.h').write_text('\n'.join(out) + '\n', newline='\n')


emit_syms()
