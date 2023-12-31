/* libs included */
#include "lpc17xx.h"
#include "lpc17xx_adc.h"
#include "lpc17xx_pinsel.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_uart.h"


/* func declarations */
void configPRIO(void);
void configPINS(void);
void configADC(void);
void configTMR(void);
void configUART(void);
void switchActiveDisplay(void);
void setLED(uint8_t value);
void setDisplayValue(uint8_t display);
void loadSevenSegValue(uint8_t value, uint8_t display);


/* global variables declaration */
uint16_t tmr_inter_count = 0;
uint8_t uart_inter_count = 0;
uint8_t enabled_seven_seg = 0;

uint16_t adc_value;

// all displays 0 by default
// displays ordered as 1, 2, 3
uint32_t port_0_on_vals[3] = {50823168, 50823168, 50823168};
uint32_t port_0_off_vals[3] = {67108864, 67108864, 67108864};


/* func definitions */
int main(void) {
  configPRIO();
  configPINS();
  configADC();
  configUART();
  configTMR();

  while (1) {}

  return 0;
}


void configPRIO(void) {
  NVIC_SetPriority(UART0_IRQn, 0);
  NVIC_SetPriority(TIMER0_IRQn, 1);
  NVIC_SetPriority(ADC_IRQn, 2);
}


void configPINS(void) {
  PINSEL_CFG_Type cfg;
  // lone from port 1 first
  cfg.Portnum = PINSEL_PORT_1;
  cfg.Funcnum = PINSEL_FUNC_0;
  cfg.Pinmode = PINSEL_PINMODE_PULLUP;
  cfg.OpenDrain = PINSEL_PINMODE_NORMAL;

  cfg.Pinnum = PINSEL_PIN_30;
  PINSEL_ConfigPin(&cfg);

  // all from port 0
  cfg.Portnum = PINSEL_PORT_0;
  uint8_t gpioPins[13] = {0, 1, 6, 7, 8, 9, 15, 16, 17, 18, 24, 25, 26};

  for (int i = 0; i <= 13; i++) {
    cfg.Pinnum = gpioPins[i];
    PINSEL_ConfigPin(&cfg);
  }

  // UART pins
  cfg.Funcnum = PINSEL_FUNC_1;
  cfg.Pinnum = PINSEL_PIN_2;
  PINSEL_ConfigPin(&cfg);
  cfg.Pinnum = PINSEL_PIN_3;
  PINSEL_ConfigPin(&cfg);

  // pins direction setting
  GPIO_SetDir(1, 1 << 30, 1);
  GPIO_SetDir(0, 0b111010001111000001111000011, 1);
}


void configADC(void) {
  ADC_Init(LPC_ADC, 200000);

  // set pin for input
  PINSEL_CFG_Type pin_0_23;
  pin_0_23.Portnum = PINSEL_PORT_0;
  pin_0_23.Pinnum = PINSEL_PIN_23;
  pin_0_23.Funcnum = PINSEL_FUNC_1;
  pin_0_23.Pinmode = PINSEL_PINMODE_TRISTATE;
  pin_0_23.OpenDrain = PINSEL_PINMODE_NORMAL;
  PINSEL_ConfigPin(&pin_0_23);

  ADC_ChannelCmd(LPC_ADC, 0, ENABLE);

  NVIC_EnableIRQ(ADC_IRQn);
}


void configTMR(void) {
  TIM_TIMERCFG_Type tmrCfg;
  TIM_MATCHCFG_Type mchCfg;

  tmrCfg.PrescaleOption = TIM_PRESCALE_TICKVAL;
  tmrCfg.PrescaleValue = 1;

  mchCfg.MatchChannel = 0;
  mchCfg.IntOnMatch = ENABLE;
  mchCfg.ResetOnMatch = ENABLE;
  mchCfg.StopOnMatch = DISABLE;
  mchCfg.ExtMatchOutputType = TIM_EXTMATCH_NOTHING;
  mchCfg.MatchValue = 165000;

  TIM_Init(LPC_TIM0, TIM_TIMER_MODE, &tmrCfg);
  TIM_ConfigMatch(LPC_TIM0, &mchCfg);
  TIM_Cmd(LPC_TIM0, ENABLE);

  NVIC_EnableIRQ(TIMER0_IRQn);
}


void configUART(void) {
  UART_CFG_Type uartCfg;
  UART_ConfigStructInit(&uartCfg);
  uartCfg.Baud_rate = 300;
  UART_Init((LPC_UART_TypeDef *)LPC_UART0, &uartCfg);

  UART_FIFO_CFG_Type fifoCfg;
  UART_FIFOConfigStructInit(&fifoCfg);
  UART_FIFOConfig((LPC_UART_TypeDef *)LPC_UART0, &fifoCfg);

  UART_TxCmd((LPC_UART_TypeDef *)LPC_UART0, ENABLE);
  UART_IntConfig((LPC_UART_TypeDef *)LPC_UART0, UART_INTCFG_RBR, ENABLE);

  NVIC_EnableIRQ(UART0_IRQn);
}


void TIMER0_IRQHandler(void) {
  switchActiveDisplay();

  tmr_inter_count++;

  if (tmr_inter_count == 64) {
    ADC_StartCmd(LPC_ADC, ADC_START_NOW);
  }

  if (tmr_inter_count == 128) {
    uint8_t split_adc_value[2] = {(uint8_t)(adc_value), (uint8_t)(adc_value >> 8)};
    UART_Send((LPC_UART_TypeDef *)LPC_UART0, split_adc_value, 2, BLOCKING);
    tmr_inter_count = 0;
  }

  TIM_ClearIntPending(LPC_TIM0, TIM_MR0_INT);
}


void ADC_IRQHandler(void) {
  adc_value = ((ADC_GlobalGetData(LPC_ADC) >> 4) & 0xFFF);
}


void UART0_IRQHandler(void) {
  uint8_t value = UART_ReceiveByte((LPC_UART_TypeDef *)LPC_UART0);

  if (value == 255) {
    uart_inter_count = 0;
    return;
  }

  switch (uart_inter_count) {
    case 0:
      setLED(value);
      break;
    case 1:
      loadSevenSegValue(value, 0);
      break;
    case 2:
      loadSevenSegValue(value, 1);
      break;
    default:
      // 3
      loadSevenSegValue(value, 2);
  }

  uart_inter_count++;
}


void switchActiveDisplay(void) {
  switch(enabled_seven_seg) {
    case 3: // resets counter and executes case 0
      enabled_seven_seg = 0;
    case 0: // enables second display
      LPC_GPIO0->FIOCLR = (1 << 9);
      LPC_GPIO0->FIOSET = (1 << 8);
      LPC_GPIO0->FIOCLR = (1 << 7);
      LPC_GPIO1->FIOCLR = (1 << 30); // enables dot
      setDisplayValue(1);
      break;
    case 1: // enables third display
      LPC_GPIO0->FIOCLR = (1 << 9);
      LPC_GPIO0->FIOCLR = (1 << 8);
      LPC_GPIO0->FIOSET = (1 << 7);
      LPC_GPIO1->FIOSET = (1 << 30); // disables dot
      setDisplayValue(2);
      break;
    default: // enables first display
      // 2
      LPC_GPIO0->FIOSET = (1 << 9);
      LPC_GPIO0->FIOCLR = (1 << 8);
      LPC_GPIO0->FIOCLR = (1 << 7);
      setDisplayValue(0);
  }

  enabled_seven_seg++;
}


void setDisplayValue(uint8_t display) {
  LPC_GPIO0->FIOCLR = port_0_on_vals[display];
  LPC_GPIO0->FIOSET = port_0_off_vals[display];
}


void setLED(uint8_t value) {
  switch (value) {
    case 1:
      LPC_GPIO0->FIOSET = (1 << 1);
      LPC_GPIO0->FIOCLR = (1 << 0);
      LPC_GPIO0->FIOCLR = (1 << 6);
      break;
    case 2:
      LPC_GPIO0->FIOCLR = (1 << 1);
      LPC_GPIO0->FIOSET = (1 << 0);
      LPC_GPIO0->FIOCLR = (1 << 6);
      break;
    default:
      // 4
      LPC_GPIO0->FIOCLR = (1 << 1);
      LPC_GPIO0->FIOCLR = (1 << 0);
      LPC_GPIO0->FIOSET = (1 << 6);
  }
}


// segs enabled by low
void loadSevenSegValue(uint8_t value, uint8_t display) {
  switch (value) {
    case 0:
      port_0_on_vals[display] = 50823168;  // enables segs A,B,C,D,E,F
      port_0_off_vals[display] = 67108864; // disables segs G
      break;
    case 1:
      port_0_on_vals[display] = 163840;     // enables segs B,C
      port_0_off_vals[display] = 117768192; // disables segs A,D,E,F,G
      break;
    case 2:
      port_0_on_vals[display] = 84344832;  // enables segs A,B,D,E,G
      port_0_off_vals[display] = 33587200; // disables segs C,F
      break;
    case 3:
      port_0_on_vals[display] = 67600384;  // enables segs A,B,C,D,G
      port_0_off_vals[display] = 50331648; // disables segs E,F
      break;
    case 4:
      port_0_on_vals[display] = 100827136; // enables segs B,C,F,G
      port_0_off_vals[display] = 17104896; // disables segs A,D,E
      break;
    case 5:
      port_0_on_vals[display] = 101023744; // enables segs A,C,D,F,G
      port_0_off_vals[display] = 16908288; // disables segs B,E
      break;
    case 6:
      port_0_on_vals[display] = 117800960;  // enables segs A,C,D,E,F,G
      port_0_off_vals[display] = 131072; // disables segs B
      break;
    case 7:
      port_0_on_vals[display] = 425984;     // enables segs A,B,C
      port_0_off_vals[display] = 117506048; // disables segs D,E,F,G
      break;
    case 8:
      port_0_on_vals[display] = 117932032; // enables segs A,B,C,D,E,F,G
      port_0_off_vals[display] = 0;        // disables no segs
      break;
    default:
      // 9
      port_0_on_vals[display] = 101154816; // enables segs A,B,C,D,F,G
      port_0_off_vals[display] = 16777216; // disables segs E
  }
}
