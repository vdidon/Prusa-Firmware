//! @file
//! @date Jun 10, 2019
//! @author Marek Bel
//! @brief First layer (Z offset) calibration

#include "first_lay_cal.h"
#include "Configuration_var.h"
#include "language.h"
#include "Marlin.h"
#include "cmdqueue.h"
#include "mmu2.h"
#include <avr/pgmspace.h>
#include <math.h>

//! @brief Count extrude length
//!
//! @param layer_height layer height in mm
//! @param extrusion_width extrusion width in mm
//! @param extrusion_length extrusion length in mm
//! @return filament length in mm which needs to be extruded to form line
static constexpr float __attribute__((noinline)) count_e(float layer_height, float extrusion_width, float extrusion_length, float filament_diameter=1.75f)
{
    return (extrusion_length * ((M_PI * pow(layer_height, 2)) / 4 + layer_height * (extrusion_width - layer_height))) / ((M_PI * pow(filament_diameter, 2)) / 4);
}

//! @brief Extrusion spacing
//!
//! @param layer_height layer height in mm
//! @param extrusion_width extrusion width in mm
//! @return filament length in mm which needs to be extruded to form line
static constexpr float spacing(float layer_height, float extrusion_width, float overlap_factor=1.f)
{
    return extrusion_width - layer_height * (overlap_factor - M_PI/4);
}

// Common code extracted into one function to reduce code size
static void lay1cal_common_enqueue_loop(const char * const * cmd_sequence, const uint8_t steps) {
    for (uint8_t i = 0; i < steps; ++i)
    {
        enquecommand_P(static_cast<char*>(pgm_read_ptr(cmd_sequence + i)));
    }
}

static const char extrude_fmt[] PROGMEM = "G1 X%d Y%d E%-.5f";
static const char zero_extrusion[] PROGMEM = "G92 E0";

//! @brief Wait for preheat
void lay1cal_wait_preheat()
{
    static const char preheat_cmd_2[] PROGMEM = "M190";
    static const char preheat_cmd_3[] PROGMEM = "M109";
    static const char preheat_cmd_4[] PROGMEM = "G28";

    static const char * const preheat_cmd[] PROGMEM =
    {
        MSG_M107,
        preheat_cmd_2,
        preheat_cmd_3,
        preheat_cmd_4,
        zero_extrusion
    };

    lay1cal_common_enqueue_loop(preheat_cmd, sizeof(preheat_cmd)/sizeof(preheat_cmd[0]));
}

//! @brief Load filament
//! @param cmd_buffer character buffer needed to format gcodes
//! @param filament filament to use (applies for MMU only)
//! @returns true if extra purge distance is needed in case of MMU prints (after a toolchange), otherwise false
bool lay1cal_load_filament(uint8_t filament)
{
    if (MMU2::mmu2.Enabled())
    {
        enquecommand_P(MSG_M83);
        enquecommand_P(PSTR("G1 Y-3 F1000"));
        enquecommand_P(PSTR("G1 Z0.4 F1000"));

        uint8_t currentTool = MMU2::mmu2.get_current_tool();
        if(currentTool == filament ){
            // already have the correct tool loaded - do nothing
            return false;
        } else if( currentTool != (uint8_t)MMU2::FILAMENT_UNKNOWN){
            // some other slot is loaded, perform an unload first
            enquecommand_P(MSG_M702);
        }
        // perform a toolchange
        enquecommandf_P(PSTR("T%d"), filament);
        return true;
    }
    return false;
}

//! @brief Print intro line
//! @param extraPurgeNeeded false if the first MMU-related "G1 E29" have to be skipped because the nozzle is already full of filament
//! @param layer_height the height of the calibration layer
//! @param extrusion_width the width of the extrusion layer 
void lay1cal_intro_line(bool extraPurgeNeeded, float layer_height, float extrusion_width)
{
    static const char cmd_intro_mmu_3[] PROGMEM = "G1 X55 E29 F1073";
    static const char cmd_intro_mmu_4[] PROGMEM = "G1 X5 E29 F1800";
    static const char cmd_intro_mmu_5[] PROGMEM = "G1 X55 E8 F2000";
    static const char cmd_intro_mmu_6[] PROGMEM = "G1 Z0.3 F1000";
    static const char cmd_intro_mmu_8[] PROGMEM = "G1 X240 E25  F2200";
    static const char cmd_intro_mmu_9[] PROGMEM = "G1 Y-2 F1000";
    static const char cmd_intro_mmu_10[] PROGMEM = "G1 X55 E25 F1400";
    static const char cmd_intro_mmu_11[] PROGMEM = "G1 Z0.2 F1000";
    static const char cmd_intro_mmu_12[] PROGMEM = "G1 X5 E4 F1000";

    static const char * const intro_mmu_cmd[] PROGMEM =
    {
        // first 2 items are only relevant if filament was not loaded - i.e. extraPurgeNeeded == true
        cmd_intro_mmu_3,
        cmd_intro_mmu_4,

        cmd_intro_mmu_5,
        cmd_intro_mmu_6,
        zero_extrusion,
        cmd_intro_mmu_8,
        cmd_intro_mmu_9,
        cmd_intro_mmu_10,
        cmd_intro_mmu_11,
        cmd_intro_mmu_12,
    };

    if (MMU2::mmu2.Enabled())
    {
        for (uint8_t i = (extraPurgeNeeded ? 0 : 2); i < (sizeof(intro_mmu_cmd)/sizeof(intro_mmu_cmd[0])); ++i)
        {
            enquecommand_P(static_cast<char*>(pgm_read_ptr(&intro_mmu_cmd[i])));
        }
    }
    else
    {
        static const char fmt1[] PROGMEM = "G1 X%d E%-.3f F1000";
        enquecommandf_P(fmt1, 60, count_e(layer_height, extrusion_width * 4.f, 60));
        enquecommandf_P(fmt1, 100, count_e(layer_height, extrusion_width * 8.f, 40));
    }
}

//! @brief Setup for printing meander
void lay1cal_before_meander()
{
    static const char cmd_pre_meander_2[] PROGMEM = "G90"; //use absolute coordinates
    static const char cmd_pre_meander_4[] PROGMEM = "G1 E-1.5 F2100";
    static const char cmd_pre_meander_5[] PROGMEM = "G1 Z5 F7200";
    static const char cmd_pre_meander_6[] PROGMEM = "M204 S1000"; //set acceleration
    static const char cmd_pre_meander_7[] PROGMEM = "G1 F4000";

    static const char * const cmd_pre_meander[] PROGMEM =
    {
            zero_extrusion,
            cmd_pre_meander_2,
            MSG_M83, // use relative distances for extrusion
            cmd_pre_meander_4,
            cmd_pre_meander_5,
            cmd_pre_meander_6,
            cmd_pre_meander_7,
    };

    lay1cal_common_enqueue_loop(cmd_pre_meander, (sizeof(cmd_pre_meander)/sizeof(cmd_pre_meander[0])));
}

//! @brief Print meander start
void lay1cal_meander_start(float layer_height, float extrusion_width)
{
    enquecommand_P(PSTR("G1 X50 Y155"));

    static const char fmt1[] PROGMEM = "G1 Z%-.3f F7200";
    enquecommandf_P(fmt1, layer_height);

    enquecommand_P(PSTR("G1 F1080"));

    enquecommandf_P(extrude_fmt,  75, 155, count_e(layer_height, extrusion_width * 4.f, 25));
    enquecommandf_P(extrude_fmt, 100, 155, count_e(layer_height, extrusion_width * 2.f, 25));
    enquecommandf_P(extrude_fmt, 200, 155, count_e(layer_height, extrusion_width, 100));
    enquecommandf_P(extrude_fmt, 200, 135, count_e(layer_height, extrusion_width, 20));
}

//! @brief Print meander
//! @param cmd_buffer character buffer needed to format gcodes
void lay1cal_meander(float layer_height, float extrusion_width)
{
    const float short_length = 20;
    float long_length = 150;
    const float long_extrusion = count_e(layer_height, extrusion_width, long_length);
    const float short_extrusion = count_e(layer_height, extrusion_width, short_length);

    uint8_t y_pos = 135;
    uint8_t x_pos = 50;
    for(uint8_t i = 0; i <= 4; ++i)
    {
        enquecommandf_P(extrude_fmt, x_pos, y_pos, long_extrusion);

        y_pos -= short_length;

        enquecommandf_P(extrude_fmt, x_pos, y_pos, short_extrusion);

        x_pos += long_length;

        long_length = -long_length;
    }

}

//! @brief Print square
//!
//! This function needs to be called 4 times with step of 0,4,8,12
//!
//! @param cmd_buffer character buffer needed to format gcodes
//! @param i iteration
void lay1cal_square(uint8_t step, float layer_height, float extrusion_width)
{
    const float long_length = 20;
    const float short_length = spacing(layer_height, extrusion_width);
    const float long_extrusion = count_e(layer_height, extrusion_width, long_length);
    const float short_extrusion = count_e(layer_height, extrusion_width, short_length);
    static const char fmt1[] PROGMEM = "G1 X%d Y%-.2f E%-.3f";

    for (uint8_t i = step; i < step+4; ++i)
    {
        enquecommandf_P(fmt1, 70, (35 - i*short_length * 2), long_extrusion);
        enquecommandf_P(fmt1, 70, (35 - (2 * i + 1)*short_length), short_extrusion);
        enquecommandf_P(fmt1, 50, (35 - (2 * i + 1)*short_length), long_extrusion);
        enquecommandf_P(fmt1, 50, (35 - (i + 1)*short_length * 2), short_extrusion);
    }
}

void lay1cal_finish(bool mmu_enabled)
{
    static const char cmd_cal_finish_1[] PROGMEM = "G1 E-0.075 F2100"; //retract
    static const char cmd_cal_finish_2[] PROGMEM = "M104 S0"; // turn off temperature
    static const char cmd_cal_finish_3[] PROGMEM = "M140 S0"; // turn off heatbed
    static const char cmd_cal_finish_4[] PROGMEM = "G1 Z10 F1300"; //lift Z
    static const char cmd_cal_finish_5[] PROGMEM = "G1 X10 Y180 F4000"; //Go to parking position

    static const char * const cmd_cal_finish[] PROGMEM =
    {
            MSG_M107, // turn off printer fan
            cmd_cal_finish_1,
            cmd_cal_finish_2,
            cmd_cal_finish_3,
            cmd_cal_finish_4,
            cmd_cal_finish_5
    };

    lay1cal_common_enqueue_loop(cmd_cal_finish, (sizeof(cmd_cal_finish)/sizeof(cmd_cal_finish[0])));

    if (mmu_enabled) enquecommand_P(MSG_M702); //unload from nozzle
    enquecommand_P(MSG_M84);// disable motors
}
