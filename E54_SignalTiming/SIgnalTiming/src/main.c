///*******************************************************************************
//  Main Source File
//
//  Company:
//    Microchip Technology Inc.
//
//  File Name:
//    main.c
//
//  Summary:
//    This file contains the "main" function for a project.
//
//  Description:
//    This file contains the "main" function for a project.  The
//    "main" function calls the "SYS_Initialize" function to initialize the state
//    machines of all modules in the system
// *******************************************************************************/
//
//// *****************************************************************************
//// *****************************************************************************
//// Section: Included Files
//// *****************************************************************************
//// *****************************************************************************
//
//#include <stddef.h>                     // Defines NULL
//#include <stdbool.h>                    // Defines true
//#include <stdlib.h>                     // Defines EXIT_FAILURE
//#include "definitions.h"                // SYS function prototypes
//#include "peripheral/port/plib_port.h"
//#include "peripheral/tc/plib_tc0.h"
//#include "peripheral/tc/plib_tc2.h"
//#include <stddef.h>
//
//
//// *****************************************************************************
//// *****************************************************************************
//// Section: Main Entry Point
//// *****************************************************************************
//// *****************************************************************************
//
//volatile bool is1msElapsed = false;
//volatile uint16_t tc2Count = 0;
//
//void TC0_Callback (TC_TIMER_STATUS status, uintptr_t context){ 
//  TC0_TimerStop();
//  tc2Count = TC2_Timer16bitCounterGet();
//  TC2_TimerStop(); 
//  is1msElapsed = true;
//}
//
//void GCLK3_Initialize_Custom(void){
//
//    GCLK_REGS->GCLK_GENCTRL[3] = GCLK_GENCTRL_DIV(1U) | GCLK_GENCTRL_SRC(2U) | GCLK_GENCTRL_GENEN_Msk;
//
//    while((GCLK_REGS->GCLK_SYNCBUSY & GCLK_SYNCBUSY_GENCTRL_GCLK3) == GCLK_SYNCBUSY_GENCTRL_GCLK3)
//    {
//        /* wait for the Generator 3 synchronization */
//    }
//
//
//    GCLK_REGS->GCLK_PCHCTRL[26] = GCLK_PCHCTRL_GEN(0x3U)  | GCLK_PCHCTRL_CHEN_Msk;
//
//    while ((GCLK_REGS->GCLK_PCHCTRL[26] & GCLK_PCHCTRL_CHEN_Msk) != GCLK_PCHCTRL_CHEN_Msk)
//    {
//        /* Wait for synchronization */
//    }
//}
//
//int main ( void )
//{
//    /* Initialize all modules */
//    SYS_Initialize ( NULL );
//    printf("\r\n\r\nBooting Up....\r\n");
//
//    TC0_TimerCallbackRegister(TC0_Callback, NULL);
//    GCLK3_Initialize_Custom();
//    TC2_TimerInitialize();
//    
//    while ( true )
//    {
//        /* Maintain state machines of all polled MPLAB Harmony modules. */
//        SYS_Tasks ( );
//        is1msElapsed = false;
//        TC0_TimerStart();
//        TC2_TimerStart();
//
//        while(!is1msElapsed); 
//        
//
//        printf("Measured Frequency: %0.3f MHz\r\n", (float) tc2Count / 1000.0);
//
//    }
//
//    /* Execution should not come here during normal operation */
//
//    return ( EXIT_FAILURE );
//}
//
//
///*******************************************************************************
// End of File
//*/
//
/*******************************************************************************
  File Name:    main.c
  Description:  Frequency counter using TC0 as 1ms gate and TC2 as pulse counter.
                GCLK3 is sourced from GCLKIN (external pin driven by TC4 output).
                GCLK3 and TC2 must be initialized AFTER TC4 starts, because the
                GCLK sync busy-loop requires a valid clock on the GCLKIN pin.
*******************************************************************************/

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include "definitions.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define GATE_PERIOD_US          1000U       // TC0 gate window in microseconds
#define COUNTS_PER_MHZ          1000.0f     // tc2Count/1ms gate ? MHz

// ---------------------------------------------------------------------------
// Globals shared between ISR and main
// ---------------------------------------------------------------------------
static volatile bool     s_gateComplete = false;
static volatile uint16_t s_pulseCount   = 0U;

// ---------------------------------------------------------------------------
// TC0 callback ? fires after 1 ms gate window
// ---------------------------------------------------------------------------
static void TC0_Callback(TC_TIMER_STATUS status, uintptr_t context)
{
    (void)status;
    (void)context;

    TC0_TimerStop();
    s_pulseCount   = TC2_Timer16bitCounterGet();
    TC2_TimerStop();
    s_gateComplete = true;
}

// ---------------------------------------------------------------------------
// GCLK3 custom init ? routes GCLKIN (SRC=2) ? TC2/TC3 peripheral channel
// NOTE: Must be called AFTER the external clock source is running,
//       otherwise the sync busy-loop will never clear (infinite hang).
// ---------------------------------------------------------------------------
static void GCLK3_Initialize_Custom(void)
{
    // Configure Generator 3: source = GCLKIN (pin), div = 1
    GCLK_REGS->GCLK_GENCTRL[3] = GCLK_GENCTRL_DIV(1U)
                                 | GCLK_GENCTRL_SRC(2U)
                                 | GCLK_GENCTRL_GENEN_Msk;

    while ((GCLK_REGS->GCLK_SYNCBUSY & GCLK_SYNCBUSY_GENCTRL_GCLK3)
            == GCLK_SYNCBUSY_GENCTRL_GCLK3)
    {
        /* Wait for Generator 3 sync ? requires GCLKIN pin to be driven */
    }

    // Connect Generator 3 to TC2/TC3 peripheral channel (PCHCTRL[26])
    GCLK_REGS->GCLK_PCHCTRL[26] = GCLK_PCHCTRL_GEN(0x3U)
                                  | GCLK_PCHCTRL_CHEN_Msk;

    while ((GCLK_REGS->GCLK_PCHCTRL[26] & GCLK_PCHCTRL_CHEN_Msk)
            != GCLK_PCHCTRL_CHEN_Msk)
    {
        /* Wait for peripheral channel sync */
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(void)
{
    SYS_Initialize(NULL);
    printf("\r\n\r\nFrequency Counter Starting...\r\n");

    // Register TC0 gate callback
    TC0_TimerCallbackRegister(TC0_Callback, NULL);

    // Now safe to init GCLK3 (clock source is present) and TC2
    GCLK3_Initialize_Custom();
    TC2_TimerInitialize();

    while (true)
    {
        SYS_Tasks();

        // Start gate measurement
        s_gateComplete = false;
        TC0_TimerStart();
        TC2_TimerStart();

        // Wait for 1ms gate to complete
        // TODO: replace with an RTOS delay or state machine for production
        while (!s_gateComplete)
        {
            SYS_Tasks();
        }

        // Safe snapshot ? TC2 already stopped in ISR
        uint16_t count = s_pulseCount;
        printf("Measured Frequency: %.3f MHz\r\n", (float)count / COUNTS_PER_MHZ);
    }

    return EXIT_FAILURE;
}