/*
 This project is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 Deviation is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with Deviation.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "extended_audio.h"

static struct musicconfig_page * const mp = &pagemem.u.musicconfig_page;
static struct musicconfig_obj * const gui = &gui_objs.u.musicconfig;

static u16 current_selected = 0;

enum {
    MUSIC_SRC_SWITCH = 1,
    MUSIC_SRC_BUTTON,
#if NUM_AUX_KNOBS
    MUSIC_SRC_AUX,
#endif
    MUSIC_SRC_TIMER,
    MUSIC_SRC_TELEMETRY,
    MUSIC_SRC_MIXER,
};

static u8 musicconfig_getsrctype (u8 idx)
{
    if (idx < NUM_SWITCHES)
        return MUSIC_SRC_SWITCH;
/*    if (idx < NUM_INPUTS - INP_HAS_CALIBRATION + NUM_TX_BUTTONS)
        return MUSIC_SRC_BUTTON;*/
#if NUM_AUX_KNOBS
    if (idx < NUM_SWITCHES + NUM_AUX_KNOBS * 2)
        return MUSIC_SRC_AUX;
#endif
    if (idx < NUM_SWITCHES + NUM_AUX_KNOBS * 2 + NUM_TIMERS)
        return MUSIC_SRC_TIMER;
    if (idx < NUM_SWITCHES + NUM_AUX_KNOBS * 2 + NUM_TIMERS + TELEM_NUM_ALARMS)
        return MUSIC_SRC_TELEMETRY;
    return MUSIC_SRC_MIXER;
}

const char *musicconfig_str_cb(guiObject_t *obj, const void *data)
{
    (void)obj;
    int idx = (long)data;
    if (musicconfig_getsrctype(idx) == MUSIC_SRC_SWITCH)
        INPUT_SourceName(tempstring,idx + INP_HAS_CALIBRATION + 1);
#if NUM_AUX_KNOBS
    if (musicconfig_getsrctype(idx) == MUSIC_SRC_AUX) {
        if ((idx - NUM_SWITCHES) % 2) {
            INPUT_SourceName(tempstring,(idx - NUM_SWITCHES -1) / 2 + NUM_STICKS + 1);
            strcat(tempstring, " ");
            strcat(tempstring, _tr("up"));
        }
        else {
            INPUT_SourceName(tempstring,(idx - NUM_SWITCHES) / 2 + NUM_STICKS + 1);
            strcat(tempstring," ");
            strcat(tempstring, _tr("down"));
        }
    }
#endif
    if (musicconfig_getsrctype(idx) == MUSIC_SRC_TIMER)
        snprintf(tempstring, sizeof(tempstring), _tr("Timer %d"),
            idx - (MODEL_CUSTOM_ALARMS - NUM_TIMERS - TELEM_NUM_ALARMS - NUM_OUT_CHANNELS - NUM_VIRT_CHANNELS) + 1);
    if (musicconfig_getsrctype(idx) == MUSIC_SRC_TELEMETRY)
        snprintf(tempstring, sizeof(tempstring), _tr("Telem %d"),
            idx - (MODEL_CUSTOM_ALARMS - TELEM_NUM_ALARMS - NUM_OUT_CHANNELS - NUM_VIRT_CHANNELS) + 1);
    if (musicconfig_getsrctype(idx) == MUSIC_SRC_MIXER)
        INPUT_SourceNameReal(tempstring,
                (idx - (MODEL_CUSTOM_ALARMS - NUM_OUT_CHANNELS - NUM_VIRT_CHANNELS) + NUM_INPUTS + 1));

    return tempstring;
}

static void music_test_cb(guiObject_t *obj, void *data)
{
    (void) obj;
    u8 idx = (long)data;
    if (musicconfig_getsrctype(idx) == MUSIC_SRC_SWITCH) {
        if (Model.music.switches[idx].music)
            MUSIC_Play(Model.music.switches[idx].music);
    }
#if NUM_AUX_KNOBS
    if (musicconfig_getsrctype(idx) == MUSIC_SRC_AUX)
        if (Model.music.aux[idx - NUM_SWITCHES].music)
            MUSIC_Play(Model.music.aux[idx - NUM_SWITCHES].music);
#endif
    if (musicconfig_getsrctype(idx) == MUSIC_SRC_TIMER) {
        if (Model.music.timer[idx - (MODEL_CUSTOM_ALARMS - NUM_TIMERS - TELEM_NUM_ALARMS - NUM_OUT_CHANNELS - NUM_VIRT_CHANNELS)].music)
            MUSIC_Play(Model.music.timer[idx - (MODEL_CUSTOM_ALARMS - NUM_TIMERS - TELEM_NUM_ALARMS - NUM_OUT_CHANNELS - NUM_VIRT_CHANNELS)].music);
    }
    if (musicconfig_getsrctype(idx) == MUSIC_SRC_TELEMETRY) {
        if (Model.music.telemetry[idx - (MODEL_CUSTOM_ALARMS - TELEM_NUM_ALARMS - NUM_OUT_CHANNELS - NUM_VIRT_CHANNELS)].music)
            MUSIC_Play(Model.music.telemetry[idx - (MODEL_CUSTOM_ALARMS - TELEM_NUM_ALARMS - NUM_OUT_CHANNELS - NUM_VIRT_CHANNELS)].music);
    }
    if (musicconfig_getsrctype(idx) == MUSIC_SRC_MIXER) {
        if (Model.music.mixer[idx - (MODEL_CUSTOM_ALARMS - NUM_OUT_CHANNELS - NUM_VIRT_CHANNELS)].music)
            MUSIC_Play(Model.music.mixer[idx - (MODEL_CUSTOM_ALARMS - NUM_OUT_CHANNELS - NUM_VIRT_CHANNELS)].music);
    }
}

static const char *musiclbl_cb(guiObject_t *obj, const void *data)
{
    (void) obj;
    int idx = (long)data;
    if (musicconfig_getsrctype(idx) == MUSIC_SRC_SWITCH) {
        if (Model.music.switches[idx].music)
            return music_map[Model.music.switches[idx].music].label;
    }
#if NUM_AUX_KNOBS
    if (musicconfig_getsrctype(idx) == MUSIC_SRC_AUX) {
        if(Model.music.aux[idx - NUM_SWITCHES].music)
            return music_map[Model.music.aux[idx - NUM_SWITCHES].music].label;
    }
#endif
    /*if (musicconfig_getsrctype(idx) == MUSIC_SRC_BUTTON) {
        if (Model.music.buttons[idx - (NUM_INPUTS - INP_HAS_CALIBRATION)].music)
            return music_map[Model.music.buttons[idx - (NUM_INPUTS - INP_HAS_CALIBRATION)].music].label;
    }*/
    if (musicconfig_getsrctype(idx) == MUSIC_SRC_TIMER) {
        if (Model.music.timer[idx - (MODEL_CUSTOM_ALARMS - NUM_TIMERS - TELEM_NUM_ALARMS - NUM_OUT_CHANNELS - NUM_VIRT_CHANNELS)].music)
            return music_map[Model.music.timer[idx - (MODEL_CUSTOM_ALARMS - NUM_TIMERS - TELEM_NUM_ALARMS - NUM_OUT_CHANNELS - NUM_VIRT_CHANNELS)].music].label;
    }
    if (musicconfig_getsrctype(idx) == MUSIC_SRC_TELEMETRY) {
        if (Model.music.telemetry[idx - (MODEL_CUSTOM_ALARMS - TELEM_NUM_ALARMS - NUM_OUT_CHANNELS - NUM_VIRT_CHANNELS)].music)
            return music_map[Model.music.telemetry[idx - (MODEL_CUSTOM_ALARMS - TELEM_NUM_ALARMS - NUM_OUT_CHANNELS - NUM_VIRT_CHANNELS)].music].label;
    }
    if (musicconfig_getsrctype(idx) == MUSIC_SRC_MIXER) {
        if (Model.music.mixer[idx - (MODEL_CUSTOM_ALARMS - NUM_OUT_CHANNELS - NUM_VIRT_CHANNELS)].music)
            return music_map[Model.music.mixer[idx - (MODEL_CUSTOM_ALARMS - NUM_OUT_CHANNELS - NUM_VIRT_CHANNELS)].music].label;
    }
    return strcpy(tempstring, "");
}

static const char *musicid_cb(guiObject_t *obj, int dir, void *data)
{
    (void) obj;
    int idx = (long)data;
    int cur_row = idx - GUI_ScrollableCurrentRow(&gui->scrollable);
    struct CustomMusic *musicpt;

    if (musicconfig_getsrctype(idx) == MUSIC_SRC_SWITCH)
        musicpt = &Model.music.switches[idx];
#if NUM_AUX_KNOBS
    if (musicconfig_getsrctype(idx) == MUSIC_SRC_AUX)
        musicpt = &Model.music.aux[idx - NUM_SWITCHES];
#endif
    if (musicconfig_getsrctype(idx) == MUSIC_SRC_TIMER)
        musicpt = &Model.music.timer[idx - (MODEL_CUSTOM_ALARMS - NUM_TIMERS - TELEM_NUM_ALARMS - NUM_OUT_CHANNELS - NUM_VIRT_CHANNELS)];

    if (musicconfig_getsrctype(idx) == MUSIC_SRC_TELEMETRY)
        musicpt = &Model.music.telemetry[idx - (MODEL_CUSTOM_ALARMS - TELEM_NUM_ALARMS - NUM_OUT_CHANNELS - NUM_VIRT_CHANNELS)];

    if (musicconfig_getsrctype(idx) == MUSIC_SRC_MIXER)
        musicpt = &Model.music.mixer[idx - (MODEL_CUSTOM_ALARMS - NUM_OUT_CHANNELS - NUM_VIRT_CHANNELS)];

    if (dir == -1 && musicpt->music == CUSTOM_ALARM_ID) // set to none below 1
        musicpt->music = 0;
    if (dir == 1 && musicpt->music == 0) // set to CUSTOM_ALARM_ID when currently none
        musicpt->music = CUSTOM_ALARM_ID - 1;
    GUI_TextSelectEnablePress((guiTextSelect_t *)obj, musicpt->music);

    if (musicpt->music == 0) {
        GUI_Redraw(&gui->musiclbl[cur_row]);
        return strcpy(tempstring, _tr("None"));
    }
    musicpt->music = GUI_TextSelectHelper(musicpt->music - CUSTOM_ALARM_ID + 1, //Relabling so music in menu starts with 1
        1, music_map_entries - CUSTOM_ALARM_ID, dir, 1, 10, NULL) + CUSTOM_ALARM_ID - 1;
    snprintf(tempstring, 5, "%d", musicpt->music - CUSTOM_ALARM_ID + 1);
    GUI_Redraw(&gui->musiclbl[cur_row]);
    return tempstring;
}
