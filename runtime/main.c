#include "lpc_chip/board.h"

/*****************************************************************************
 * Private types/enumerations/variables
 ****************************************************************************/
#define TICKRATE_HZ 100
#define EV_TICK_CT_DISPLAY 0x01

#define LED_RED 0
#define LED_GREEN 1
#define LED_BLUE 2

/*****************************************************************************
 * Public types/enumerations/variables
 ****************************************************************************/

/*****************************************************************************
 * Private functions
 ****************************************************************************/
static void ansi_clr_screen(void) {
  Board_UARTPutSTR("\x1B[2J");    // clear screen
  Board_UARTPutSTR("\x1B[0m");    // color mode reset
  Board_UARTPutSTR("\x1B[1;1H");  // set position to 1,1
}

static void Board_LED_clear(void) {
  Board_LED_Set(LED_RED, false);
  Board_LED_Set(LED_GREEN, false);
  Board_LED_Set(LED_BLUE, false);
}

/*****************************************************************************
 * Public functions
 ****************************************************************************/

/**
 * @brief	Handle interrupt from SysTick timer
 * @return	Nothing
 */
static uint32_t sys_event = 0;
static uint32_t tick_ct = 0;
static char out_str[16];

void SysTick_Handler(void) {
  tick_ct += 1;
  if ((tick_ct % 50) == 0) Board_LED_Toggle(LED_RED);
  if ((tick_ct % 100) == 0) Board_LED_Toggle(LED_GREEN);
  if ((tick_ct % 200) == 0) Board_LED_Toggle(LED_BLUE);

  if ((tick_ct % 100) == 0) sys_event |= EV_TICK_CT_DISPLAY;
}

/**
 * @brief	main routine for blinky example
 * @return	Function should not exit.
 */
int main(void) {
  SystemCoreClockUpdate();
  Board_Init();

  Board_LED_clear();
  ansi_clr_screen();
  Board_UARTPutSTR("Hello,UART demo:\n");
  Board_UARTPutSTR("build datetime: " __DATE__ " " __TIME__ "\n");

  /* Enable SysTick Timer */
  SysTick_Config(SystemCoreClock / TICKRATE_HZ);

  /* Loop forever */
  while (1) {
    __WFI();
    if (sys_event & EV_TICK_CT_DISPLAY) {
      sys_event &= ~EV_TICK_CT_DISPLAY;
      Board_UARTPutSTR("system tick: ");
      Board_itoa(tick_ct, out_str, 10);
      Board_UARTPutSTR(out_str);
      Board_UARTPutChar(0x0d);
    }
  }
}
