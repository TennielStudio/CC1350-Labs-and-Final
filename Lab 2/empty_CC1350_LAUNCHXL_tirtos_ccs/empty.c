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
#include <ti/drivers/ADC.h>
#include <ti/display/Display.h>

/* Board Header file */
#include "Board.h"


// Global Variables
uint16_t adcValue = 0;
uint16_t threshold = 100;
uint16_t trigger = 0;

void gpioButtonFxn0(uint_least8_t index)
{
    /* Clear the GPIO interrupt and decrement threshold */
    if(threshold < 250){ // Ensure threshold doesn't go below zero
        threshold = 0;
    } else {
        threshold -= 250; // decrement by 250
    }
}


void gpioButtonFxn1(uint_least8_t index)
{
    /* Clear the GPIO interrupt and increment threshold */
    if(threshold > 4096){ // Ensure threshold doesn't go above max ADC range
        threshold = 4096;
    } else {
        threshold += 250; // increment by 250
    }
}

/*
 *  ======== mainThread ========
 */
void *mainThread(void *arg0)
{
    /* 1 second delay */
    uint32_t time = 100000;

    /* Call driver init functions */
    GPIO_init();
    ADC_init();
    // I2C_init();
    // SPI_init();
    // UART_init();
    // Watchdog_init();

    /* Configure the LED pin */
    GPIO_setConfig(Board_GPIO_LED0, GPIO_CFG_OUT_STD | GPIO_CFG_OUT_LOW);
    GPIO_setConfig(Board_GPIO_BUTTON0, GPIO_CFG_IN_PU | GPIO_CFG_IN_INT_FALLING);
    GPIO_setConfig(Board_GPIO_BUTTON1, GPIO_CFG_IN_PU | GPIO_CFG_IN_INT_FALLING);

    /* Turn on user LED */
    GPIO_write(Board_GPIO_LED0, Board_GPIO_LED_ON);

    // ADC
    ADC_Handle adc;
    ADC_Params params;
    ADC_Params_init(&params);
    adc = ADC_open(Board_ADC0, &params);
    if (adc == NULL) {
        // ADC_open() failed
        while (1);
    }

    // UART
    Display_Handle    displayHandle;
    Display_Params    displayParams;
    Display_Params_init(&displayParams);
    displayHandle = Display_open(Display_Type_UART, NULL);

    /* install Button callback */
    GPIO_setCallback(Board_GPIO_BUTTON0, gpioButtonFxn0);
    GPIO_setCallback(Board_GPIO_BUTTON1, gpioButtonFxn1);

    /* Enable interrupts */
    GPIO_enableInt(Board_GPIO_BUTTON0);
    GPIO_enableInt(Board_GPIO_BUTTON1);

    while (1) {
        int_fast16_t res;
        res = ADC_convert(adc, &adcValue);
        Display_printf(displayHandle, 1, 0, "ADC Reading %d", adcValue);
        Display_printf(displayHandle, 1, 0, "Threshold %d", threshold);

        if (res == ADC_STATUS_SUCCESS) {
            if(adcValue >= threshold){  // arbitrary threshold
                GPIO_write(Board_GPIO_LED0, Board_GPIO_LED_ON);
                trigger = 1;
            } else{
                GPIO_write(Board_GPIO_LED0, Board_GPIO_LED_OFF);
                trigger = 0;
            }
        }

        usleep(time);
    }
}

