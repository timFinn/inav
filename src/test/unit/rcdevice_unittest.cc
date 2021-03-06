/*
 * This file is part of Cleanflight.
 *
 * Cleanflight is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Cleanflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Cleanflight.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "gtest/gtest.h"

extern "C" {
    #include <stdbool.h>
    #include <stdint.h>
    #include <ctype.h>

    #include "platform.h"

    #include "common/bitarray.h"
    #include "common/maths.h"
    #include "common/utils.h"
    #include "common/streambuf.h"

    #include "config/parameter_group.h"
    #include "config/parameter_group_ids.h"

    #include "fc/rc_controls.h"
    #include "fc/rc_modes.h"


    #include "io/beeper.h"
    #include "io/serial.h"

    #include "scheduler/scheduler.h"
    #include "drivers/serial.h"
    #include "drivers/vcd.h"
    #include "io/rcdevice_cam.h"
    #include "io/osd.h"
    #include "io/rcdevice.h"

    #include "rx/rx.h"

    int16_t rcData[MAX_SUPPORTED_RC_CHANNEL_COUNT];     // interval [1000;2000]

    extern rcdeviceSwitchState_t switchStates[BOXCAMERA3 - BOXCAMERA1 + 1];
    extern runcamDevice_t *camDevice;
    extern bool needRelease;
    bool unitTestIsSwitchActivited(boxId_e boxId)
    {
        uint8_t adjustBoxID = boxId - BOXCAMERA1;
        rcdeviceSwitchState_s switchState = switchStates[adjustBoxID];
        return switchState.isActivated;
    }
}

#define MAX_RESPONSES_COUNT 10

typedef struct testData_s {
    bool isRunCamSplitPortConfigurated;
    bool isRunCamSplitOpenPortSupported;
    int8_t maxTimesOfRespDataAvailable;
    bool isAllowBufferReadWrite;
    uint8_t indexOfCurrentRespBuf;
    uint8_t responseBufCount;
    uint8_t responesBufs[MAX_RESPONSES_COUNT][RCDEVICE_PROTOCOL_MAX_PACKET_SIZE];
    uint8_t responseBufsLen[MAX_RESPONSES_COUNT];
    uint8_t responseDataReadPos;
    uint32_t millis;
} testData_t;

static testData_t testData;

static void clearResponseBuff()
{
    testData.indexOfCurrentRespBuf = 0;
    testData.responseBufCount = 0;
    memset(testData.responseBufsLen, 0, MAX_RESPONSES_COUNT);
    memset(testData.responesBufs, 0, MAX_RESPONSES_COUNT * 60);
}

static void addResponseData(uint8_t *data, uint8_t dataLen, bool withDataForFlushSerial)
{
    if (withDataForFlushSerial) {
        memcpy(testData.responesBufs[testData.responseBufCount], "0", 1);
        testData.responseBufsLen[testData.responseBufCount] = 1;
        testData.responseBufCount++;
    }

    
    memcpy(testData.responesBufs[testData.responseBufCount], data, dataLen);
    testData.responseBufsLen[testData.responseBufCount] = dataLen;
    testData.responseBufCount++;
}

TEST(RCDeviceTest, TestRCSplitInitWithoutPortConfigurated)
{
    runcamDevice_t device;

    memset(&testData, 0, sizeof(testData));
    bool result = runcamDeviceInit(&device);
    EXPECT_EQ(false, result);
}

TEST(RCDeviceTest, TestRCSplitInitWithoutOpenPortConfigurated)
{
    runcamDevice_t device;

    memset(&testData, 0, sizeof(testData));
    testData.isRunCamSplitOpenPortSupported = false;
    testData.isRunCamSplitPortConfigurated = true;

    bool result = runcamDeviceInit(&device);
    EXPECT_EQ(false, result);
}

TEST(RCDeviceTest, TestInitDevice)
{
    runcamDevice_t device;

    // test correct response
    memset(&testData, 0, sizeof(testData));
    testData.isRunCamSplitOpenPortSupported = true;
    testData.isRunCamSplitPortConfigurated = true;
    testData.isAllowBufferReadWrite = true;
    uint8_t responseData[] = { 0xCC, 0x01, 0x37, 0x00, 0xBD };
    addResponseData(responseData, sizeof(responseData), true);
    
    bool result = runcamDeviceInit(&device);
    EXPECT_EQ(result, true);
}

TEST(RCDeviceTest, TestInitDeviceWithInvalidResponse)
{
    runcamDevice_t device;

    // test correct response data with incorrect len
    memset(&testData, 0, sizeof(testData));
    testData.isRunCamSplitOpenPortSupported = true;
    testData.isRunCamSplitPortConfigurated = true;
    testData.isAllowBufferReadWrite = true;

    uint8_t responseData[] = { 0xCC, 0x01, 0x37, 0x00, 0xBD, 0x33 };
    addResponseData(responseData, sizeof(responseData), true);
    bool result = runcamDeviceInit(&device);
    EXPECT_EQ(result, true);
    clearResponseBuff();

    // invalid crc
    uint8_t responseDataWithInvalidCRC[] = { 0xCC, 0x01, 0x37, 0x00, 0xBE };
    addResponseData(responseDataWithInvalidCRC, sizeof(responseDataWithInvalidCRC), true);
    result = runcamDeviceInit(&device);
    EXPECT_EQ(result, false);
    clearResponseBuff();

    // incomplete response data
    uint8_t incompleteResponseData[] = { 0xCC, 0x01, 0x37 };
    addResponseData(incompleteResponseData, sizeof(incompleteResponseData), true);
    result = runcamDeviceInit(&device);
    EXPECT_EQ(result, false);
    clearResponseBuff();

    // test timeout
    memset(&testData, 0, sizeof(testData));
    testData.isRunCamSplitOpenPortSupported = true;
    testData.isRunCamSplitPortConfigurated = true;
    testData.isAllowBufferReadWrite = true;
    result = runcamDeviceInit(&device);
    EXPECT_EQ(result, false);
    clearResponseBuff();
}

TEST(RCDeviceTest, TestWifiModeChangeWithDeviceUnready)
{
    // test correct response
    memset(&testData, 0, sizeof(testData));
    testData.isRunCamSplitOpenPortSupported = true;
    testData.isRunCamSplitPortConfigurated = true;
    testData.isAllowBufferReadWrite = true;
    testData.maxTimesOfRespDataAvailable = 0;
    uint8_t responseData[] = { 0xCC, 0x01, 0x37, 0x00, 0xBC };
    addResponseData(responseData, sizeof(responseData), true);
    bool result = rcdeviceInit();
    EXPECT_EQ(result, false);

    // bind aux1, aux2, aux3 channel to wifi button, power button and change mode
    for (uint8_t i = 0; i <= (BOXCAMERA3 - BOXCAMERA1); i++) {
        memset(modeActivationConditionsMutable(i), 0, sizeof(modeActivationCondition_t));
    }

    // bind aux1 to wifi button with range [900,1600]
    modeActivationConditionsMutable(0)->auxChannelIndex = 0;
    modeActivationConditionsMutable(0)->modeId = BOXCAMERA1;
    modeActivationConditionsMutable(0)->range.startStep = CHANNEL_VALUE_TO_STEP(CHANNEL_RANGE_MIN);
    modeActivationConditionsMutable(0)->range.endStep = CHANNEL_VALUE_TO_STEP(1600);

    // bind aux2 to power button with range [1900, 2100]
    modeActivationConditionsMutable(1)->auxChannelIndex = 1;
    modeActivationConditionsMutable(1)->modeId = BOXCAMERA2;
    modeActivationConditionsMutable(1)->range.startStep = CHANNEL_VALUE_TO_STEP(1900);
    modeActivationConditionsMutable(1)->range.endStep = CHANNEL_VALUE_TO_STEP(2100);

    // bind aux3 to change mode with range [1300, 1600]
    modeActivationConditionsMutable(2)->auxChannelIndex = 2;
    modeActivationConditionsMutable(2)->modeId = BOXCAMERA3;
    modeActivationConditionsMutable(2)->range.startStep = CHANNEL_VALUE_TO_STEP(1300);
    modeActivationConditionsMutable(2)->range.endStep = CHANNEL_VALUE_TO_STEP(1600);

    // make the binded mode inactive
    rcData[modeActivationConditions(0)->auxChannelIndex + NON_AUX_CHANNEL_COUNT] = 1800;
    rcData[modeActivationConditions(1)->auxChannelIndex + NON_AUX_CHANNEL_COUNT] = 900;
    rcData[modeActivationConditions(2)->auxChannelIndex + NON_AUX_CHANNEL_COUNT] = 900;

    updateActivatedModes();

    // runn process loop
    rcdeviceUpdate(0);

    EXPECT_EQ(false, unitTestIsSwitchActivited(BOXCAMERA1));
    EXPECT_EQ(false, unitTestIsSwitchActivited(BOXCAMERA2));
    EXPECT_EQ(false, unitTestIsSwitchActivited(BOXCAMERA3));
}

TEST(RCDeviceTest, TestWifiModeChangeWithDeviceReady)
{
    // test correct response
    memset(&testData, 0, sizeof(testData));
    testData.isRunCamSplitOpenPortSupported = true;
    testData.isRunCamSplitPortConfigurated = true;
    testData.isAllowBufferReadWrite = true;
    testData.maxTimesOfRespDataAvailable = 0;
    uint8_t responseData[] = { 0xCC, 0x01, 0x37, 0x00, 0xBD };
    addResponseData(responseData, sizeof(responseData), true);
    bool result = rcdeviceInit();
    EXPECT_EQ(result, true);

    // bind aux1, aux2, aux3 channel to wifi button, power button and change mode
    for (uint8_t i = 0; i <= BOXCAMERA3 - BOXCAMERA1; i++) {
        memset(modeActivationConditionsMutable(i), 0, sizeof(modeActivationCondition_t));
    }

    // bind aux1 to wifi button with range [900,1600]
    modeActivationConditionsMutable(0)->auxChannelIndex = 0;
    modeActivationConditionsMutable(0)->modeId = BOXCAMERA1;
    modeActivationConditionsMutable(0)->range.startStep = CHANNEL_VALUE_TO_STEP(CHANNEL_RANGE_MIN);
    modeActivationConditionsMutable(0)->range.endStep = CHANNEL_VALUE_TO_STEP(1600);

    // bind aux2 to power button with range [1900, 2100]
    modeActivationConditionsMutable(1)->auxChannelIndex = 1;
    modeActivationConditionsMutable(1)->modeId = BOXCAMERA2;
    modeActivationConditionsMutable(1)->range.startStep = CHANNEL_VALUE_TO_STEP(1900);
    modeActivationConditionsMutable(1)->range.endStep = CHANNEL_VALUE_TO_STEP(2100);

    // bind aux3 to change mode with range [1300, 1600]
    modeActivationConditionsMutable(2)->auxChannelIndex = 2;
    modeActivationConditionsMutable(2)->modeId = BOXCAMERA3;
    modeActivationConditionsMutable(2)->range.startStep = CHANNEL_VALUE_TO_STEP(1900);
    modeActivationConditionsMutable(2)->range.endStep = CHANNEL_VALUE_TO_STEP(2100);

    rcData[modeActivationConditions(0)->auxChannelIndex + NON_AUX_CHANNEL_COUNT] = 1700;
    rcData[modeActivationConditions(1)->auxChannelIndex + NON_AUX_CHANNEL_COUNT] = 2000;
    rcData[modeActivationConditions(2)->auxChannelIndex + NON_AUX_CHANNEL_COUNT] = 1700;

    updateUsedModeActivationConditionFlags();
    updateActivatedModes();

    // runn process loop
    int8_t randNum = rand() % 127 + 6;
    testData.maxTimesOfRespDataAvailable = randNum;
    rcdeviceUpdate((timeUs_t)0);

    EXPECT_EQ(false, unitTestIsSwitchActivited(BOXCAMERA1));
    EXPECT_EQ(true, unitTestIsSwitchActivited(BOXCAMERA2));
    EXPECT_EQ(false, unitTestIsSwitchActivited(BOXCAMERA3));
}

TEST(RCDeviceTest, TestWifiModeChangeCombine)
{
    memset(&testData, 0, sizeof(testData));
    testData.isRunCamSplitOpenPortSupported = true;
    testData.isRunCamSplitPortConfigurated = true;
    testData.isAllowBufferReadWrite = true;
    testData.maxTimesOfRespDataAvailable = 0;
    uint8_t responseData[] = { 0xCC, 0x01, 0x37, 0x00, 0xBD };
    addResponseData(responseData, sizeof(responseData), true);
    bool result = rcdeviceInit();
    EXPECT_EQ(true, result);

    // bind aux1, aux2, aux3 channel to wifi button, power button and change mode
    for (uint8_t i = 0; i <= BOXCAMERA3 - BOXCAMERA1; i++) {
        memset(modeActivationConditionsMutable(i), 0, sizeof(modeActivationCondition_t));
    }


    // bind aux1 to wifi button with range [900,1600]
    modeActivationConditionsMutable(0)->auxChannelIndex = 0;
    modeActivationConditionsMutable(0)->modeId = BOXCAMERA1;
    modeActivationConditionsMutable(0)->range.startStep = CHANNEL_VALUE_TO_STEP(CHANNEL_RANGE_MIN);
    modeActivationConditionsMutable(0)->range.endStep = CHANNEL_VALUE_TO_STEP(1600);

    // bind aux2 to power button with range [1900, 2100]
    modeActivationConditionsMutable(1)->auxChannelIndex = 1;
    modeActivationConditionsMutable(1)->modeId = BOXCAMERA2;
    modeActivationConditionsMutable(1)->range.startStep = CHANNEL_VALUE_TO_STEP(1900);
    modeActivationConditionsMutable(1)->range.endStep = CHANNEL_VALUE_TO_STEP(2100);

    // bind aux3 to change mode with range [1300, 1600]
    modeActivationConditionsMutable(2)->auxChannelIndex = 2;
    modeActivationConditionsMutable(2)->modeId = BOXCAMERA3;
    modeActivationConditionsMutable(2)->range.startStep = CHANNEL_VALUE_TO_STEP(1900);
    modeActivationConditionsMutable(2)->range.endStep = CHANNEL_VALUE_TO_STEP(2100);

    // // make the binded mode inactive
    rcData[modeActivationConditions(0)->auxChannelIndex + NON_AUX_CHANNEL_COUNT] = 1700;
    rcData[modeActivationConditions(1)->auxChannelIndex + NON_AUX_CHANNEL_COUNT] = 2000;
    rcData[modeActivationConditions(2)->auxChannelIndex + NON_AUX_CHANNEL_COUNT] = 1700;
    updateActivatedModes();

    // runn process loop
    int8_t randNum = rand() % 127 + 6;
    testData.maxTimesOfRespDataAvailable = randNum;
    rcdeviceUpdate((timeUs_t)0);

    EXPECT_EQ(false, unitTestIsSwitchActivited(BOXCAMERA1));
    EXPECT_EQ(true, unitTestIsSwitchActivited(BOXCAMERA2));
    EXPECT_EQ(false, unitTestIsSwitchActivited(BOXCAMERA3));


    // // make the binded mode inactive
    rcData[modeActivationConditions(0)->auxChannelIndex + NON_AUX_CHANNEL_COUNT] = 1500;
    rcData[modeActivationConditions(1)->auxChannelIndex + NON_AUX_CHANNEL_COUNT] = 1300;
    rcData[modeActivationConditions(2)->auxChannelIndex + NON_AUX_CHANNEL_COUNT] = 1900;
    updateActivatedModes();
    rcdeviceUpdate((timeUs_t)0);
    EXPECT_EQ(true, unitTestIsSwitchActivited(BOXCAMERA1));
    EXPECT_EQ(false, unitTestIsSwitchActivited(BOXCAMERA2));
    EXPECT_EQ(true, unitTestIsSwitchActivited(BOXCAMERA3));


    rcData[modeActivationConditions(2)->auxChannelIndex + NON_AUX_CHANNEL_COUNT] = 1899;
    updateActivatedModes();
    rcdeviceUpdate((timeUs_t)0);
    EXPECT_EQ(false, unitTestIsSwitchActivited(BOXCAMERA3));

    rcData[modeActivationConditions(1)->auxChannelIndex + NON_AUX_CHANNEL_COUNT] = 2001;
    updateActivatedModes();
    rcdeviceUpdate((timeUs_t)0);
    EXPECT_EQ(true, unitTestIsSwitchActivited(BOXCAMERA1));
    EXPECT_EQ(true, unitTestIsSwitchActivited(BOXCAMERA2));
    EXPECT_EQ(false, unitTestIsSwitchActivited(BOXCAMERA3));
}

TEST(RCDeviceTest, Test5KeyOSDCableSimulationProtocol)
{
    memset(&testData, 0, sizeof(testData));
    testData.isRunCamSplitOpenPortSupported = true;
    testData.isRunCamSplitPortConfigurated = true;
    testData.isAllowBufferReadWrite = true;
    testData.maxTimesOfRespDataAvailable = 0;
    uint8_t responseData[] = { 0xCC, 0x01, 0x37, 0x00, 0xBD };
    addResponseData(responseData, sizeof(responseData), true);
    bool result = rcdeviceInit();
    EXPECT_EQ(true, result);

    // test timeout of open connection
    result = runcamDeviceOpen5KeyOSDCableConnection(camDevice);
    EXPECT_EQ(false, result);
    clearResponseBuff();

    // open connection with correct response
    uint8_t responseDataOfOpenConnection[] = { 0xCC, 0x11, 0xe7 };
    addResponseData(responseDataOfOpenConnection, sizeof(responseDataOfOpenConnection), true);
    result = runcamDeviceOpen5KeyOSDCableConnection(camDevice);
    EXPECT_EQ(true, result);
    clearResponseBuff();

    // open connection with correct response but wrong data length 
    uint8_t incorrectResponseDataOfOpenConnection1[] = { 0xCC, 0x11, 0xe7, 0x55 };
    addResponseData(incorrectResponseDataOfOpenConnection1, sizeof(incorrectResponseDataOfOpenConnection1), true);
    result = runcamDeviceOpen5KeyOSDCableConnection(camDevice);
    EXPECT_EQ(true, result);
    clearResponseBuff();
    
    // open connection with invalid crc
    uint8_t incorrectResponseDataOfOpenConnection2[] = { 0xCC, 0x10, 0x42 };
    addResponseData(incorrectResponseDataOfOpenConnection2, sizeof(incorrectResponseDataOfOpenConnection2), true);
    result = runcamDeviceOpen5KeyOSDCableConnection(camDevice);
    EXPECT_EQ(false, result);
    clearResponseBuff();

    // test timeout of close connection
    runcamDeviceClose5KeyOSDCableConnection(camDevice);
    EXPECT_EQ(false, result);
    clearResponseBuff();

    // close connection with correct response
    uint8_t responseDataOfCloseConnection[] = { 0xCC, 0x21, 0x11 };
    addResponseData(responseDataOfCloseConnection, sizeof(responseDataOfCloseConnection), true);
    result = runcamDeviceClose5KeyOSDCableConnection(camDevice);
    EXPECT_EQ(true, result);
    clearResponseBuff();

    // close connection with correct response but wrong data length 
    uint8_t responseDataOfCloseConnection1[] = { 0xCC, 0x21, 0x11, 0xC1 };
    addResponseData(responseDataOfCloseConnection1, sizeof(responseDataOfCloseConnection1), true);
    result = runcamDeviceClose5KeyOSDCableConnection(camDevice);
    EXPECT_EQ(true, result);
    clearResponseBuff();

    // close connection with response that invalid crc
    uint8_t responseDataOfCloseConnection2[] = { 0xCC, 0x21, 0xA1 };
    addResponseData(responseDataOfCloseConnection2, sizeof(responseDataOfCloseConnection2), true);
    result = runcamDeviceClose5KeyOSDCableConnection(camDevice);
    EXPECT_EQ(false, result);
    clearResponseBuff();

    // simulate press button with no response
    result = runcamDeviceSimulate5KeyOSDCableButtonPress(camDevice, RCDEVICE_PROTOCOL_5KEY_SIMULATION_SET);
    EXPECT_EQ(false, result);
    clearResponseBuff();

    // simulate press button with correct response
    uint8_t responseDataOfSimulation1[] = { 0xCC, 0xA5 };
    addResponseData(responseDataOfSimulation1, sizeof(responseDataOfSimulation1), true);
    result = runcamDeviceSimulate5KeyOSDCableButtonPress(camDevice, RCDEVICE_PROTOCOL_5KEY_SIMULATION_SET);
    EXPECT_EQ(true, result);
    clearResponseBuff();

    // simulate press button with correct response but wrong data length 
    uint8_t responseDataOfSimulation2[] = { 0xCC, 0xA5, 0x22 };
    addResponseData(responseDataOfSimulation2, sizeof(responseDataOfSimulation2), true);
    result = runcamDeviceSimulate5KeyOSDCableButtonPress(camDevice, RCDEVICE_PROTOCOL_5KEY_SIMULATION_SET);
    EXPECT_EQ(true, result);
    clearResponseBuff();

    // simulate press button event with incorrect response
    uint8_t responseDataOfSimulation3[] = { 0xCC, 0xB5, 0x22 };
    addResponseData(responseDataOfSimulation3, sizeof(responseDataOfSimulation3), true);
    result = runcamDeviceSimulate5KeyOSDCableButtonPress(camDevice, RCDEVICE_PROTOCOL_5KEY_SIMULATION_SET);
    EXPECT_EQ(false, result);
    clearResponseBuff();

    // simulate release button event 
    result = runcamDeviceSimulate5KeyOSDCableButtonRelease(camDevice);
    EXPECT_EQ(false, result);
    clearResponseBuff();

    // simulate release button with correct response
    uint8_t responseDataOfSimulation4[] = { 0xCC, 0xA5 };
    addResponseData(responseDataOfSimulation4, sizeof(responseDataOfSimulation4), true);
    result = runcamDeviceSimulate5KeyOSDCableButtonRelease(camDevice);
    EXPECT_EQ(true, result);
    clearResponseBuff();

    // simulate release button with correct response but wrong data length
    uint8_t responseDataOfSimulation5[] = { 0xCC, 0xA5, 0xFF };
    addResponseData(responseDataOfSimulation5, sizeof(responseDataOfSimulation5), true);
    result = runcamDeviceSimulate5KeyOSDCableButtonRelease(camDevice);
    EXPECT_EQ(true, result);
    clearResponseBuff();

    // simulate release button with incorrect response
    uint8_t responseDataOfSimulation6[] = { 0xCC, 0x31, 0xFF };
    addResponseData(responseDataOfSimulation6, sizeof(responseDataOfSimulation6), true);
    result = runcamDeviceSimulate5KeyOSDCableButtonRelease(camDevice);
    EXPECT_EQ(false, result);
    clearResponseBuff();
}

TEST(RCDeviceTest, Test5KeyOSDCableSimulationWithout5KeyFeatureSupport)
{
    // test simulation without device init
    rcData[THROTTLE] = 1500; // THROTTLE Mid
    rcData[ROLL] = 1500; // ROLL Mid
    rcData[PITCH] = 1500; // PITCH Mid
    rcData[YAW] = 1900; // Yaw High
    rcdeviceUpdate(0);
    EXPECT_EQ(false, rcdeviceInMenu);
    
    // init device that have not 5 key OSD cable simulation feature
    memset(&testData, 0, sizeof(testData));
    testData.isRunCamSplitOpenPortSupported = true;
    testData.isRunCamSplitPortConfigurated = true;
    testData.isAllowBufferReadWrite = true;
    testData.maxTimesOfRespDataAvailable = 0;
    uint8_t responseData[] = { 0xCC, 0x01, 0x37, 0x00, 0xBD };
    addResponseData(responseData, sizeof(responseData), true);
    bool result = rcdeviceInit();
    EXPECT_EQ(result, true);
    clearResponseBuff();

    // open connection, rcdeviceInMenu will be false if the codes is right
    uint8_t responseDataOfOpenConnection[] = { 0xCC, 0x11, 0xe7 };
    addResponseData(responseDataOfOpenConnection, sizeof(responseDataOfOpenConnection), false);
    rcdeviceUpdate(0);
    EXPECT_EQ(false, rcdeviceInMenu);
    clearResponseBuff();
}

TEST(RCDeviceTest, Test5KeyOSDCableSimulationWith5KeyFeatureSupport)
{
    // test simulation without device init
    rcData[THROTTLE] = 1500; // THROTTLE Mid
    rcData[ROLL] = 1500; // ROLL Mid
    rcData[PITCH] = 1500; // PITCH Mid
    rcData[YAW] = 1900; // Yaw High
    rcdeviceUpdate(0);
    EXPECT_EQ(false, rcdeviceInMenu);

    // init device that have not 5 key OSD cable simulation feature
    memset(&testData, 0, sizeof(testData));
    testData.isRunCamSplitOpenPortSupported = true;
    testData.isRunCamSplitPortConfigurated = true;
    testData.isAllowBufferReadWrite = true;
    testData.maxTimesOfRespDataAvailable = 0;
    uint8_t responseData[] = { 0xCC, 0x01, 0x3F, 0x00, 0xE5 };
    addResponseData(responseData, sizeof(responseData), true);
    bool result = rcdeviceInit();
    EXPECT_EQ(result, true);
    clearResponseBuff();

    // open connection
    uint8_t responseDataOfOpenConnection[] = { 0xCC, 0x11, 0xe7 };
    addResponseData(responseDataOfOpenConnection, sizeof(responseDataOfOpenConnection), true);
    rcdeviceUpdate(0);
    EXPECT_EQ(true, rcdeviceInMenu);
    EXPECT_EQ(true, needRelease);
    clearResponseBuff();
    // relase button 
    rcData[ROLL] = 1500; // ROLL Mid
    rcData[PITCH] = 1500; // PITCH Mid
    rcData[YAW] = 1500; // Yaw High
    uint8_t responseDataOfReleaseButton[] = { 0xCC, 0xA5 };
    addResponseData(responseDataOfReleaseButton, sizeof(responseDataOfReleaseButton), true);
    rcdeviceUpdate(0);
    EXPECT_EQ(false, needRelease);
    clearResponseBuff();

    // close connection
    rcData[THROTTLE] = 1500; // THROTTLE Mid
    rcData[ROLL] = 1500; // ROLL Mid
    rcData[PITCH] = 1500; // PITCH Mid
    rcData[YAW] = 900; // Yaw High
    uint8_t responseDataOfCloseConnection[] = { 0xCC, 0x21, 0x11 };
    addResponseData(responseDataOfCloseConnection, sizeof(responseDataOfCloseConnection), true);
    rcdeviceUpdate(0);
    EXPECT_EQ(false, rcdeviceInMenu);
    EXPECT_EQ(true, needRelease);
    clearResponseBuff();
    // relase button 
    rcData[ROLL] = 1500; // ROLL Mid
    rcData[PITCH] = 1500; // PITCH Mid
    rcData[YAW] = 1500; // Yaw High
    addResponseData(responseDataOfReleaseButton, sizeof(responseDataOfReleaseButton), true);
    rcdeviceUpdate(0);
    EXPECT_EQ(false, needRelease);
    clearResponseBuff();

    // open osd menu again
    rcData[THROTTLE] = 1500; // THROTTLE Mid
    rcData[ROLL] = 1500; // ROLL Mid
    rcData[PITCH] = 1500; // PITCH Mid
    rcData[YAW] = 1900; // Yaw High
    addResponseData(responseDataOfOpenConnection, sizeof(responseDataOfOpenConnection), true);
    rcdeviceUpdate(0);
    EXPECT_EQ(true, rcdeviceInMenu);
    EXPECT_EQ(true, needRelease);
    clearResponseBuff();
    // relase button 
    rcData[ROLL] = 1500; // ROLL Mid
    rcData[PITCH] = 1500; // PITCH Mid
    rcData[YAW] = 1500; // Yaw High
    addResponseData(responseDataOfReleaseButton, sizeof(responseDataOfReleaseButton), true);
    rcdeviceUpdate(0);
    EXPECT_EQ(false, needRelease);
    clearResponseBuff();

    // send down button event
    rcData[PITCH] = 900;
    addResponseData(responseDataOfReleaseButton, sizeof(responseDataOfReleaseButton), true);
    rcdeviceUpdate(0);
    EXPECT_EQ(true, needRelease);
    clearResponseBuff();
    // relase button 
    rcData[ROLL] = 1500; // ROLL Mid
    rcData[PITCH] = 1500; // PITCH Mid
    rcData[YAW] = 1500; // Yaw High
    addResponseData(responseDataOfReleaseButton, sizeof(responseDataOfReleaseButton), true);
    rcdeviceUpdate(0);
    EXPECT_EQ(false, needRelease);
    rcData[PITCH] = 1500; // rest down button
    clearResponseBuff();

    // simulate right button long press
    rcData[ROLL] = 1900;
    addResponseData(responseDataOfReleaseButton, sizeof(responseDataOfReleaseButton), true);
    rcdeviceUpdate(0);
    EXPECT_EQ(true, needRelease);
    rcdeviceUpdate(0);
    EXPECT_EQ(true, needRelease);
    rcdeviceUpdate(0);
    EXPECT_EQ(true, needRelease);
    clearResponseBuff();
    // send relase button event
    rcData[ROLL] = 1500; // ROLL Mid
    rcData[PITCH] = 1500; // PITCH Mid
    rcData[YAW] = 1500; // Yaw High
    addResponseData(responseDataOfReleaseButton, sizeof(responseDataOfReleaseButton), true);
    rcdeviceUpdate(0);
    EXPECT_EQ(false, needRelease);
    rcData[ROLL] = 1500; // reset right button
    clearResponseBuff();

    // simulate right button and get failed response, then FC should release the controller of joysticks
    rcData[ROLL] = 1900;
    addResponseData(responseDataOfReleaseButton, sizeof(responseDataOfReleaseButton), true);
    rcdeviceUpdate(0);
    EXPECT_EQ(true, needRelease);
    clearResponseBuff();
    // send relase button with empty response, so the release will failed
    rcData[ROLL] = 1500; // ROLL Mid
    rcData[PITCH] = 1500; // PITCH Mid
    rcData[YAW] = 1500; // Yaw High
    rcdeviceUpdate(0);
    EXPECT_EQ(true, needRelease);
    EXPECT_EQ(false, rcdeviceInMenu); // if failed on release button event, then FC side need release the controller of joysticks
    rcData[ROLL] = 1500; // rest right button
    // send again release button with correct response
    rcData[ROLL] = 1500; // ROLL Mid
    rcData[PITCH] = 1500; // PITCH Mid
    rcData[YAW] = 1500; // Yaw High
    clearResponseBuff();
    addResponseData(responseDataOfReleaseButton, sizeof(responseDataOfReleaseButton), true);
    rcdeviceUpdate(0);
    EXPECT_EQ(false, needRelease);
    EXPECT_EQ(false, rcdeviceInMenu);
    clearResponseBuff();

    // open OSD menu again
    rcData[THROTTLE] = 1500; // THROTTLE Mid
    rcData[ROLL] = 1500; // ROLL Mid
    rcData[PITCH] = 1500; // PITCH Mid
    rcData[YAW] = 1900; // Yaw High
    clearResponseBuff();
    addResponseData(responseDataOfOpenConnection, sizeof(responseDataOfOpenConnection), true);
    rcdeviceUpdate(0);
    EXPECT_EQ(true, rcdeviceInMenu);
    EXPECT_EQ(true, needRelease);
    clearResponseBuff();

    // relase button 
    rcData[ROLL] = 1500; // ROLL Mid
    rcData[PITCH] = 1500; // PITCH Mid
    rcData[YAW] = 1500; // Yaw High
    addResponseData(responseDataOfReleaseButton, sizeof(responseDataOfReleaseButton), true);
    rcdeviceUpdate(0);
    EXPECT_EQ(false, needRelease);
    clearResponseBuff();

    // send left event
    rcData[ROLL] = 900;
    addResponseData(responseDataOfReleaseButton, sizeof(responseDataOfReleaseButton), true);
    rcdeviceUpdate(0);
    EXPECT_EQ(true, needRelease);
    clearResponseBuff();
    // send relase button 
    rcData[ROLL] = 1500; // ROLL Mid
    rcData[PITCH] = 1500; // PITCH Mid
    rcData[YAW] = 1500; // Yaw High
    addResponseData(responseDataOfReleaseButton, sizeof(responseDataOfReleaseButton), true);
    rcdeviceUpdate(0);
    EXPECT_EQ(false, needRelease);
    EXPECT_EQ(true, rcdeviceInMenu);
    clearResponseBuff();
    rcData[ROLL] = 1500; // rest right button

    // close connection
    rcData[THROTTLE] = 1500; // THROTTLE Mid
    rcData[ROLL] = 1500; // ROLL Mid
    rcData[PITCH] = 1500; // PITCH Mid
    rcData[YAW] = 900; // Yaw High
    addResponseData(responseDataOfCloseConnection, sizeof(responseDataOfCloseConnection), true);
    rcdeviceUpdate(0);
    EXPECT_EQ(false, rcdeviceInMenu);
    EXPECT_EQ(true, needRelease);
    clearResponseBuff();
    // relase button 
    rcData[ROLL] = 1500; // ROLL Mid
    rcData[PITCH] = 1500; // PITCH Mid
    rcData[YAW] = 1500; // Yaw High
    addResponseData(responseDataOfReleaseButton, sizeof(responseDataOfReleaseButton), true);
    rcdeviceUpdate(0);
    EXPECT_EQ(false, needRelease);
    clearResponseBuff();
}

extern "C" {
    serialPort_t *openSerialPort(serialPortIdentifier_e identifier, serialPortFunction_e functionMask, serialReceiveCallbackPtr callback, uint32_t baudRate, portMode_t mode, portOptions_t options)
    {
        UNUSED(identifier);
        UNUSED(functionMask);
        UNUSED(baudRate);
        UNUSED(callback);
        UNUSED(mode);
        UNUSED(options);

        if (testData.isRunCamSplitOpenPortSupported) {
            static serialPort_t s;
            s.vTable = NULL;

            // common serial initialisation code should move to serialPort::init()
            s.rxBufferHead = s.rxBufferTail = 0;
            s.txBufferHead = s.txBufferTail = 0;
            s.rxBufferSize = 0;
            s.txBufferSize = 0;
            s.rxBuffer = s.rxBuffer;
            s.txBuffer = s.txBuffer;

            // callback works for IRQ-based RX ONLY
            s.rxCallback = NULL;
            s.baudRate = 0;

            return (serialPort_t *)&s;
        }

        return NULL;
    }

    serialPortConfig_t *findSerialPortConfig(serialPortFunction_e function)
    {
        UNUSED(function);
        if (testData.isRunCamSplitPortConfigurated) {
            static serialPortConfig_t portConfig;

            portConfig.identifier = SERIAL_PORT_USART3;
            portConfig.msp_baudrateIndex = BAUD_115200;
            portConfig.gps_baudrateIndex = BAUD_57600;
            portConfig.telemetry_baudrateIndex = BAUD_AUTO;
            portConfig.functionMask = FUNCTION_MSP;

            return &portConfig;
        }

        return NULL;
    }

    uint32_t serialRxBytesWaiting(const serialPort_t *instance) 
    { 
        UNUSED(instance);

        uint8_t bufIndex = testData.indexOfCurrentRespBuf;
        uint8_t leftDataLen = 0;
        if (testData.responseDataReadPos >= testData.responseBufsLen[bufIndex]) {
            return 0;
        } else {
            leftDataLen = testData.responseBufsLen[bufIndex] - testData.responseDataReadPos;
        }

        if (leftDataLen) {
            return leftDataLen;
        }

        return 0;
    }

    uint8_t serialRead(serialPort_t *instance) 
    { 
        UNUSED(instance);

        uint8_t bufIndex = testData.indexOfCurrentRespBuf;
        uint8_t *buffer = NULL;
        uint8_t leftDataLen = 0;
        if (testData.responseDataReadPos >= testData.responseBufsLen[bufIndex]) {
            leftDataLen = 0;
        } else {
            buffer = testData.responesBufs[bufIndex];
            leftDataLen = testData.responseBufsLen[bufIndex] - testData.responseDataReadPos;
        }

        if (leftDataLen) {
            return buffer[testData.responseDataReadPos++];
        }

        return 0; 
    }

    void sbufWriteString(sbuf_t *dst, const char *string) 
    { 
        UNUSED(dst); UNUSED(string); 

        if (testData.isAllowBufferReadWrite) {
            sbufWriteData(dst, string, strlen(string));
        }
    }
    void sbufWriteU8(sbuf_t *dst, uint8_t val) 
    { 
        UNUSED(dst); UNUSED(val); 

        if (testData.isAllowBufferReadWrite) {
            *dst->ptr++ = val;
        }
    }
    
    void sbufWriteData(sbuf_t *dst, const void *data, int len)
    {
        UNUSED(dst); UNUSED(data); UNUSED(len); 

        if (testData.isAllowBufferReadWrite) {
            memcpy(dst->ptr, data, len);
            dst->ptr += len;
            
        }
    }

    // modifies streambuf so that written data are prepared for reading
    void sbufSwitchToReader(sbuf_t *buf, uint8_t *base)
    {
        UNUSED(buf); UNUSED(base); 

        if (testData.isAllowBufferReadWrite) {
            buf->end = buf->ptr;
            buf->ptr = base;
        }
    }

    uint8_t sbufReadU8(sbuf_t *src)
    {
        if (testData.isAllowBufferReadWrite) {
            return *src->ptr++;
        }

        return 0;
    }

    void sbufAdvance(sbuf_t *buf, int size)
    {
        if (testData.isAllowBufferReadWrite) {
            buf->ptr += size;
        }
    }

    int sbufBytesRemaining(const sbuf_t *buf)
    {
        if (testData.isAllowBufferReadWrite) {
            return buf->end - buf->ptr;
        }
        return 0;
    }

    const uint8_t* sbufConstPtr(const sbuf_t *buf)
    {
        return buf->ptr;
    }

    void sbufReadData(const sbuf_t *src, void *data, int len)
    {
        if (testData.isAllowBufferReadWrite) {
            memcpy(data, src->ptr, len);
        }
    }

    uint16_t sbufReadU16(sbuf_t *src)
    {
        uint16_t ret;
        ret = sbufReadU8(src);
        ret |= sbufReadU8(src) << 8;
        return ret;
    }

    void sbufWriteU16(sbuf_t *dst, uint16_t val)
    {
        sbufWriteU8(dst, val >> 0);
        sbufWriteU8(dst, val >> 8);
    }

    void sbufWriteU16BigEndian(sbuf_t *dst, uint16_t val)
    {
        sbufWriteU8(dst, val >> 8);
        sbufWriteU8(dst, (uint8_t)val);
    }

    bool feature(uint32_t) { return false; }

    void serialWriteBuf(serialPort_t *instance, const uint8_t *data, int count) 
    { 
        UNUSED(instance); UNUSED(data); UNUSED(count); 

        // // reset the input buffer
        testData.responseDataReadPos = 0;
        testData.indexOfCurrentRespBuf++;
        // testData.maxTimesOfRespDataAvailable = testData.responseDataLen + 1;
    }

    serialPortConfig_t *findNextSerialPortConfig(serialPortFunction_e function)
    {
        UNUSED(function);

        return NULL;
    }

    void closeSerialPort(serialPort_t *serialPort)
    {
        UNUSED(serialPort);
    }

    uint8_t* sbufPtr(sbuf_t *buf)
    {
        return buf->ptr;
    }

    uint32_t sbufReadU32(sbuf_t *src)
    {
        uint32_t ret;
        ret = sbufReadU8(src);
        ret |= sbufReadU8(src) <<  8;
        ret |= sbufReadU8(src) << 16;
        ret |= sbufReadU8(src) << 24;
        return ret;
    }
    

    uint32_t millis(void) { return testData.millis++; }

    void beeper(beeperMode_e mode) { UNUSED(mode); }
    uint8_t armingFlags = 0;
    bool cmsInMenu;
    uint32_t resumeRefreshAt = 0;

    void failsafeOnRxSuspend(uint32_t ) {}
    void failsafeOnRxResume(void) {}
    void failsafeOnValidDataReceived(void) {}
    void failsafeOnValidDataFailed(void) {}
}
