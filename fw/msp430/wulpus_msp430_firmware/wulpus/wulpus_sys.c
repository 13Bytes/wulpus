/*
 * Copyright (C) 2023 ETH Zurich. All rights reserved.
 *
 * Author: Sergei Vostrikov, ETH Zurich
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * 
 * SPDX-License-Identifier: Apache-2.0
 */

#include "wulpus_sys.h"


void getDefaultUsConfig(msp_config_t * msp_config)
{

    msp_config->pllOutFreq = HSPLL_OUT_80_MHZ;
    msp_config->xtalFreq = HSPLL_XTAL_FREQ_8_MHZ;
    msp_config->xtalType = HSPLL_XTAL_CERAMIC_RESONATOR;
    // No buffered xtal clock output
    msp_config->outEnPllXtal = false;


    msp_config->biasImp = BIAS_IMP_2950_OHM;
    msp_config->chargePumpMode = CHARGE_PUMP_NORMAL_MODE;

    msp_config->uupsBiasDelay = UUPS_BIAS_NO_DELAY;

    // Six time marks events (default values)
    msp_config->startPpgCnt = 2500;
    msp_config->turnOnAdcCnt = 25;
    msp_config->startPgaInBiasCnt = 25;
    msp_config->startAdcSamplCnt = 2514;
    msp_config->restartCaptCnt = 937;
    msp_config->captTimeoutCnt = 3750;

    // Extra time events (SW-managed)
    msp_config->startHvMuxRxCnt = 4000;
    msp_config->dcDcTurnOnTime = 1000;


    // Acquisition settings
    msp_config->overSamplRate = SDHS_OVER_SAMPL_RATE_10;
    msp_config->sampleSize = 400;
    msp_config->rxGain = PGA_GAIN_9_0_DB;
    msp_config->measPeriod = 32768;

    // TX/RX configurations
    msp_config->txRxConfLen = 0;
    // msp_config->txConfigs[TX_RX_CONF_LEN_MAX];
    // msp_config->rxConfigs[TX_RX_CONF_LEN_MAX];

    // Pulser settins
    msp_config->driveStrength = PPG_NORMAL_DRIVE;

    msp_config->transFreq = 2250000; // Reserved, not used
    msp_config->pulseFreq = 2250000;
    msp_config->pulsesDutyCycle = 50; // 50 %
    msp_config->numPulses = 2;
    msp_config->numStopPulses = 0;
    msp_config->pulserPolarity = PPG_POLARITY_START_WITH_HIGH;
    msp_config->pulserPauseState = PPG_PAUSE_STATE_LOW;
}

// Extract Uss config from spi RX buffer
// Return 1 if config is valid
bool extractUsConfig(uint8_t * spi_rx, msp_config_t * msp_config)
{
    // Check start byte
    if(!isNewConfigCondition(spi_rx)){
        return 0;
    }

    // Note: The MSP430 cannot access 2-byte words at odd addresses, so the CPU just ignores the lowest bit of word addresses.
    //Therefore, we do here some magic

    msp_config->dcDcTurnOnTime = READ_uint16(spi_rx + 1);
    msp_config->measPeriod     = READ_uint16(spi_rx + 3);
    msp_config->transFreq      = READ_uint32(spi_rx + 5); // Reserved, not used
    msp_config->pulseFreq      = READ_uint32(spi_rx + 9);
    msp_config->numPulses      = READ_uint8(spi_rx + 13);
    msp_config->overSamplRate  = (sdhs_over_sampl_rate_t)READ_uint16(spi_rx + 14);
    msp_config->sampleSize     = READ_uint16(spi_rx + 16);
    msp_config->rxGain         = READ_uint8(spi_rx + 18);
    msp_config->txRxConfLen    = READ_uint8(spi_rx + 19);

    if (msp_config->txRxConfLen > TX_RX_CONF_LEN_MAX)
        return 0;

    // Copy the TX RX configs
    uint8_t i;
    for (i = 0; i < (msp_config->txRxConfLen); i++)
    {
        msp_config->txConfigs[i] = READ_uint16(spi_rx + 20 + 4*i);
        msp_config->rxConfigs[i] = READ_uint16(spi_rx + 22 + 4*i);
    }

    uint8_t offset = 20 + 4*(msp_config->txRxConfLen);

    // Copy the data from the Advanced settings section
    msp_config->startHvMuxRxCnt   = READ_uint16(spi_rx + offset);
    msp_config->startPpgCnt       = READ_uint16(spi_rx + offset + 2);
    msp_config->turnOnAdcCnt      = READ_uint16(spi_rx + offset + 4);
    msp_config->startPgaInBiasCnt = READ_uint16(spi_rx + offset + 6);
    msp_config->startAdcSamplCnt  = READ_uint16(spi_rx + offset + 8);
    msp_config->restartCaptCnt    = READ_uint16(spi_rx + offset + 10);
    msp_config->captTimeoutCnt    = READ_uint16(spi_rx + offset + 12);

    return 1;
}

// Check the first byte if restart should be performed
bool isRestartCondition(uint8_t * spi_rx)
{
    return (spi_rx[0] == START_BYTE_RESTART);
}

// Check the first byte if a new config is requested
bool isNewConfigCondition(uint8_t * spi_rx){
    return (spi_rx[0] == START_BYTE_CONF_PACK);
}

// Initiate MSP430-controlled power switches
void initAllPowerSwitches(void)
{   
    #if defined(WULPUS_BUTTON_V2_1)
    GPIO_setAsOutputPin(GPIO_PORT_RX_ENABLE, GPIO_PIN_RX_ENABLE);   // Set output "RxEn" (powers up input amplifier)
    GPIO_setAsOutputPin(GPIO_PORT_HV_DC_ENABLE, GPIO_PIN_HV_DC_ENABLE); // Set output "HV_EN" (HV DC/DC: LT1945)
    #else
    // Set output "RxPwr" (powers up input amplifier using the load switch U6)
    GPIO_setAsOutputPin(GPIO_PORT_RX_POWER, GPIO_PIN_RX_POWER);

    // Set output "EN HV" (power up HV PCB)
    // Power enable pin for DC-DC converters on HV PCB (powers TPS61222 and LT1945)
    GPIO_setAsOutputPin(GPIO_PORT_HV_SUPPLY, GPIO_PIN_HV_SUPPLY);

    // Set output "SW_EN" (Switch DC/DC: TPS61222)
    // Set output "HV_EN" (HV DC/DC: LT1945)
    GPIO_setAsOutputPin(GPIO_PORT_RX_ENABLE, GPIO_PIN_RX_ENABLE);
    GPIO_setAsOutputPin(GPIO_PORT_SWITCH_ENABLE, GPIO_PIN_SWITCH_ENABLE);
    GPIO_setAsOutputPin(GPIO_PORT_HV_DC_ENABLE, GPIO_PIN_HV_DC_ENABLE);
    #endif
}

// Init other GPIOs
void initOtherGpios(void)
{
    // Set BLE ready pin as input
    GPIO_setAsInputPin(GPIO_PORT_BLE_READY, GPIO_PIN_BLE_READY);
    #if defined(HAS_LED)
    // Configure LED
    GPIO_setAsOutputPin(GPIO_PORT_LED_MSP430, GPIO_PIN_LED_MSP430);
    GPIO_setOutputLowOnPin(GPIO_PORT_LED_MSP430, GPIO_PIN_LED_MSP430);
    #endif
}

bool isBleReady(void)
{
    return GPIO_getInputPinValue(GPIO_PORT_BLE_READY, GPIO_PIN_BLE_READY);
}

// Enable Rx Operational Amplifier
void enableOpAmp(void)
{
    // Enable RX OPA836
    // Set "RxEn" to high
    GPIO_setOutputHighOnPin(GPIO_PORT_RX_ENABLE, GPIO_PIN_RX_ENABLE);
}

// Disable Rx Operational Amplifier
void disableOpAmp(void)
{
    // Disable RX OPA836
    // Set "RxEn" to low
    GPIO_setOutputLowOnPin(GPIO_PORT_RX_ENABLE, GPIO_PIN_RX_ENABLE);
}

// Disable HV DC-DC converter
void disableHvDcDc(void)
{
    // Set "HV_EN" to low (disables the HV DC/DC)
    GPIO_setOutputLowOnPin(GPIO_PORT_HV_DC_ENABLE, GPIO_PIN_HV_DC_ENABLE);
}

// Enable HV DC-DC converter
void enableHvDcDc(void)
{
    // Set "HV_EN" to high (enables the HV DC/DC)
    GPIO_setOutputHighOnPin(GPIO_PORT_HV_DC_ENABLE, GPIO_PIN_HV_DC_ENABLE);
}

#ifndef WULPUS_BUTTON_V2_1
// Enable Rx Operational Amplifier power supply
void enableOpAmpSupply(void)
{
    // Enable Power for OPA836
    // Set "RxPwr" to high
    GPIO_setOutputHighOnPin(GPIO_PORT_RX_POWER, GPIO_PIN_RX_POWER);
}

// Disable Rx Operational Amplifier power supply
void disableOpAmpSupply(void)
{
    // Disable Power for OPA836
    // Set "RxPwr" to low
    GPIO_setOutputLowOnPin(GPIO_PORT_RX_POWER, GPIO_PIN_RX_POWER);
}

// Enable HV PCB power supply
void enableHvPcbSupply(void)
{
    // Power up HV PCB
    // Powering up this domain takes quite long, therefore it's powered on always and not
    // just for the US measurements
    // Set "EN HV" to high (power up HV PCB)
    GPIO_setOutputHighOnPin(GPIO_PORT_HV_SUPPLY, GPIO_PIN_HV_SUPPLY);
}

// Disable HV PCB power supply
void disableHvPcbSupply(void)
{
    // Power down HV PCB
    // Set "EN HV" to low (power down HV PCB)
    GPIO_setOutputLowOnPin(GPIO_PORT_HV_SUPPLY, GPIO_PIN_HV_SUPPLY);
}

// Enable DC-DC converters on HV PCB
void enableHvPcbDcDc(void)
{
    // Enable HV and +5 V
    // Set "SW_EN" to high (enables the DC/DC TPS61222)
    // Set "HV1_EN" to high (enables the HV DC/DC LT1945)
    GPIO_setOutputHighOnPin(GPIO_PORT_SWITCH_ENABLE,
                            GPIO_PIN_SWITCH_ENABLE | GPIO_PIN_HV_DC_ENABLE);
}

// Disable DC-DC converters on HV PCB
void disableHvPcbDcDc(void)
{
    // Disable HV and +5 V
    // Set "SW_EN" to low (disables the DC/DC TPS61222)
    // Set "HV1_EN" to low (disables the HV DC/DC LT1945)
    GPIO_setOutputLowOnPin(GPIO_PORT_SWITCH_ENABLE,
                           GPIO_PIN_SWITCH_ENABLE | GPIO_PIN_HV_DC_ENABLE);
}
#endif