/******************************************************************************

 @file jdllc.c

 @brief Joining Device Logical Link Controller
 This module is the Joining Device Logical Link Controller for the application.

 Group: WCS LPC
 Target Device: cc13x0

 ******************************************************************************
 
 Copyright (c) 2016-2019, Texas Instruments Incorporated
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:

 *  Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

 *  Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

 *  Neither the name of Texas Instruments Incorporated nor the names of
    its contributors may be used to endorse or promote products derived
    from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 ******************************************************************************
 
 
 *****************************************************************************/

/******************************************************************************
 Includes
 *****************************************************************************/
#include <string.h>
#include <stdint.h>

#include "mac_util.h"
#include "jdllc.h"
#include "sensor.h"
#include "ssf.h"
#include "config.h"
#include <ti/sysbios/knl/Clock.h>

/******************************************************************************
 Constants and definitions
 *****************************************************************************/
#define JDLLC_CHAN_LOWEST             0
/* Returns if the specific bit in the scan channel map array is set */
#define JDLLC_IS_CHANNEL_MASK_SET(a, c) \
                  (*((uint8_t*)(a) + ((c) - JDLLC_CHAN_LOWEST) / 8) & \
                  ((uint8_t) 1 << (((c) - JDLLC_CHAN_LOWEST) % 8)))
#define JDLLC_DEFAULT_KEY_SOURCE {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33}
/*
 sensor reporting interval in milliseconds used to restart sensor reading
 after coordinator realignment
*/
#define JDLLC_RESTART_REPORTING_INTERVAL 1000
/*! link quality */
#define CONFIG_LINKQUALITY           1
/*! percent filter */
#define CONFIG_PERCENTFILTER         0xFF

/*! FH channel function value for channel hopping */
#define JDLLC_FH_CHANNEL_HOPPPING  0x02
#define FH_ASSOC_DELAY                2000
#define JDLLC_BEACON_ORDER_NON_BEACON 15
#define JDLLC_INVALID_VALUE           -1
#define JDLLC_INVALID_PAN              0xFFFF
#define JDLLC_INVALID_CHANNEL          0xFF
#define JDLLC_RETRY_POLL               500
#define DEFAULT_FH_SLEEP_FIXED_CHANNEL 0
#define WISUN_PANIE_PRESENT            0x1
#define WISUN_NETNAME_IE_PRESENT       0x2
#define WISUN_PANVER_IE_PRESENT        0x4
#define WISUN_GTKHASH_IE_PRESENT       0x8
#define GTK_HASH_LEN                   0x8
#define FH_MAX_ASYN_ON_GIVEN_CHANNEL   5
/******************************************************************************
 Security constants and definitions
 *****************************************************************************/

#define KEY_TABLE_ENTRIES 1
#define KEY_ID_LOOKUP_ENTRIES 1
#define KEY_DEVICE_TABLE_ENTRIES 8
#define KEY_USAGE_TABLE_ENTRIES 1
#define SECURITY_LEVEL_ENTRIES 1

#define MAC_FRAME_TYPE_DATA 1
#define MAC_DATA_REQ_FRAME 4

#define AUTO_REQUEST_SEC_LEVEL 0x00
/******************************************************************************
 Structures
 *****************************************************************************/
/* Device information, used to store default parameters */
typedef struct
{
    uint16_t panID;
    uint8_t channel;
    uint16_t coordShortAddr;
    uint8_t coordExtAddr[APIMAC_SADDR_EXT_LEN];
    uint16_t devShortAddr;
    uint8_t devExtAddr[APIMAC_SADDR_EXT_LEN];
    uint8_t beaconOrder;
    uint8_t superframeOrder;
    Jdllc_states_t currentJdllcState;
    Jdllc_states_t prevJdllcState;
    Jdllc_device_states_t currentDevState;
    Jdllc_device_states_t prevDevState;
    uint8_t dataFailures;
    uint32_t pollInterval;
} devInformation_t;

/******************************************************************************
 Global variables
 *****************************************************************************/
/* Task pending events */
uint16_t Jdllc_events = 0;
/* JDLLC statistics variable */
Jdllc_statistics_t Jdllc_statistics;
/*Default key for JDLLC */
uint8_t CONST Jdllc_keySource[] = JDLLC_DEFAULT_KEY_SOURCE;
/* Default value for initBroadcastMsg */
bool initBroadcastMsg = true;

/* Interim Delay Ticks (used for rejoin delay calculation) */
static uint_fast32_t interimDelayTicks = 0;

#ifdef DBG_APP
Jdllc_device_states_t appDbg_state[40];
uint8_t appDbg_sidx = 0;
#endif
/******************************************************************************
 Local variables
 *****************************************************************************/
/* structure containing device and its parents information*/
STATIC devInformation_t devInfoBlock =
                {
                  CONFIG_PAN_ID,
                  JDLLC_INVALID_CHANNEL,
                  0xFFFF,
                  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
                  0xFFFF,
                  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
                  CONFIG_MAC_BEACON_ORDER,
                  CONFIG_MAC_SUPERFRAME_ORDER,
                  Jdllc_states_initWaiting,
                  Jdllc_states_initWaiting,
                  Jdllc_deviceStates_scanActive,
                  Jdllc_deviceStates_scanActive,
                   0,
                  CONFIG_POLLING_INTERVAL
                };
/* default channel mask */
STATIC CONST uint8_t defaultChannelMask[APIMAC_154G_CHANNEL_BITMAP_SIZ] =
                CONFIG_CHANNEL_MASK;
/* copy of MAC API callbacks */
STATIC ApiMac_callbacks_t macCallbacksCopy =  { 0 };
/* copy of CLLC callbacks */
STATIC Jdllc_callbacks_t *pJdllcCallbacksCopy = (Jdllc_callbacks_t *)NULL;
/* current channel in FH sleep node hop sequence */
STATIC uint8_t sleepNodeChIdx = 0;
/* Flag to pick parent from incoming beacons or in FH networks send association
   request only once
 */
bool parentFound = false;

/* Flag to indicate whether a valid matching PAN ID has been identified from
 * an incoming beacon
 */
STATIC bool panIdMatch = false;

STATIC uint8_t numSyncLoss = 0;
/* netname to identify node */
STATIC CONST uint8_t fhNetname[APIMAC_FH_NET_NAME_SIZE_MAX] = CONFIG_FH_NETNAME;
STATIC uint8_t fhNumPASRcvdInTrickleWindow = 0;
STATIC uint8_t fhNumPCSRcvdInTrickleWindow = 0;
STATIC uint8_t fhAssociationAttempts = 0;
STATIC uint16_t fhPanAdvInterval = 6000;
STATIC uint16_t fhPanConfigInterval = 6000;
/* FH Channel Mask */
STATIC uint8_t fhChannelMask[] = CONFIG_FH_CHANNEL_MASK;

#ifdef FEATURE_MAC_SECURITY
/******************************************************************************
 Local security variables
 *****************************************************************************/

static CONST ApiMac_keyIdLookupDescriptor_t keyIdLookupList[] =
    {
      {
        /* Key identity data */
        { 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x03 },
        0x01 /* 9 octets */
      }
    };

/* Key device list can be modified at run time */
static CONST ApiMac_keyDeviceDescriptor_t keyDeviceList[] =
    {
      { 0x00, false, false },
      { 0x00, false, false },
      { 0x00, false, false },
      { 0x00, false, false },
      { 0x00, false, false },
      { 0x00, false, false },
      { 0x00, false, false },
      { 0x00, false, false }
    };

static CONST ApiMac_keyUsageDescriptor_t keyUsageList[] =
    {
      { MAC_FRAME_TYPE_DATA, MAC_DATA_REQ_FRAME }
    };

STATIC CONST ApiMac_keyDescriptor_t keyTable[] =
    {
      {
        (ApiMac_keyIdLookupDescriptor_t *)keyIdLookupList,
        KEY_ID_LOOKUP_ENTRIES,
        (ApiMac_keyDeviceDescriptor_t *)keyDeviceList,
        KEY_DEVICE_TABLE_ENTRIES,
        (ApiMac_keyUsageDescriptor_t *)keyUsageList,
        KEY_USAGE_TABLE_ENTRIES,
        KEY_TABLE_DEFAULT_KEY,
        0 /* frame counter */
      }
    };

STATIC CONST ApiMac_securityPibSecurityLevelEntry_t securityLevelEntry =
    {
      0,
      { MAC_FRAME_TYPE_DATA, MAC_DATA_REQ_FRAME, 0, false }
    };

STATIC CONST ApiMac_secLevel_t secLevel = ApiMac_secLevel_encMic32;

STATIC CONST ApiMac_keyIdMode_t secKeyIdMode = ApiMac_keyIdMode_8;
/* cant be zero for implicit key identifier */
STATIC CONST uint8_t secKeyIndex = 3;

STATIC bool macSecurity = CONFIG_SECURE;
#endif /* FEATURE_MAC_SECURITY */

/******************************************************************************
 Local Function Prototypes
 *****************************************************************************/
/* CLLC callbacks */
static void assocCnfCb(ApiMac_mlmeAssociateCnf_t *pData);
static void beaconNotifyIndCb(ApiMac_mlmeBeaconNotifyInd_t *pData);
static void scanCnfCb(ApiMac_mlmeScanCnf_t *pData);
static void disassoCnfCb(ApiMac_mlmeDisassociateCnf_t *pData);
static void disassocIndCb(ApiMac_mlmeDisassociateInd_t *pData);
static void wsAsyncIndCb(ApiMac_mlmeWsAsyncInd_t *pData);
static void syncLossCb(ApiMac_mlmeSyncLossInd_t *pData);
static void dataCnfCb(ApiMac_mcpsDataCnf_t *pData);
static void pollCnfCb(ApiMac_mlmePollCnf_t *pData);

static void switchState(Jdllc_device_states_t newState);
static void processState(Jdllc_device_states_t state);
static bool checkBeaconOrder(uint16_t superframeSpec);
static void sendAssocReq(void);
static void updateState(Jdllc_states_t state);
static void sendPollReq(void);
static void processCoordRealign(void);
static void sendScanReq(ApiMac_scantype_t type);
static void sendAsyncReq(ApiMac_wisunAsyncFrame_t frameType);
static uint8_t getFHSleepNodeHopChannel(void);
static void processFHEvent(uint8_t *pFhFrameVar,
                           ApiMac_wisunAsyncFrame_t frameType,
                           uint16_t trickleDuration);
static void populateInfo(ApiMac_deviceDescriptor_t *pDevInfo,
                         Llc_netInfo_t *pParentNetInfo);
static void handleMaxDataFail(void);

/******************************************************************************
 Public Functions
 *****************************************************************************/
/*!
 Initialize this module.

 Public function defined in jdllc.h
 */
void Jdllc_init(ApiMac_callbacks_t *pMacCbs, Jdllc_callbacks_t *pJdllcCbs)
{
    if(CONFIG_MAC_BEACON_ORDER == JDLLC_BEACON_ORDER_NON_BEACON)
    {
        /* non beacon network */
        devInfoBlock.currentDevState = Jdllc_deviceStates_scanActive;
        devInfoBlock.prevDevState = Jdllc_deviceStates_scanActive;
    }
    else if((CONFIG_MAC_BEACON_ORDER > 0) &&
            (CONFIG_MAC_BEACON_ORDER < JDLLC_BEACON_ORDER_NON_BEACON))
    {
        /* beacon network */
        devInfoBlock.currentDevState = Jdllc_deviceStates_scanPassive;
        devInfoBlock.prevDevState = Jdllc_deviceStates_scanPassive;
    }

    ApiMac_mlmeSetReqUint8(ApiMac_attribute_phyCurrentDescriptorId,
                           CONFIG_PHY_ID);
    ApiMac_mlmeSetReqBool(ApiMac_attribute_RxOnWhenIdle, true);

    /* Save callback */
    memcpy(&macCallbacksCopy, pMacCbs, sizeof(ApiMac_callbacks_t));
    pJdllcCallbacksCopy = pJdllcCbs;

    /* overwrite callbacks with llc callbacks */
    pMacCbs->pAssocCnfCb = assocCnfCb;
    pMacCbs->pDisassociateCnfCb = disassoCnfCb;
    pMacCbs->pDataCnfCb = dataCnfCb;
    pMacCbs->pPollCnfCb = pollCnfCb;
    pMacCbs->pDisassociateIndCb = disassocIndCb;
    if(!CONFIG_FH_ENABLE)
    {
        pMacCbs->pBeaconNotifyIndCb = beaconNotifyIndCb;
        pMacCbs->pScanCnfCb = scanCnfCb;
        pMacCbs->pSyncLossIndCb = syncLossCb;
    }
    else
    {
        pMacCbs->pWsAsyncIndCb = wsAsyncIndCb;
        devInfoBlock.coordShortAddr = FH_COORD_SHORT_ADDR;
    }

    /* Initialize poll clock */
    if(!CONFIG_RX_ON_IDLE)
    {
        Ssf_initializePollClock();
    }

    if(CONFIG_FH_ENABLE)
    {
        uint8_t sizeOfChannelMask, idx;
        sizeOfChannelMask = sizeof(fhChannelMask)/sizeof(uint8_t);

        /* PIB for FH are set when we receive the IEs*/
        /* initialize trickle timer clock */
        Ssf_initializeTrickleClock();
        /* initialize FH association delay clock */
        Ssf_initializeFHAssocClock();

        /* enable frequency hopping operation */

        ApiMac_mlmeSetFhReqUint8(ApiMac_FHAttribute_unicastDwellInterval,
                                 CONFIG_DWELL_TIME);
        ApiMac_mlmeSetFhReqUint8(ApiMac_FHAttribute_broadcastDwellInterval,
                                 CONFIG_DWELL_TIME);
        ApiMac_mlmeSetFhReqArray(ApiMac_FHAttribute_netName,
                                 (uint8_t*)&fhNetname[0]);
        /* set up the number of NON-sleep and sleep device */
        ApiMac_mlmeSetFhReqUint16(ApiMac_FHAttribute_numNonSleepDevice,
                                 FH_NUM_NON_SLEEPY_HOPPING_NEIGHBORS);
        ApiMac_mlmeSetFhReqUint16(ApiMac_FHAttribute_numSleepDevice,
                                 FH_NUM_NON_SLEEPY_FIXED_CHANNEL_NEIGHBORS);


        /* if non sleepy FH device */
        if(CONFIG_RX_ON_IDLE)
        {
            uint8_t excludeChannels[APIMAC_154G_CHANNEL_BITMAP_SIZ];

            /* set PIB to enable channel hopping*/
            ApiMac_mlmeSetFhReqUint8(
                            ApiMac_FHAttribute_unicastChannelFunction,
                            JDLLC_FH_CHANNEL_HOPPPING);
            ApiMac_mlmeSetFhReqUint8(
                            ApiMac_FHAttribute_broadcastChannelFunction,
                            JDLLC_FH_CHANNEL_HOPPPING);

            /* set of Exclude Channels */
            if(sizeOfChannelMask > APIMAC_154G_CHANNEL_BITMAP_SIZ)
            {
                sizeOfChannelMask = APIMAC_154G_CHANNEL_BITMAP_SIZ;
            }
            memset(excludeChannels, 0, APIMAC_154G_CHANNEL_BITMAP_SIZ);
            for(idx = 0; idx < sizeOfChannelMask; idx++)
            {
                excludeChannels[idx] = ~fhChannelMask[idx];
            }
            ApiMac_mlmeSetFhReqArray(ApiMac_FHAttribute_unicastExcludedChannels,
                                     excludeChannels);
            ApiMac_mlmeSetFhReqArray(ApiMac_FHAttribute_broadcastExcludedChannels,
                                     excludeChannels);
        }
        else
        {
            uint8_t bitIndex, chIndex;
            /* set PIB to enable fixed channel*/
            ApiMac_mlmeSetFhReqUint8(ApiMac_FHAttribute_unicastChannelFunction,
                                     0);
            ApiMac_mlmeSetFhReqUint8(
                            ApiMac_FHAttribute_broadcastChannelFunction, 0);

            /*Initialize the hop sequence to account for maxChannels*/
            chIndex = 0;
            for(idx = 0; idx < sizeOfChannelMask; idx++)
            {
                for(bitIndex = 0; bitIndex < 8; bitIndex ++)
                {
                    if(chIndex >= APIMAC_154G_MAX_NUM_CHANNEL)
                    {
                        fhChannelMask[idx] &= ~(1 << bitIndex);
                    }
                    chIndex++;
                }
            }
            /* set fixed channel in FH PIB */
            ApiMac_mlmeSetFhReqUint16(ApiMac_FHAttribute_unicastFixedChannel,
                                      (uint16_t) getFHSleepNodeHopChannel());
        }


        fhPanConfigInterval = CONFIG_PAN_CONFIG_SOLICIT_CLK_DURATION;
        fhPanAdvInterval = CONFIG_PAN_ADVERT_SOLICIT_CLK_DURATION;

        /* Start FH */
        ApiMac_startFH();

    }
    else
    {
        /* turn off RX until joining */
        ApiMac_mlmeSetReqBool(ApiMac_attribute_RxOnWhenIdle,
                              CONFIG_RX_ON_IDLE);

        /* Initialize scan backoff clock */
        Ssf_initializeScanBackoffClock();
    }
}

/*!
 Jdllc task processing.

 Public function defined in jdllc.h
 */
void Jdllc_process(void)
{
    /* The LLC has an event */
    if(Jdllc_events & JDLLC_PAS_EVT)
    {
        if(CONFIG_FH_ENABLE)
        {

            processFHEvent(&fhNumPASRcvdInTrickleWindow,
                           ApiMac_wisunAsyncFrame_advertisementSolicit,
                           fhPanAdvInterval);
        }

        /* Clear the event */
        Util_clearEvent(&Jdllc_events, JDLLC_PAS_EVT);
    }

    /* The LLC has an PC event */
    if(Jdllc_events & JDLLC_PCS_EVT)
    {
        if(CONFIG_FH_ENABLE)
        {
            processFHEvent(&fhNumPCSRcvdInTrickleWindow,
                           ApiMac_wisunAsyncFrame_configSolicit,
                           fhPanConfigInterval);
        }

        /* Clear the event */
        Util_clearEvent(&Jdllc_events, JDLLC_PCS_EVT);
    }

    /* Process poll event */
    if(Jdllc_events & JDLLC_POLL_EVT)
    {
        bool autoRequest;

        ApiMac_mlmeGetReqBool(ApiMac_attribute_autoRequest, &autoRequest);

        if( ( (CONFIG_MAC_BEACON_ORDER == JDLLC_BEACON_ORDER_NON_BEACON) ||
              (autoRequest == false) ) &&
             !CERTIFICATION_TEST_MODE)
        {
            if((devInfoBlock.currentJdllcState == Jdllc_states_joined)
                || (devInfoBlock.currentJdllcState == Jdllc_states_rejoined))
            {
                /* set poll timer */
                Ssf_setPollClock(devInfoBlock.pollInterval);
            }

            /* send poll request */
            sendPollReq();
        }

        /* Clear the event */
        Util_clearEvent(&Jdllc_events, JDLLC_POLL_EVT);
    }

    /* Send association request */
    if(Jdllc_events & JDLLC_ASSOCIATE_REQ_EVT)
    {
        /* Send Associate request */
        sendAssocReq();

        /* Clear the event */
        Util_clearEvent(&Jdllc_events, JDLLC_ASSOCIATE_REQ_EVT);
    }

    /* Process coordinator realignment */
    if(Jdllc_events & JDLLC_COORD_REALIGN)
    {
        if(!CONFIG_FH_ENABLE)
        {
            processCoordRealign();
        }
        /* Clear the event */
        Util_clearEvent(&Jdllc_events, JDLLC_COORD_REALIGN);
   }

    /* Process scan backoff in case of orphan scan */
    if(Jdllc_events & JDLLC_SCAN_BACKOFF)
    {
        switchState(devInfoBlock.prevDevState);
        if(devInfoBlock.prevDevState == Jdllc_deviceStates_scanOrphan) {
            ApiMac_mlmeSetReqBool(ApiMac_attribute_RxOnWhenIdle, true);
        }
        /* Clear the event */
        Util_clearEvent(&Jdllc_events, JDLLC_SCAN_BACKOFF);
    }

    /* Process state change event */
    if(Jdllc_events & JDLLC_STATE_CHANGE_EVT)
    {
        /* Process LLC Event */
        processState(devInfoBlock.currentDevState);

        /* Clear the event */
        Util_clearEvent(&Jdllc_events, JDLLC_STATE_CHANGE_EVT);
    }

    /* If any events have been set then post the application
     * semaphore so the event is processed.
     */
    if(Jdllc_events)
    {
        Ssf_PostAppSem();
    }
}

/*!
 Join the network

 Public function defined in jdllc.h
 */
void Jdllc_join(void)
{
    /* set state */
    updateState(Jdllc_states_joining);

    /* if beacon enabled network  passive scan or
     * if non beacon network  active scan*/
    if(CONFIG_FH_ENABLE)
    {
        ApiMac_mlmeSetReqBool(ApiMac_attribute_RxOnWhenIdle, true);
        /* start trickle timer for PAS */
        Ssf_setTrickleClock(fhPanAdvInterval,
                            ApiMac_wisunAsyncFrame_advertisementSolicit);
    }
    else
    {
        if(CONFIG_MAC_BEACON_ORDER == JDLLC_BEACON_ORDER_NON_BEACON)
        {
            /* non beacon network */
            switchState(Jdllc_deviceStates_scanActive);
        }
        else if((CONFIG_MAC_BEACON_ORDER > 0) &&
                (CONFIG_MAC_BEACON_ORDER < JDLLC_BEACON_ORDER_NON_BEACON))
        {
            /* beacon network */
            switchState(Jdllc_deviceStates_scanPassive);
        }
    }
}

/*!
 Rejoin network

 Public function defined in jdllc.h
 */
void Jdllc_rejoin(ApiMac_deviceDescriptor_t *pDevInfo,
                  Llc_netInfo_t *pParentInfo)
{
    /* set state */
    updateState(Jdllc_states_initRestoring);

    /* update device information variables */
    devInfoBlock.panID = pDevInfo->panID;
    devInfoBlock.channel = pParentInfo->channel;
    Util_copyExtAddr(&devInfoBlock.devExtAddr, &pDevInfo->extAddress);

    devInfoBlock.devShortAddr = pDevInfo->shortAddress;
    Util_copyExtAddr(&devInfoBlock.coordExtAddr,
                     &pParentInfo->devInfo.extAddress);
    devInfoBlock.coordShortAddr = pParentInfo->devInfo.shortAddress;

    /* update MAC PIBs */
    ApiMac_mlmeSetReqUint16(ApiMac_attribute_panId, devInfoBlock.panID);
    ApiMac_mlmeSetReqUint16(ApiMac_attribute_shortAddress,
                            devInfoBlock.devShortAddr);
    ApiMac_mlmeSetReqArray(ApiMac_attribute_coordExtendedAddress,
                           devInfoBlock.coordExtAddr);

    if(!CONFIG_FH_ENABLE)
    {
        ApiMac_mlmeSetReqUint8(ApiMac_attribute_logicalChannel,
                               devInfoBlock.channel);
        ApiMac_mlmeSetReqUint16(ApiMac_attribute_coordShortAddress,
                                devInfoBlock.coordShortAddr);

        if((CONFIG_MAC_BEACON_ORDER > 0) &&
           (CONFIG_MAC_BEACON_ORDER < JDLLC_BEACON_ORDER_NON_BEACON))
        {
            ApiMac_mlmeSetReqBool(ApiMac_attribute_RxOnWhenIdle,
                                  CONFIG_RX_ON_IDLE);

            /* send sync request for beacon enabled device */
            switchState(Jdllc_deviceStates_syncReq);

            /* device joined */
            if(pJdllcCallbacksCopy && pJdllcCallbacksCopy->pJoinedCb)
            {
                pJdllcCallbacksCopy->pJoinedCb(pDevInfo, pParentInfo);
            }
            updateState(Jdllc_states_rejoined);
        }
    }
    else
    {
        parentFound = false;
        ApiMac_mlmeSetReqBool(ApiMac_attribute_RxOnWhenIdle, true);
        /* start trickle timer for PCS */
        Ssf_setTrickleClock(fhPanConfigInterval,
                            ApiMac_wisunAsyncFrame_configSolicit);

    }

    if(!CONFIG_FH_ENABLE)
    {
        if(!CONFIG_RX_ON_IDLE)
        {
            /* set event for polling if sleepy device*/
            Util_setEvent(&Jdllc_events, JDLLC_POLL_EVT);
        }
        else
        {
            uint32_t randomNum;
            randomNum =
                ((ApiMac_randomByte() << 16) + (ApiMac_randomByte() << 8)
                 + ApiMac_randomByte());
            randomNum = (randomNum % JDLLC_RESTART_REPORTING_INTERVAL) +
                    SENSOR_MIN_POLL_TIME;
            Ssf_setReadingClock(randomNum);
        }
    }
}

/*!
 Set the poll interval.

 Public function defined in jdllc.h
 */
void Jdllc_setPollRate(uint32_t pollInterval)
{
    devInfoBlock.pollInterval = pollInterval;
}

/*!
 Send disassociation request.

 Public function defined in jdllc.h
 */
void Jdllc_sendDisassociationRequest(void)
{
    ApiMac_mlmeDisassociateReq_t disassocReq;

    /* Zero-init the data strcture
     * set up the sec field: no security
     * */
    memset(&disassocReq,0x00, sizeof(disassocReq));

    if(CONFIG_FH_ENABLE && CONFIG_RX_ON_IDLE)
    {
        disassocReq.deviceAddress.addrMode = ApiMac_addrType_extended;
        Util_copyExtAddr(&disassocReq.deviceAddress.addr.extAddr,
                         &devInfoBlock.coordExtAddr);
    }
    else
    {
        disassocReq.deviceAddress.addrMode = ApiMac_addrType_short;
        disassocReq.deviceAddress.addr.shortAddr = devInfoBlock.coordShortAddr;
    }

    disassocReq.devicePanId = devInfoBlock.panID;
    disassocReq.disassociateReason = ApiMac_disassocateReason_device;
    disassocReq.txIndirect = false;
    ApiMac_mlmeDisassociateReq(&disassocReq);
}

#ifdef FEATURE_MAC_SECURITY
/*!
 Initialize the MAC Security

 Public function defined in jdllc.h
 */
void Jdllc_securityInit(uint32_t frameCounter)
{
    if(macSecurity == true)
    {
        ApiMac_secAddKeyInitFrameCounter_t secInfo;

        memcpy(secInfo.key, keyTable[0].key, APIMAC_KEY_MAX_LEN);
        secInfo.frameCounter = frameCounter;
        secInfo.replaceKeyIndex = 0;
        secInfo.newKeyFlag = true;
        secInfo.lookupDataSize = APIMAC_KEY_LOOKUP_LONG_LEN;
        memcpy(secInfo.lookupData, keyIdLookupList[0].lookupData,
               (APIMAC_MAX_KEY_LOOKUP_LEN));
#ifdef FEATURE_SECURE_COMMISSIONING
        secInfo.networkKey = true;
#endif
        ApiMac_secAddKeyInitFrameCounter(&secInfo);
        ApiMac_mlmeSetSecurityReqArray(
                                ApiMac_securityAttribute_defaultKeySource,
                                (void *) Jdllc_keySource);

        ApiMac_mlmeSetSecurityReqStruct(ApiMac_securityAttribute_keyTable,
                                        (void *)NULL);

        /* Set the number of security keys */
        ApiMac_mlmeSetSecurityReqUint16(ApiMac_securityAttribute_keyTableEntries, (uint16_t)KEY_TABLE_ENTRIES);

        /* Write a security level entry to PIB */
        ApiMac_mlmeSetSecurityReqStruct(
                        ApiMac_securityAttribute_securityLevelEntry,
                        (void *)&securityLevelEntry);

        /* Set the MAC security */
        ApiMac_mlmeSetReqBool(ApiMac_attribute_securityEnabled, macSecurity);

        /* set security for auto request for beacon enabled network to zero */
        if((CONFIG_MAC_BEACON_ORDER > 0) &&
           (CONFIG_MAC_BEACON_ORDER < JDLLC_BEACON_ORDER_NON_BEACON))
        {
            ApiMac_mlmeSetSecurityReqUint8(
                            ApiMac_securityAttribute_autoRequestSecurityLevel,
                            AUTO_REQUEST_SEC_LEVEL);
        }
    }
}

/*!
 Fill in the security structure

 Public function defined in jdllc.h
 */
void Jdllc_securityFill(ApiMac_sec_t *pSec)
{
    if(pSec)
    {
        if(macSecurity == true)
        {
            memcpy(pSec->keySource, keyIdLookupList[0].lookupData,
                   (APIMAC_KEY_SOURCE_MAX_LEN));
            pSec->securityLevel = secLevel;
            pSec->keyIdMode = secKeyIdMode;
            pSec->keyIndex = secKeyIndex;
        }
    }
}

/*!
 Check the security level against expected level

 Public function defined in jdllc.h
 */
bool Jdllc_securityCheck(ApiMac_sec_t *pSec)
{
    bool ret = false;

    if(macSecurity == true)
    {
        if(pSec)
        {
            if(pSec->securityLevel == secLevel)
            {
                ret = true;
            }
        }
    }
    else
    {
       ret = true;
    }

    return(ret);
}


/*!
 Add a device to the MAC security device table.

 Public function defined in sensor.h
 */
ApiMac_status_t Jdllc_addSecDevice(uint16_t panID, uint16_t shortAddr,
                         ApiMac_sAddrExt_t *pExtAddr, uint32_t frameCounter)
{
    if(macSecurity == true)
    {
        ApiMac_secAddDevice_t device;
#ifndef FEATURE_SECURE_COMMISSIONING
        uint8_t keyIndex = 0;
#endif
        device.panID = panID;
        device.shortAddr = shortAddr;
        memcpy(device.extAddr, pExtAddr, sizeof(ApiMac_sAddrExt_t));
        device.frameCounter = frameCounter;

        device.exempt = false;
#ifndef FEATURE_SECURE_COMMISSIONING
        /* get the key lookup information from the initial loaded key */
        device.keyIdLookupDataSize = keyIdLookupList[keyIndex].lookupDataSize;
        memcpy(device.keyIdLookupData, keyIdLookupList[keyIndex].lookupData,
               (APIMAC_MAX_KEY_LOOKUP_LEN));

        device.uniqueDevice = false;
        device.duplicateDevFlag = false;
#endif
        return(ApiMac_secAddDevice(&device));
    }
    else
    {
        return(ApiMac_status_success);
    }
}
#endif /* FEATURE_MAC_SECURITY */

/******************************************************************************
 Local Functions
 *****************************************************************************/
/*!
 * @brief       Switch to the new  coordinator state and set the event bit.
 *
 * @param       newState - next state of coordinator
 */
static void switchState(Jdllc_device_states_t newState)
{
#ifdef DBG_APP
    appDbg_state[appDbg_sidx] = newState;
    appDbg_sidx = (appDbg_sidx + 1) % 40;
#endif
    devInfoBlock.currentDevState = newState;
    Util_setEvent(&Jdllc_events, JDLLC_STATE_CHANGE_EVT);
}

/*!
 * @brief       Function to transition various states involved with scan request
 *               and start request before the coordinator is started.
 *
 * @param       state - current startup state of coordinator
 */
static void processState(Jdllc_device_states_t state)
{
    ApiMac_mlmeSyncReq_t syncReq;

    switch(state)
    {
        case Jdllc_deviceStates_scanActive:

            /* ensure RX is on for the scan */
            if(!CONFIG_FH_ENABLE)
            {
                ApiMac_mlmeSetReqBool(ApiMac_attribute_RxOnWhenIdle, true);
            }

            /* Active scan */
            sendScanReq(ApiMac_scantype_active);
            break;

        case Jdllc_deviceStates_scanPassive:
            /* ensure RX is on for the scan */
            if(!CONFIG_FH_ENABLE)
            {
                ApiMac_mlmeSetReqBool(ApiMac_attribute_RxOnWhenIdle, true);
            }
            /* Passive scan */
            sendScanReq(ApiMac_scantype_passive);
            break;

        case Jdllc_deviceStates_syncReq:
            ApiMac_mlmeSetReqUint8(ApiMac_attribute_beaconOrder,
                                   devInfoBlock.beaconOrder);
            ApiMac_mlmeSetReqUint8(ApiMac_attribute_superframeOrder,
                                   devInfoBlock.superframeOrder);
            ApiMac_mlmeSetReqUint16(ApiMac_attribute_coordShortAddress,
                                    devInfoBlock.coordShortAddr);
            ApiMac_mlmeSetReqUint16(ApiMac_attribute_panId, devInfoBlock.panID);
            /* Sync request for beacon enabled devices */
            syncReq.logicalChannel = devInfoBlock.channel;
            syncReq.channelPage = CONFIG_CHANNEL_PAGE;
            syncReq.phyID = CONFIG_PHY_ID;
            syncReq.trackBeacon = true;
            ApiMac_mlmeSyncReq(&syncReq);

            /* Wait for Beacon Sync and verify that the joining device only
             * connects to a coordinator of the same PAN Id. */
            if((devInfoBlock.currentJdllcState == Jdllc_states_joining)
                && panIdMatch)
            {
                Util_setEvent(&Jdllc_events, JDLLC_ASSOCIATE_REQ_EVT);
            }
            break;

        case Jdllc_deviceStates_scanOrphan:
            /* Orphan scan */
            if(!CONFIG_FH_ENABLE)
            {
                sendScanReq(ApiMac_scantype_orphan);
            }
            break;

        default:
            break;
    }
}

/*!
 * @brief       Check if the incoming frame's beacon order matches the network
 *              type of device
 *
 * @param       superframeSpec - super frame spec of incoming beacon
 *
 * @return      true if matches, else false
 */
static bool checkBeaconOrder(uint16_t superframeSpec)
{
    if(CONFIG_MAC_BEACON_ORDER == JDLLC_BEACON_ORDER_NON_BEACON)
    {
        if(APIMAC_SFS_BEACON_ORDER(superframeSpec) == CONFIG_MAC_BEACON_ORDER)
        {
            return (true);
        }
    }
    else if((APIMAC_SFS_BEACON_ORDER(superframeSpec) <= CONFIG_MAC_BEACON_ORDER))
    {
        return (true);
    }

    return (false);
}

/*!
 * @brief       Get the next channel for sleep node hopping based on
 *              chanel mask and sleepNodeChIdx
 *
 * @param       none
 *
 * @return      Channel to be used by sleep node
 */
static uint8_t getFHSleepNodeHopChannel(void)
{
    uint8_t curChBitMap = 0;
    uint8_t curChListPos = 0, bitIdx;
    uint8_t i, chanBitMapSize, startChListPos, retCh;

    chanBitMapSize = sizeof(fhChannelMask)/sizeof(uint8_t);

    curChListPos = sleepNodeChIdx >> 3;
    startChListPos = curChListPos;
    bitIdx = sleepNodeChIdx & 7;

    if((!chanBitMapSize) || (curChListPos >= chanBitMapSize))
    {
        return(DEFAULT_FH_SLEEP_FIXED_CHANNEL);
    }

    curChBitMap = fhChannelMask[curChListPos] >> bitIdx;

    if(!curChBitMap)
    {
        do
        {
            curChListPos++;
            if(curChListPos > (chanBitMapSize - 1))
            {
                curChListPos = 0;
            }
            curChBitMap = fhChannelMask[curChListPos];
            sleepNodeChIdx = curChListPos * 8;
        }while((!curChBitMap) && (curChListPos != startChListPos));
    }

    if(curChBitMap != 0)
    {
        i = sleepNodeChIdx;
        while(i < (8*(curChListPos + 1)))
        {
            if(curChBitMap & 1)
            {
                retCh = sleepNodeChIdx;
                /* Increment chIndex for next Call */
                sleepNodeChIdx += 1;
                return(retCh);
            }
            else
            {
                sleepNodeChIdx += 1;
                curChBitMap >>= 1;
            }
            i++;
        }
    }

    /* An array of all Zeros */
    return(DEFAULT_FH_SLEEP_FIXED_CHANNEL);
}

/*!
 * @brief       Process  Beacon Notification callback.
 *
 * @param       pData - pointer MAC Beacon indication info
 */
static void beaconNotifyIndCb(ApiMac_mlmeBeaconNotifyInd_t *pData)
{
    /* check beacon type */
    if(pData->beaconType == ApiMac_beaconType_normal)
    {
        if(parentFound == false)
        {
            /* Check if the received beacon is from a
             * coordinator which is not in the blacklist */
            if(Ssf_findBlackListIndex(
                            &pData->panDesc.coordAddress) ==
                                         JDLLC_INVALID_VALUE)
            {
                /* Check if association bit permit is set */
                if(APIMAC_SFS_ASSOCIATION_PERMIT(
                                pData->panDesc.superframeSpec))
                {
                    /* Check for beacon order match */
                    if(checkBeaconOrder(pData->panDesc.superframeSpec) == true)
                    {
                        if(devInfoBlock.panID == JDLLC_INVALID_PAN)
                        {
                            /* Device can join any network , associate with
                             * first coordinator from which beacon is received */
                            devInfoBlock.panID = pData->panDesc.coordPanId;
                            panIdMatch = true;
                        }
                        /* Check the incoming PAN ID to see if it's a valid coordinator */
                        else if (devInfoBlock.panID == pData->panDesc.coordPanId)
                        {
                            panIdMatch = true;
                        }

                        devInfoBlock.channel = pData->panDesc.logicalChannel;
                        devInfoBlock.coordShortAddr = pData->panDesc
                                        .coordAddress.addr.shortAddr;
                        if(APIMAC_SFS_BEACON_ORDER( pData->panDesc.superframeSpec)
                                        != JDLLC_BEACON_ORDER_NON_BEACON)
                        {
                            devInfoBlock.beaconOrder = APIMAC_SFS_BEACON_ORDER(
                                            pData->panDesc.superframeSpec);
                            devInfoBlock.superframeOrder =
                                APIMAC_SFS_SUPERFRAME_ORDER(
                                                pData->panDesc.superframeSpec);
                        }

                        if(devInfoBlock.beaconOrder == JDLLC_BEACON_ORDER_NON_BEACON)
                        {
                            parentFound = true;
#ifdef FREQ_2_4G
                            ApiMac_srcMatchEnable();
#endif
                            Ssf_stopScanBackoffClock();
                        }
                    }
                }
            }
        }
    }

    /* Callback to MAC API */
    if(macCallbacksCopy.pBeaconNotifyIndCb != NULL)
    {
        macCallbacksCopy.pBeaconNotifyIndCb(pData);
    }
}

/*!
 * @brief       Process  Scan Confirm  callback.
 *
 * @param       pData - pointer to Scan Confirm
 */
static void scanCnfCb(ApiMac_mlmeScanCnf_t *pData)
{
    if(pData->status == ApiMac_status_success)
    {
        if(pData->scanType == ApiMac_scantype_active)
        {
            /* Only send association requests for a matching PAN */
            if(panIdMatch)
            {
                /* Set event to send Association Request */
                Util_setEvent(&Jdllc_events, JDLLC_ASSOCIATE_REQ_EVT);
            }
            else {
                /* The current scan didn't return a matching beacon */

                /* Turn off receiver until next scan if RX on idle is false*/
                if ((!CONFIG_FH_ENABLE) && (CONFIG_RX_ON_IDLE == false))
                {
                    ApiMac_mlmeSetReqBool(ApiMac_attribute_RxOnWhenIdle, CONFIG_RX_ON_IDLE);
                }

                /* schedule next scan */
                devInfoBlock.prevDevState = devInfoBlock.currentDevState;
                Ssf_setScanBackoffClock(CONFIG_SCAN_BACKOFF_INTERVAL);
            }
        }
        else if(pData->scanType == ApiMac_scantype_passive)
        {
            if(panIdMatch)
            {
                /* Send sync request for beacon enabled device */
                switchState(Jdllc_deviceStates_syncReq);
            }
            else
            {
                /* scan failed turn off receiver until next scan if RX on idle is false*/
                if ((!CONFIG_FH_ENABLE) && (CONFIG_RX_ON_IDLE == false))
                {
                    ApiMac_mlmeSetReqBool(ApiMac_attribute_RxOnWhenIdle, CONFIG_RX_ON_IDLE);
                }

                /* schedule next scan */
                devInfoBlock.prevDevState = devInfoBlock.currentDevState;
                Ssf_setScanBackoffClock(CONFIG_SCAN_BACKOFF_INTERVAL);
            }
        }
        else if(pData->scanType == ApiMac_scantype_orphan)
        {
            numSyncLoss = 0;
            /* Coordinator realignment received, set event to process it */
            Util_setEvent(&Jdllc_events, JDLLC_COORD_REALIGN);
        }
    }
    else
    {
        /* scan failed, turn off receiver until next scan if RX on idle is false */
        if ((!CONFIG_FH_ENABLE) && (CONFIG_RX_ON_IDLE == false))
        {
            ApiMac_mlmeSetReqBool(ApiMac_attribute_RxOnWhenIdle, CONFIG_RX_ON_IDLE);
        }

        if(((pData->scanType == ApiMac_scantype_orphan) && (pData->status
                        == ApiMac_status_noBeacon)))

        {
            devInfoBlock.prevDevState = devInfoBlock.currentDevState;
            Ssf_setScanBackoffClock(CONFIG_ORPHAN_BACKOFF_INTERVAL);
        }
        else if(pData->scanType == ApiMac_scantype_active)
        {
            /* Reset previously found PAN Id match state */
            panIdMatch = false;

            devInfoBlock.prevDevState = devInfoBlock.currentDevState;
            Ssf_setScanBackoffClock(CONFIG_SCAN_BACKOFF_INTERVAL);
        }
        else
        {
            if((CONFIG_MAC_BEACON_ORDER > 0) && (CONFIG_MAC_BEACON_ORDER
                            < JDLLC_BEACON_ORDER_NON_BEACON))
            {
                /* Reset previously found PAN Id match state */
                panIdMatch = false;

                devInfoBlock.prevDevState = devInfoBlock.currentDevState;
                Ssf_setScanBackoffClock(CONFIG_SCAN_BACKOFF_INTERVAL);
            }
        }
    }

    if(macCallbacksCopy.pScanCnfCb != NULL)
    {
        macCallbacksCopy.pScanCnfCb(pData);
    }
}

/*!
 * @brief       Handle Jdllc callback for assoc Confirm
 *
 * @param       pData - pointer to Associate Confirm structure
 */
static void assocCnfCb(ApiMac_mlmeAssociateCnf_t *pData)
{
    Llc_netInfo_t parentInfo;
    ApiMac_deviceDescriptor_t devInfo;
    uint16_t randomNum = 0;

    if(pData->status == ApiMac_assocStatus_success)
    {
        parentInfo.devInfo.panID = devInfoBlock.panID;
        devInfo.shortAddress = pData->assocShortAddress;

        ApiMac_mlmeGetReqArray(ApiMac_attribute_extendedAddress,
                               devInfoBlock.devExtAddr);
        Util_copyExtAddr(&devInfo.extAddress, &devInfoBlock.devExtAddr);

        /* set device short address PIB */
        ApiMac_mlmeSetReqUint16(ApiMac_attribute_shortAddress,
                                pData->assocShortAddress);
        devInfo.panID = devInfoBlock.panID;

        ApiMac_mlmeGetReqArray(ApiMac_attribute_coordExtendedAddress,
                               devInfoBlock.coordExtAddr);

        Util_copyExtAddr(&parentInfo.devInfo.extAddress,
                         &devInfoBlock.coordExtAddr);
        parentInfo.devInfo.shortAddress = devInfoBlock.coordShortAddr;

        if(CONFIG_FH_ENABLE)
        {
            parentInfo.fh = true;
            Ssf_setTrickleClock(0, ApiMac_wisunAsyncFrame_advertisementSolicit);
            Ssf_setTrickleClock(0, ApiMac_wisunAsyncFrame_configSolicit);
        }
        else
        {
            parentInfo.fh = false;
            parentInfo.channel = devInfoBlock.channel;
        }

        /* stop scan backoff timer */
        Ssf_setScanBackoffClock(0);

        ApiMac_mlmeSetReqBool(ApiMac_attribute_RxOnWhenIdle, CONFIG_RX_ON_IDLE);

        if(CONFIG_FH_ENABLE ||
          (devInfoBlock.beaconOrder != JDLLC_BEACON_ORDER_NON_BEACON))
        {
            /* flag to ensure no action for further Async messages received */
            parentFound = true;
            initBroadcastMsg = true;
            /* Set FH Coordinator Short Address to handle frames from coordinator
               with short address in non-sleepy FH devices */
            ApiMac_mlmeSetReqUint16(ApiMac_attribute_coordShortAddress,
                                            devInfoBlock.coordShortAddr);
        }

        /* device joined */
        if(pJdllcCallbacksCopy && pJdllcCallbacksCopy->pJoinedCb)
        {
            pJdllcCallbacksCopy->pJoinedCb(&devInfo, &parentInfo);
        }
        if(CONFIG_FH_ENABLE)
        {
            if((devInfoBlock.currentJdllcState == Jdllc_states_initRestoring)||
               (devInfoBlock.currentJdllcState == Jdllc_states_orphan &&
                devInfoBlock.prevJdllcState == Jdllc_states_rejoined))
            {
                updateState(Jdllc_states_rejoined);
            }
            else
            {
                updateState(Jdllc_states_joined);
            }
        }
        else
        {
             updateState(Jdllc_states_joined);
        }

        /* for sleepy devices */
        if((!CONFIG_RX_ON_IDLE))
        {
            if((devInfoBlock.currentJdllcState == Jdllc_states_joined) ||
                  (devInfoBlock.currentJdllcState == Jdllc_states_rejoined))
            {
                /* start poll timer  */
                if(CONFIG_FH_ENABLE)
                {
                    randomNum = ((ApiMac_randomByte() << 8) +
                                    ApiMac_randomByte());
                    Ssf_setPollClock(((uint32_t)randomNum %
                                      CONFIG_FH_START_POLL_DATA_RAND_WINDOW));
                }
                else
                {
                    Util_setEvent(&Jdllc_events, JDLLC_POLL_EVT);
                }
            }
        }

    }
    else if((pData->status == ApiMac_assocStatus_panAtCapacity) ||
            (pData->status == ApiMac_assocStatus_panAccessDenied))
    {
        /* Stop Attempting to Join as PAN has reached Capacity or Access denied*/
        switchState(Jdllc_deviceStates_accessDenied);
        updateState(Jdllc_states_accessDenied);
        /* Update stats */
        Sensor_msgStats.joinFails++;
        /* Enter sleep state based on configuration */
        ApiMac_mlmeSetReqBool(ApiMac_attribute_RxOnWhenIdle, CONFIG_RX_ON_IDLE);
    }
    else
    {
        parentFound = false;

        if(!CONFIG_FH_ENABLE)
        {
            /* could not associate with parent, scan for new parent */
            if(CONFIG_MAC_BEACON_ORDER == JDLLC_BEACON_ORDER_NON_BEACON)
            {
                /* non beacon network */
                switchState(Jdllc_deviceStates_scanActive);
                Ssf_setScanBackoffClock(CONFIG_SCAN_BACKOFF_INTERVAL);
            }
            else if((CONFIG_MAC_BEACON_ORDER > 0) && (CONFIG_MAC_BEACON_ORDER <
                                         JDLLC_BEACON_ORDER_NON_BEACON))
            {
                if(devInfoBlock.currentDevState != Jdllc_deviceStates_scanPassive)
                {
                    switchState(Jdllc_deviceStates_scanPassive);
                }
            }
        }
        else
        {
            fhAssociationAttempts++;
            if(fhAssociationAttempts < CONFIG_FH_MAX_ASSOCIATION_ATTEMPTS)
            {
                Ssf_setFHAssocClock(FH_ASSOC_DELAY);
            }
            else
            {
                /* enable Rx to receive PC */
                ApiMac_mlmeSetReqBool(ApiMac_attribute_RxOnWhenIdle, true);
                /* start trickle timer for PCS */
                Ssf_setTrickleClock(fhPanConfigInterval,
                                    ApiMac_wisunAsyncFrame_configSolicit);
            }
        }
        /* Update stats */
        Sensor_msgStats.joinFails++;
    }

    if(macCallbacksCopy.pAssocCnfCb != NULL)
    {
        macCallbacksCopy.pAssocCnfCb(pData);
    }
}

/*!
 * @brief       Handle Disassociate Confirm callback
 *
 * @param       pData - pointer to Disassociate Confirm structure
 */
static void disassoCnfCb(ApiMac_mlmeDisassociateCnf_t *pData)
{
    if(pData->status == ApiMac_status_success)
    {
        /* stop polling */
        if(!CONFIG_RX_ON_IDLE)
        {
            Ssf_setPollClock(0);
        }
        /* enable looking for new parent */
        parentFound = false;

        /* set devInfoBlock back to defaults */
        devInfoBlock.panID = CONFIG_PAN_ID;
        devInfoBlock.channel = JDLLC_INVALID_CHANNEL;
        devInfoBlock.coordShortAddr = 0xFFFF;
        memset(&devInfoBlock.coordExtAddr[0], 0x00, APIMAC_SADDR_EXT_LEN);
        devInfoBlock.devShortAddr = 0xFFFF;
        memset(&devInfoBlock.devExtAddr[0], 0x00, APIMAC_SADDR_EXT_LEN);
        devInfoBlock.beaconOrder = CONFIG_MAC_BEACON_ORDER;
        devInfoBlock.superframeOrder = CONFIG_MAC_SUPERFRAME_ORDER;
        /* Below two commented lines will be set by updateState()*/
        /*devInfoBlock.currentJdllcState = Jdllc_states_initWaiting;*/
        /*devInfoBlock.prevJdllcState = Jdllc_states_initWaiting;*/
        devInfoBlock.currentDevState = Jdllc_deviceStates_scanActive;
        devInfoBlock.prevDevState = Jdllc_deviceStates_scanActive;
        devInfoBlock.dataFailures = 0;
        devInfoBlock.pollInterval = CONFIG_POLLING_INTERVAL;

        /* change state back to initWaiting and print on UART */
        updateState(Jdllc_states_initWaiting);
    }

    if(pJdllcCallbacksCopy && pJdllcCallbacksCopy->pDisassocCnfCb)
    {
        pJdllcCallbacksCopy->pDisassocCnfCb(&pData->deviceAddress.addr.extAddr,
                                            pData->status);
    }

    if(macCallbacksCopy.pDisassociateCnfCb != NULL)
    {
        macCallbacksCopy.pDisassociateCnfCb(pData);
    }
}

/*!
 * @brief       callback for Async indication
 *
 * @param       pData - pointer to Async indication structure
 */
static void wsAsyncIndCb(ApiMac_mlmeWsAsyncInd_t *pData)
{
    ApiMac_status_t status;
    ApiMac_payloadIeRec_t *pPayloadGroupRec = NULL;
    uint8_t wisunPiePresent = 0;
    uint8_t netname[APIMAC_FH_NET_NAME_SIZE_MAX] = {0};
    uint16_t panSize;
    uint8_t useParentBSIE;
    uint8_t routingMethod;
    uint8_t routeCost = 0;
    uint8_t eapolReady;
    uint8_t fanTpsVersion;
    uint16_t panVersion;
    uint8_t gtkHash0[GTK_HASH_LEN], gtkHash1[GTK_HASH_LEN];
    uint8_t gtkHash2[GTK_HASH_LEN], gtkHash3[GTK_HASH_LEN];

    /* Parse group IEs */
    status = ApiMac_parsePayloadGroupIEs(pData->pPayloadIE, pData->payloadIeLen,
                                         &pPayloadGroupRec);

    if((status == ApiMac_status_success) && (pPayloadGroupRec != NULL))
    {
        ApiMac_payloadIeRec_t *pGroup = pPayloadGroupRec;

        while(pGroup != NULL)
        {
            if(pGroup->item.ieId == ApiMac_payloadIEGroup_WiSUN)
            {
                ApiMac_payloadIeRec_t *pPayloadSubRec = NULL;

                status = ApiMac_parsePayloadSubIEs(pGroup->item.pIEContent,
                                                   pGroup->item.ieContentLen,
                                                   &pPayloadSubRec);
                if((status == ApiMac_status_success) && (pPayloadSubRec!= NULL))
                {
                    ApiMac_payloadIeRec_t *pSubGroup = pPayloadSubRec;

                    while(pSubGroup != NULL)
                    {
                        uint8_t *pIEContent = pSubGroup->item.pIEContent;

                        switch(pSubGroup->item.ieId)
                        {
                            case ApiMac_wisunSubIE_netNameIE:
                                if(pSubGroup->item.ieContentLen <=
                                    APIMAC_FH_NET_NAME_SIZE_MAX)
                                {
                                    memset(&netname, 0,
                                           APIMAC_FH_NET_NAME_SIZE_MAX);
                                    memcpy(&netname, pIEContent,
                                           pSubGroup->item.ieContentLen);
                                    wisunPiePresent |= WISUN_NETNAME_IE_PRESENT;
                                }
                                break;

                            case ApiMac_wisunSubIE_PANIE:
                                /* set PAN size */
                                memcpy(&panSize, pIEContent, sizeof(uint16_t));
                                ApiMac_mlmeSetFhReqUint16(
                                                ApiMac_FHAttribute_panSize,
                                                panSize);
                                pIEContent += sizeof(uint16_t);
                                 /*set routing cost */
                                routeCost = *pIEContent;
                                pIEContent += sizeof(uint8_t);
                                /* set if propagating parent BSIE*/
                                useParentBSIE = ((*pIEContent) >> 0) & 1;
                                /* set routing method */
                                routingMethod = ((*pIEContent) >> 1) & 1;
                                /* set EAPOL ready attribute */
                                eapolReady = ((*pIEContent) >> 2) & 1;
                                 /*set FAN TPS version */
                                fanTpsVersion = ((*pIEContent) >> 5) & 1;
                                wisunPiePresent |= WISUN_PANIE_PRESENT;
                                break;

                            case ApiMac_wisunSubIE_PANVersionIE:
                                memcpy(&panVersion, pIEContent,
                                       sizeof(uint16_t));
                                wisunPiePresent |= WISUN_PANVER_IE_PRESENT;

                                break;

                            case ApiMac_wisunSubIE_GTKHashIE:
                                /* get gtkHas0 */
                                memcpy(gtkHash0, pIEContent, GTK_HASH_LEN);
                                pIEContent += GTK_HASH_LEN;
                                /* get gtkHash1 */
                                memcpy(gtkHash1, pIEContent, GTK_HASH_LEN);
                                pIEContent += GTK_HASH_LEN;
                                /* get gtkHash2 */
                                memcpy(gtkHash2, pIEContent, GTK_HASH_LEN);
                                pIEContent += GTK_HASH_LEN;
                                /* get gtkHash3 */
                                memcpy(gtkHash3, pIEContent, GTK_HASH_LEN);
                                wisunPiePresent |= WISUN_GTKHASH_IE_PRESENT;
                                break;
                        }

                        /* move to the next item*/
                        pSubGroup = pSubGroup->pNext;
                    }

                  /* Free the IE List allocated by ApiMac_parsePayloadSubIEs()*/
                    ApiMac_freeIEList(pPayloadSubRec);
                }
            }

            /* Move to next item*/
            pGroup = pGroup->pNext;
        }

         /* Free the IE List allocated by ApiMac_parsePayloadGroupIEs() */
        ApiMac_freeIEList(pPayloadGroupRec);
    }

    if((pData->fhFrameType != ApiMac_fhFrameType_config) &&
       ((!(wisunPiePresent & WISUN_NETNAME_IE_PRESENT)) ||
       (memcmp(netname, fhNetname, APIMAC_FH_NET_NAME_SIZE_MAX) != 0)))
    {
        /* Drop PAS, PCS or PA from other networks */
        return;
    }

    if(pData->fhFrameType == ApiMac_fhFrameType_panAdvertSolicit)
    {
        fhNumPASRcvdInTrickleWindow++;
    }

    if(pData->fhFrameType == ApiMac_fhFrameType_configSolicit)
    {
        fhNumPCSRcvdInTrickleWindow++;
    }

    if(pData->fhFrameType == ApiMac_fhFrameType_panAdvert)
    {
        /* PA is received , increment statistics */
        Jdllc_statistics.fhNumPAReceived++;
        if(wisunPiePresent & WISUN_PANIE_PRESENT)
        {
            /*
             set PAN coordinator source address if
             route cost = 0
            */
            if(routeCost == 0)
            {
                Util_copyExtAddr(&devInfoBlock.coordExtAddr,
                                 &pData->srcAddr.addr.extAddr);
                ApiMac_mlmeSetReqArray(ApiMac_attribute_coordExtendedAddress,
                                       devInfoBlock.coordExtAddr);
            }

            ApiMac_mlmeSetFhReqUint8(
                            ApiMac_FHAttribute_routingCost,
                            routeCost++);
            ApiMac_mlmeSetFhReqUint8(
                            ApiMac_FHAttribute_useParentBSIE,
                            useParentBSIE);
            ApiMac_mlmeSetFhReqUint8(
                           ApiMac_FHAttribute_routingMethod,
                           routingMethod);
            ApiMac_mlmeSetFhReqUint8(
                            ApiMac_FHAttribute_eapolReady,
                            eapolReady);
            ApiMac_mlmeSetFhReqUint8(
                           ApiMac_FHAttribute_fanTPSVersion,
                           fanTpsVersion);
        }

        /* set PIB to track parent */
        ApiMac_mlmeSetFhReqArray(ApiMac_FHAttribute_trackParentEUI,
                                 (uint8_t *) (pData->srcAddr.addr.extAddr));
        /* set PAN */
        ApiMac_mlmeSetReqUint16(ApiMac_attribute_panId, pData->srcPanId);

#ifdef FEATURE_MAC_SECURITY
        /* add parent to security device table */
        Jdllc_addSecDevice(pData->srcPanId, FH_COORD_SHORT_ADDR,
                           &(pData->srcAddr.addr.extAddr), pData->frameCntr);
#endif /* FEATURE_MAC_SECURITY */

        /* set join related PIBS */
        devInfoBlock.panID = pData->srcPanId;
        Util_copyExtAddr(&devInfoBlock.coordExtAddr,
                         &pData->srcAddr.addr.extAddr);

        if(parentFound == false)
        {
            /* Stop PAS Timer */
            Ssf_setTrickleClock(0, ApiMac_wisunAsyncFrame_advertisementSolicit);
            /* set trickle timer for PCS */
            Ssf_setTrickleClock(fhPanConfigInterval,
                                ApiMac_wisunAsyncFrame_configSolicit);
        }

    }
    else if(pData->fhFrameType == ApiMac_fhFrameType_config)
    {
        if(wisunPiePresent & WISUN_PANVER_IE_PRESENT)
        {
            ApiMac_mlmeSetFhReqUint16(
                            ApiMac_FHAttribute_panVersion,
                            panVersion);
        }

        if(wisunPiePresent & WISUN_GTKHASH_IE_PRESENT)
        {
            ApiMac_mlmeSetFhReqArray(
                            ApiMac_FHAttribute_gtk0Hash,
                            gtkHash0);
            ApiMac_mlmeSetFhReqArray(
                            ApiMac_FHAttribute_gtk1Hash,
                            gtkHash1);
            ApiMac_mlmeSetFhReqArray(
                            ApiMac_FHAttribute_gtk2Hash,
                            gtkHash2);
            ApiMac_mlmeSetFhReqArray(
                            ApiMac_FHAttribute_gtk3Hash,
                            gtkHash3);
        }

        /* PC is received , parse IEs and increment statistics */
        Jdllc_statistics.fhNumPANConfigReceived++;
        /* set PIB to track parent */
        ApiMac_mlmeSetFhReqArray(ApiMac_FHAttribute_trackParentEUI,
                                 (uint8_t *) (pData->srcAddr.addr.extAddr));
        /* stop PCS Timer */
        Ssf_setTrickleClock(0, ApiMac_wisunAsyncFrame_configSolicit);

        if(parentFound == false)
        {
            parentFound = true;
            initBroadcastMsg = true;
            fhAssociationAttempts = 0;
            ApiMac_mlmeSetReqBool(ApiMac_attribute_RxOnWhenIdle, CONFIG_RX_ON_IDLE);
            /* Send association request if not associated previously */
            /* Increase delay for the first association attempt */
            Ssf_setFHAssocClock(FH_ASSOC_DELAY + FH_ASSOC_DELAY);
        }
    }

    if(macCallbacksCopy.pWsAsyncIndCb != NULL)
    {
        macCallbacksCopy.pWsAsyncIndCb(pData);
    }
}

/*!
 * @brief       Send Association request
 */
static void sendAssocReq(void)
{
    ApiMac_mlmeAssociateReq_t assocReq;
    memset(&assocReq, 0, sizeof(ApiMac_mlmeAssociateReq_t));
    assocReq.coordPanId = devInfoBlock.panID;

    if(CONFIG_FH_ENABLE && CONFIG_RX_ON_IDLE)
    {
        assocReq.coordAddress.addrMode = ApiMac_addrType_extended;
        Util_copyExtAddr(&assocReq.coordAddress.addr.extAddr,
                         &devInfoBlock.coordExtAddr);
    }
    else
    {
        assocReq.logicalChannel = devInfoBlock.channel;
        assocReq.coordAddress.addrMode = ApiMac_addrType_short;
        assocReq.coordAddress.addr.shortAddr = devInfoBlock.coordShortAddr;
    }

    if(CONFIG_FH_ENABLE)
    {
        assocReq.logicalChannel = 0;
    }

    assocReq.channelPage = CONFIG_CHANNEL_PAGE;
    assocReq.phyID = CONFIG_PHY_ID;
    assocReq.sec.securityLevel = ApiMac_secLevel_none;
    assocReq.capabilityInformation.allocAddr = true;
    assocReq.capabilityInformation.ffd = false;
    assocReq.capabilityInformation.panCoord = false;
    assocReq.capabilityInformation.rxOnWhenIdle = CONFIG_RX_ON_IDLE;
    ApiMac_mlmeAssociateReq(&assocReq);

    /* Update stats */
    Sensor_msgStats.joinAttempts++;
}

/*!
 * @brief       Process sync loss callback
 *
 * @param       pData - Pointer to sync loss callback structure
 */
static void syncLossCb(ApiMac_mlmeSyncLossInd_t *pData)
{
    if(pData->reason == ApiMac_status_beaconLoss)
    {
        /* Initialize counter for re-join delay calculation */
        interimDelayTicks = Clock_getTicks();

        if((parentFound == false) && (numSyncLoss == 0)
            && (devInfoBlock.currentJdllcState == Jdllc_states_joining))
        {
            if(devInfoBlock.currentDevState != Jdllc_deviceStates_scanPassive)
            {
                switchState(Jdllc_deviceStates_scanPassive);
            }
        }
        else
        {
            /* Update stats */
            Sensor_msgStats.syncLossIndications++;

            if(numSyncLoss == 0)
            {
                numSyncLoss++;
                parentFound = false;
                /* retry sync request */
                switchState(Jdllc_deviceStates_syncReq);
            }
            else
            {
                if(!CONFIG_RX_ON_IDLE)
                {
                    /* Stop polling */
                    Ssf_setPollClock(0);
                }

                /* set up orphan scan */
                switchState(Jdllc_deviceStates_scanOrphan);
                updateState(Jdllc_states_orphan);
            }
        }
    }
    if(macCallbacksCopy.pSyncLossIndCb != NULL)
    {
        macCallbacksCopy.pSyncLossIndCb(pData);
    }
}

/*!
 * @brief       Update Jdllc state
 *
 * @param       state - new state
 */
static void updateState(Jdllc_states_t state)
{
    if(state != devInfoBlock.currentJdllcState)
    {
        devInfoBlock.prevJdllcState = devInfoBlock.currentJdllcState;
        devInfoBlock.currentJdllcState = state;

        if(state == Jdllc_states_orphan)
        {
            /* Stop the reporting timer */
            Ssf_setReadingClock(0);
        }

        if(pJdllcCallbacksCopy && pJdllcCallbacksCopy->pStateChangeCb)
        {
            /* state change callback */
            pJdllcCallbacksCopy->pStateChangeCb(devInfoBlock.currentJdllcState);
        }
    }

    if((state == Jdllc_states_rejoined) || (state == Jdllc_states_joined))
    {
        /* Calculate Max Rejoin Delay */
        uint_fast32_t rejoinDelay = (Clock_getTicks() - interimDelayTicks)
                                    /(TICKPERIOD_MS_US*1000);
        if((rejoinDelay > Sensor_msgStats.interimDelay)
            && (interimDelayTicks > 0))
        {
            Sensor_msgStats.interimDelay = (uint16_t)rejoinDelay;
            interimDelayTicks = 0;
        }
    }
}

/*!
 * @brief       Process data confirm callback
 *
 * @param       pData - pointer to data confirm structure
 */
static void dataCnfCb(ApiMac_mcpsDataCnf_t *pData)
{
    if(CONFIG_MAC_BEACON_ORDER == JDLLC_BEACON_ORDER_NON_BEACON)
    {
        if(pData->status == ApiMac_status_noAck)
        {
            /* track the number of failures  */
            devInfoBlock.dataFailures++;
            if(devInfoBlock.dataFailures == CONFIG_MAX_DATA_FAILURES)
            {
                handleMaxDataFail();
            }
        }
        else if(pData->status == ApiMac_status_success)
        {
            devInfoBlock.dataFailures = 0;
            if(devInfoBlock.currentJdllcState == Jdllc_states_initRestoring)
            {
                ApiMac_deviceDescriptor_t devInfo;
                Llc_netInfo_t parentNetInfo;
                populateInfo(&devInfo, &parentNetInfo);
                ApiMac_mlmeSetReqBool(ApiMac_attribute_RxOnWhenIdle,
                CONFIG_RX_ON_IDLE);

                /* device joined */
                if(pJdllcCallbacksCopy && pJdllcCallbacksCopy->pJoinedCb)
                {
                    pJdllcCallbacksCopy->pJoinedCb(&devInfo, &parentNetInfo);
                }

                updateState(Jdllc_states_rejoined);
            }
        }
    }

    if(macCallbacksCopy.pDataCnfCb != NULL)
    {
        macCallbacksCopy.pDataCnfCb(pData);
    }
}

/*!
 * @brief       Send Poll request
 */
static void sendPollReq()
{
    ApiMac_mlmePollReq_t pollReq;
    memset(&pollReq, 0, sizeof(ApiMac_mlmePollReq_t));
    pollReq.coordPanId = devInfoBlock.panID;
    if(CONFIG_FH_ENABLE && CONFIG_RX_ON_IDLE)
    {
        pollReq.coordAddress.addrMode = ApiMac_addrType_extended;
        Util_copyExtAddr(&pollReq.coordAddress.addr.extAddr,
                         &devInfoBlock.coordExtAddr);

    }
    else
    {
        pollReq.coordAddress.addrMode = ApiMac_addrType_short;
        pollReq.coordAddress.addr.shortAddr = devInfoBlock.coordShortAddr;
    }
#ifdef POWER_MEAS
    Sensor_pwrMeasStats.pollRequestsSent++;
#endif
    ApiMac_mlmePollReq(&pollReq);
}

/*!
 * @brief       Process coordinator realignment
 */
static void processCoordRealign(void)
{
    uint16_t panID = 0;
    /* read PAN ID from PIB */
    ApiMac_mlmeGetReqUint16(ApiMac_attribute_panId, &panID);

    if(panID == devInfoBlock.panID)
    {
        if(CONFIG_MAC_BEACON_ORDER != JDLLC_BEACON_ORDER_NON_BEACON)
        {
            /* send sync request for beacon enabled device */
            switchState(Jdllc_deviceStates_syncReq);
        }

        /* transition to correct non orphan state */
        if(devInfoBlock.prevJdllcState == Jdllc_states_joined)
        {
            updateState(Jdllc_states_joined);
        }
        else if(devInfoBlock.prevJdllcState == Jdllc_states_rejoined)
        {
            updateState(Jdllc_states_rejoined);
        }
        else if(devInfoBlock.prevJdllcState == Jdllc_states_initRestoring)
        {
            ApiMac_deviceDescriptor_t devInfo;
            Llc_netInfo_t parentNetInfo;
            populateInfo(&devInfo,&parentNetInfo);

            /* device joined */
            if(pJdllcCallbacksCopy && pJdllcCallbacksCopy->pJoinedCb)
            {
                pJdllcCallbacksCopy->pJoinedCb(&devInfo, &parentNetInfo);
            }
            updateState(Jdllc_states_rejoined);
        }

        /* if sleepy device before setting poll timer */
        if((!CONFIG_RX_ON_IDLE))
        {
            /* start polling if parent matches*/
            Ssf_setPollClock(devInfoBlock.pollInterval);
        }

        ApiMac_mlmeSetReqBool(ApiMac_attribute_RxOnWhenIdle, CONFIG_RX_ON_IDLE);
        /* restart sensor reading timer */
        {
            uint32_t randomNum;
            randomNum = ((ApiMac_randomByte() << 16) +
                         (ApiMac_randomByte() << 8) + ApiMac_randomByte());
            randomNum = (randomNum % JDLLC_RESTART_REPORTING_INTERVAL ) +
                        SENSOR_MIN_POLL_TIME;
            Ssf_setReadingClock(randomNum);
        }
        Ssf_setScanBackoffClock(0);
    }
    else
    {
        /* orphan scan */
        switchState(Jdllc_deviceStates_scanOrphan);
        updateState(Jdllc_states_orphan);

        /* orphan scan failed turn off receiver until next scan if RX on idle is false*/
        if ((!CONFIG_FH_ENABLE) && (CONFIG_RX_ON_IDLE == false))
        {
            ApiMac_mlmeSetReqBool(ApiMac_attribute_RxOnWhenIdle, CONFIG_RX_ON_IDLE);
        }

    }
}

/*!
 * @brief       Process poll confirm callback
 *
 * @param       pData - pointer to poll confirm structure
 */
static void pollCnfCb(ApiMac_mlmePollCnf_t *pData)
{
    if((pData->status == ApiMac_status_noData) ||
       (pData->status == ApiMac_status_success))
    {
        if(!CONFIG_FH_ENABLE)
        {
            if(devInfoBlock.currentJdllcState == Jdllc_states_initRestoring)
            {
                ApiMac_deviceDescriptor_t devInfo;
                Llc_netInfo_t parentNetInfo;
                populateInfo(&devInfo,&parentNetInfo);
                ApiMac_mlmeSetReqBool(ApiMac_attribute_RxOnWhenIdle,
                                      CONFIG_RX_ON_IDLE);

                /* device joined */
                if(pJdllcCallbacksCopy && pJdllcCallbacksCopy->pJoinedCb)
                {
                    pJdllcCallbacksCopy->pJoinedCb(&devInfo, &parentNetInfo);
                }

                updateState(Jdllc_states_rejoined);

                if((!CONFIG_RX_ON_IDLE))
                {
                    /* set event for polling if sleepy device*/
                    Util_setEvent(&Jdllc_events, JDLLC_POLL_EVT);
                }
            }
        }
        devInfoBlock.dataFailures = 0;
    }
    else if(pData->status == ApiMac_status_noAck)
    {

            /* track the number of failures  */
            devInfoBlock.dataFailures++;

        if((devInfoBlock.currentJdllcState == Jdllc_states_joined)
           || (devInfoBlock.currentJdllcState == Jdllc_states_rejoined))
        {
            if(!CONFIG_FH_ENABLE)
            {
                /* retry poll with shorter interval in busy network */
                Ssf_setPollClock(JDLLC_RETRY_POLL);
            }
        }

        if(devInfoBlock.dataFailures == CONFIG_MAX_DATA_FAILURES)
        {
            if(CONFIG_MAC_BEACON_ORDER == JDLLC_BEACON_ORDER_NON_BEACON)
            {
                handleMaxDataFail();
            }
        }
        else
        {
            if(devInfoBlock.currentJdllcState == Jdllc_states_initRestoring)
            {
                if((!CONFIG_RX_ON_IDLE) && (!CONFIG_FH_ENABLE))
                {
                    /* set event for polling if sleepy device*/
                    Util_setEvent(&Jdllc_events, JDLLC_POLL_EVT);
                }
            }
        }
    }
    else if(pData->status == ApiMac_status_channelAccessFailure)
    {
        if(!CONFIG_FH_ENABLE)
        {
            /* retry poll with shorter interval in busy network */
            Ssf_setPollClock(JDLLC_RETRY_POLL);
        }
    }

    if(macCallbacksCopy.pPollCnfCb != NULL)
    {
        macCallbacksCopy.pPollCnfCb(pData);
    }
}

/*!
 * @brief       Process Disassociate Indication callback
 *
 * @param       pData - pointer to disassociation indication structure
 */
static void disassocIndCb(ApiMac_mlmeDisassociateInd_t *pData)
{
    if(!CONFIG_RX_ON_IDLE)
    {
        /* stop polling */
        Ssf_setPollClock(0);
    }
    /* enable looking for new parent */
    parentFound = false;

    /* set devInfoBlock back to defaults */
    devInfoBlock.panID = CONFIG_PAN_ID;
    devInfoBlock.channel = JDLLC_INVALID_CHANNEL;
    devInfoBlock.coordShortAddr = 0xFFFF;
    memset(&devInfoBlock.coordExtAddr[0], 0x00, APIMAC_SADDR_EXT_LEN);
    devInfoBlock.devShortAddr = 0xFFFF;
    memset(&devInfoBlock.devExtAddr[0], 0x00, APIMAC_SADDR_EXT_LEN);
    devInfoBlock.beaconOrder = CONFIG_MAC_BEACON_ORDER;
    devInfoBlock.superframeOrder = CONFIG_MAC_SUPERFRAME_ORDER;
    /* Below two commented lines will be set by updateState()*/
    /*devInfoBlock.currentJdllcState = Jdllc_states_initWaiting;*/
    /*devInfoBlock.prevJdllcState = Jdllc_states_initWaiting;*/
    devInfoBlock.currentDevState = Jdllc_deviceStates_scanActive;
    devInfoBlock.prevDevState = Jdllc_deviceStates_scanActive;
    devInfoBlock.dataFailures = 0;
    devInfoBlock.pollInterval = CONFIG_POLLING_INTERVAL;

    /* change state back to initWaiting and print on UART */
    updateState(Jdllc_states_initWaiting);

    /* pass indication to app */
    if(pJdllcCallbacksCopy && pJdllcCallbacksCopy->pDisassocIndCb)
    {
        pJdllcCallbacksCopy->pDisassocIndCb(&pData->deviceAddress,
                                            pData->disassociateReason);
    }

    if(macCallbacksCopy.pDisassociateIndCb != NULL)
    {
        macCallbacksCopy.pDisassociateIndCb(pData);
    }
}

/*!
 * @brief       Send scan request
 *
 * @param       type - type of scan: active ,passive or orphan
 */
static void sendScanReq(ApiMac_scantype_t type)
{
    ApiMac_mlmeScanReq_t scanReq;
    /* set common parameters for all scans */
    memset(&scanReq, 0, sizeof(ApiMac_mlmeScanReq_t));
    /* set scan channels from channel mask*/
    memcpy(scanReq.scanChannels, defaultChannelMask,
           APIMAC_154G_CHANNEL_BITMAP_SIZ);
    scanReq.scanType = type;
    if(CONFIG_MAC_BEACON_ORDER == JDLLC_BEACON_ORDER_NON_BEACON)
    {
        scanReq.scanDuration = CONFIG_SCAN_DURATION;
    }
    else
    {
        scanReq.scanDuration = CONFIG_MAC_BEACON_ORDER;
    }
    scanReq.maxResults = 0;/* Expecting beacon notifications */
    scanReq.permitJoining = false;
    scanReq.linkQuality = CONFIG_LINKQUALITY;
    scanReq.percentFilter = CONFIG_PERCENTFILTER;
    scanReq.channelPage = CONFIG_CHANNEL_PAGE;
    scanReq.phyID = CONFIG_PHY_ID;
    /* using no security for scan request command */
    memset(&scanReq.sec, 0, sizeof(ApiMac_sec_t));
    /* send scan Req */
    ApiMac_mlmeScanReq(&scanReq);
}

/*!
 * @brief       Send Async request command
 *
 * @param       frameType - type of async frame to be sent
 */
static void sendAsyncReq(ApiMac_wisunAsyncFrame_t frameType)
{
    ApiMac_mlmeWSAsyncReq_t asyncReq;
    uint8_t sizeOfChannelMask;
    uint8_t asyncChannelMask[] = FH_ASYNC_CHANNEL_MASK;

    /* set of Exclude Channels */
    sizeOfChannelMask = sizeof(asyncChannelMask)/sizeof(uint8_t);
    if(sizeOfChannelMask > APIMAC_154G_CHANNEL_BITMAP_SIZ)
    {
        sizeOfChannelMask = APIMAC_154G_CHANNEL_BITMAP_SIZ;
    }
    memset(asyncReq.channels, 0, (APIMAC_154G_CHANNEL_BITMAP_SIZ));
    asyncReq.operation = ApiMac_wisunAsycnOperation_start;
    memcpy(asyncReq.channels, asyncChannelMask, sizeOfChannelMask);
    memset(asyncReq.sec.keySource, 0, APIMAC_KEY_SOURCE_MAX_LEN);

    /* send PAS or PCS according to frame type */
    if(frameType == ApiMac_wisunAsyncFrame_advertisementSolicit)
    {
        /* Fill in the information for async request */
        asyncReq.frameType = ApiMac_wisunAsyncFrame_advertisementSolicit;
        /* no security for PAS */
        asyncReq.sec.securityLevel = ApiMac_secLevel_none;
        Jdllc_statistics.fhNumPASolicitSent++;
        /* Change fixed channel of operation to see if packet was
           Async were failing due to interference on rx channel
           every FH_MAX_ASYN_ON_GIVEN_CHANNEL attempts
         */
        if((Jdllc_statistics.fhNumPASolicitSent %
            FH_MAX_ASYN_ON_GIVEN_CHANNEL) == 0)
        {
            /* set fixed channel in FH PIB */
            ApiMac_mlmeSetFhReqUint16(ApiMac_FHAttribute_unicastFixedChannel,
                                      (uint16_t) getFHSleepNodeHopChannel());
        }
    }
    else if(frameType == ApiMac_wisunAsyncFrame_configSolicit)
    {
        /* Fill in the information for async request */
        asyncReq.frameType = ApiMac_wisunAsyncFrame_configSolicit;
        /* no security for PAS */
        asyncReq.sec.securityLevel = ApiMac_secLevel_none;
        Jdllc_statistics.fhNumPANConfigSolicitsSent++;
        /* Change fixed channel of operation to see if packet was
           Async were failing due to interference on rx channel
           every FH_MAX_ASYN_ON_GIVEN_CHANNEL attempts
         */
        if((Jdllc_statistics.fhNumPANConfigSolicitsSent %
            FH_MAX_ASYN_ON_GIVEN_CHANNEL) == 0)
        {
            /* set fixed channel in FH PIB */
            ApiMac_mlmeSetFhReqUint16(ApiMac_FHAttribute_unicastFixedChannel,
                                      (uint16_t) getFHSleepNodeHopChannel());
        }
    }

    ApiMac_mlmeWSAsyncReq(&asyncReq);
}

/*!
 * @brief       Send Async request command
 *
 * @param       pFhFrameVar - pointer to FH frame variable
 *
 * @param       frameType - type of FH frame
 *
 * @param       trickleDuration - duration of trickle timer
 */
static void processFHEvent(uint8_t *pFhFrameVar,
                           ApiMac_wisunAsyncFrame_t frameType,
                           uint16_t trickleDuration)
{
    if(*pFhFrameVar == 0)
    {
        /* Process LLC Event */
        sendAsyncReq(frameType);
    }

    *pFhFrameVar = 0;
    Ssf_setTrickleClock(trickleDuration, frameType);
}

/*!
 * @brief       Populate information for end device and its parent
 *
 * @param       pDevInfo - pointer to device descriptor structure
 *
 * @param       pParentNetInfo - pointer to network information structure
 */
static void populateInfo(ApiMac_deviceDescriptor_t *pDevInfo,
                         Llc_netInfo_t *pParentNetInfo)
{
    Util_copyExtAddr(&pDevInfo->extAddress, &devInfoBlock.devExtAddr);
    pDevInfo->panID = devInfoBlock.panID;
    pDevInfo->shortAddress = devInfoBlock.devShortAddr;
    Util_copyExtAddr(&pParentNetInfo->devInfo.extAddress,
                     &devInfoBlock.coordExtAddr);
    pParentNetInfo->channel = devInfoBlock.channel;
    pParentNetInfo->fh = CONFIG_FH_ENABLE;
    pParentNetInfo->devInfo.panID = devInfoBlock.panID;
    pParentNetInfo->devInfo.shortAddress = devInfoBlock.coordShortAddr;
}

/*!
 * @brief       Handle maximum ack failures for non beacon and FH mode
 */
static void handleMaxDataFail(void)
{
    if(!CONFIG_RX_ON_IDLE)
    {
        /* stop polling */
        Ssf_setPollClock(0);
    }

    ApiMac_mlmeSetReqBool(ApiMac_attribute_RxOnWhenIdle, true);
    /* Initialize counter for re-join delay calculation */
    interimDelayTicks = Clock_getTicks();
    /* non beacon network or fh mode - update stats */
    Sensor_msgStats.syncLossIndications++;

    if(CONFIG_FH_ENABLE)
    {
        updateState(Jdllc_states_orphan);
        parentFound = false;
        /* start trickle timer for PCS */
        Ssf_setTrickleClock(fhPanConfigInterval,
                            ApiMac_wisunAsyncFrame_configSolicit);
    }
    else
    {
        /* start orphan scan */
        switchState(Jdllc_deviceStates_scanOrphan);
        updateState(Jdllc_states_orphan);
    }

    devInfoBlock.dataFailures = 0;
}
