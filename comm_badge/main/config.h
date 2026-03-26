/*
 * config.h — Central pin and hardware configuration for CommBadge.
 *
 * All GPIO assignments live here. Change them once; everything else adapts.
 * Board: Olimex ESP32-S3-DevKit-Lipo (ESP32-S3, 8 MB flash, 8 MB PSRAM).
 */
#pragma once

/* ---- Button ------------------------------------------------------------ */
/* Active-low tactile button; internal pull-up is enabled in button_service. */
#define CONFIG_BUTTON_GPIO      1

/* ---- I2S — MAX98357 amplifier ----------------------------------------- */
/* The amp's DIN pin connects to the ESP32-S3 DOUT line.                    */
#define CONFIG_I2S_BCLK_GPIO    15   /* Bit clock          */
#define CONFIG_I2S_LRCLK_GPIO   16   /* Left/right (word select) clock */
#define CONFIG_I2S_DOUT_GPIO    17   /* Data out → DIN on amp */
