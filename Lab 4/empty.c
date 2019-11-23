/*
 * Copyright (c) 2015-2019, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *  ======== empty.c ========
 */

/* For usleep() */
#include <unistd.h>
#include <stdint.h>
#include <stddef.h>

/* Driver Header files */
#include <ti/drivers/GPIO.h>
// #include <ti/drivers/I2C.h>
// #include <ti/drivers/SPI.h>
// #include <ti/drivers/UART.h>
// #include <ti/drivers/Watchdog.h>

/* Board Header file */
#include "Board.h"
#include "scif.h"

#define BV(x) (1 << (x))

void processTaskAlert(void) {
    //clear the alert interrupt source
    scifClearAlertIntSource();

    //do sc task processing here
    uint8_t high = scifTaskData.adcLevelTrigger.state.high; //fetch "state.high" from sc
    GPIO_write(Board_GPIO_RLED, high); //set red led state equal to the state.high variable

    //acknowledge the ALERT event
    scifAckAlertEvents();
}//processTaskAlert

void scCtrlReadyCallback(void) {

} //scCtrlReadyCallback

void scTaskAlertCallback(void) {

} //scTaskAlertCallback

/*
 *  ======== mainThread ========
 */
void *tirtosScThread(void *arg0)
{
    /* 1 second delay */
     // uint32_t time = 1;
      /* Call driver init functions */
    //Initialize the Sensor Controller
    scifOsalInit();
    scifOsalRegisterCtrlReadyCallback(scCtrlReadyCallback);
    scifOsalRegisterTaskAlertCallback(scTaskAlertCallback);
    scifInit(&scifDriverSetup);

    // Set the Sensor Controller task tick interval to 1 second
    uint32_t rtc_Hz = 1;  // 1Hz RTC
    scifStartRtcTicksNow(0x00010000/ rtc_Hz);

    //configure sensor controller tasks
    scifTaskData.adcLevelTrigger.cfg.threshold = 600;

    //start sensor controller task
    scifStartTasksNbl(BV(SCIF_ADC_LEVEL_TRIGGER_TASK_ID));
      GPIO_init();
      // I2C_init();
      // SPI_init();
      // UART_init();
      // Watchdog_init();

      /* Configure the LED pin */
      GPIO_setConfig(Board_GPIO_LED0, GPIO_CFG_OUT_STD | GPIO_CFG_OUT_LOW);

      /* Turn on user LED */
      GPIO_write(Board_GPIO_LED0, Board_GPIO_LED_ON);

      while (1) {
         // sleep(time);
         // GPIO_toggle(Board_GPIO_LED0);
      }

}
