/**
 * @file      utilities.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  Shenzhen Xin Yuan Electronic Technology Co., Ltd
 * @date      2025-04-30
 *
 */

#pragma once

#if defined(LILYGO_T_A7670)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_DTR_PIN                       (25)
    #define MODEM_TX_PIN                        (26)
    #define MODEM_RX_PIN                        (27)
    // The modem boot pin needs to follow the startup sequence.
    #define BOARD_PWRKEY_PIN                    (4)
    // The modem power switch must be set to HIGH for the modem to supply power.
    #define BOARD_POWERON_PIN                   (12)
    #define MODEM_RING_PIN                      (33)
    #define MODEM_RESET_PIN                     (5)
    #define BOARD_MISO_PIN                      (2)
    #define BOARD_MOSI_PIN                      (15)
    #define BOARD_SCK_PIN                       (14)
    #define BOARD_SD_CS_PIN                     (13)
    #define BOARD_BAT_ADC_PIN                   (35)
    #define MODEM_RESET_LEVEL                   HIGH
    #define SerialAT                            Serial1

    #define MODEM_GPS_ENABLE_GPIO               (-1)
    #define MODEM_GPS_ENABLE_LEVEL              (-1)

    #ifndef TINY_GSM_MODEM_A7670
        #define TINY_GSM_MODEM_A7670
    #endif

    // It is only available in V1.4 version. In other versions, IO36 is not connected.
    #define BOARD_SOLAR_ADC_PIN                 (36)


    //! The following pins are for SimShield and need to be used with SimShield
    //! 以下引脚针对SimShield,需要搭配SimShield 
    #define SIMSHIELD_MOSI                      (23)
    #define SIMSHIELD_MISO                      (19)
    #define SIMSHIELD_SCK                       (18)
    #define SIMSHIELD_SD_CS                     (32)
    #define SIMSHIELD_RADIO_BUSY                (39)
    #define SIMSHIELD_RADIO_CS                  (5)
    #define SIMSHIELD_RADIO_IRQ                 (34)
    #define SIMSHIELD_RADIO_RST                 (15)
    #define SIMSHIELD_RS_RX                     (13)
    #define SIMSHIELD_RS_TX                     (14)
    #define SIMSHIELD_SDA                       (21)
    #define SIMSHIELD_SCL                       (22)
    #define SerialRS485                         Serial2

    #define PRODUCT_MODEL_NAME                  "LilyGo-A7670 ESP32 Version"

#endif

#if defined(TINY_GSM_MODEM_SIM7670G) || defined(TINY_GSM_MODEM_A7670) || defined(TINY_GSM_MODEM_A7608)
    #define MODEM_REG_SMS_ONLY
#endif

// Invalid version
// #define LILYGO_T_A7608X_DC_S3

// Power on/off sequence
#if defined(TINY_GSM_MODEM_A7670)
    #define MODEM_POWERON_PULSE_WIDTH_MS      (100)
    #define MODEM_POWEROFF_PULSE_WIDTH_MS     (3000)
#endif

#ifndef MODEM_START_WAIT_MS
    #define MODEM_START_WAIT_MS             3000
#endif
