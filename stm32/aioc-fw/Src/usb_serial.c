#include "usb_serial.h"
#include "stm32f3xx_hal.h"
#include <assert.h>
#include "tusb.h"
#include "led.h"
#include "usb_descriptors.h"

void USB_SERIAL_UART_IRQ(void)
{
    uint32_t ISR = USB_SERIAL_UART->ISR;

    if (ISR & USART_ISR_TXE) {
        /* TX register is empty, load up another character */
        if (tud_cdc_n_available(ITF_NUM_CDC_0) > 0) {
            /* Write char from fifo */
            int32_t c = tud_cdc_n_read_char(ITF_NUM_CDC_0);
            assert(c != -1);
            USB_SERIAL_UART->TDR = (uint8_t) c;
        } else {
            /* No char left in fifo. Disable TX-empty interrupt */
            __disable_irq();
            USB_SERIAL_UART->CR1 &= (uint32_t) ~USART_CR1_TXEIE;
            __enable_irq();
        }
    }

    if (ISR & USART_ISR_RXNE) {
        /* RX register is not empty, get character and put into USB send buffer */
        if (tud_cdc_n_write_available(ITF_NUM_CDC_0) > 0) {
            uint8_t c = USB_SERIAL_UART->RDR;
            tud_cdc_n_write(ITF_NUM_CDC_0, &c, 1);
        } else {
            /* No space in fifo currently. Pause this interrupt and re-enable later */
            __disable_irq();
            USB_SERIAL_UART->CR1 &= (uint32_t) ~USART_CR1_RXNEIE;
            __enable_irq();
        }
    }

    if (ISR & USART_ISR_RTOF) {
        USB_SERIAL_UART->ICR = USART_ICR_RTOCF;
        /* Receiver timeout. Flush data via USB. */
        tud_cdc_n_write_flush(ITF_NUM_CDC_0);
    }

    if (ISR & USART_ISR_ORE) {
        /* Overflow error */
        USB_SERIAL_UART->ICR = USART_ICR_ORECF;
        assert(0);
    }
}

// Invoked when CDC interface received data from host
void tud_cdc_rx_cb(uint8_t itf)
{
    if (itf == ITF_NUM_CDC_0) {
        /* This enables TX-empty interrupt, which handles writing UART data */
        __disable_irq();
        USB_SERIAL_UART->CR1 |= USART_CR1_TXEIE;
        __enable_irq();
    }
}

// Invoked when space becomes available in TX buffer
void tud_cdc_tx_complete_cb(uint8_t itf)
{
    if (itf == ITF_NUM_CDC_0) {
        /* Re-enable UART RX-nonempty interrupt to handle reading UART data */
        __disable_irq();
        USB_SERIAL_UART->CR1 |= USART_CR1_RXNEIE;
        __enable_irq();
    }
}

// Invoked when line coding is change via SET_LINE_CODING
void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* p_line_coding)
{
    if (itf == ITF_NUM_CDC_0) {
        /* Disable IRQs and UART */
        __disable_irq();
        USB_SERIAL_UART->CR1 &= (uint32_t) ~USART_CR1_UE;

        /* Calculate new baudrate */
        USB_SERIAL_UART->BRR = (HAL_RCCEx_GetPeriphCLKFreq(USB_SERIAL_UART_PERIPHCLK) + p_line_coding->bit_rate/2) / p_line_coding->bit_rate;

        if (p_line_coding->data_bits == 8) {
        } else {
            /* Support only 8 bit character size */
            assert(0);
        }

        if (p_line_coding->parity == 0) {
            /* No parity */
            USB_SERIAL_UART->CR1 = (USB_SERIAL_UART->CR1 & (uint32_t) ~(USART_CR1_PCE | USART_CR1_PS | USART_CR1_M | USART_CR1_M0))
                    | UART_PARITY_NONE;
        } else if (p_line_coding->parity == 1) {
            /* Odd parity */
            USB_SERIAL_UART->CR1 = (USB_SERIAL_UART->CR1 & (uint32_t) ~(USART_CR1_PCE | USART_CR1_PS | USART_CR1_M | USART_CR1_M0))
                    | UART_PARITY_ODD | UART_WORDLENGTH_9B;
        } else if (p_line_coding->parity == 2) {
            /* Even parity */
            USB_SERIAL_UART->CR1 = (USB_SERIAL_UART->CR1 & (uint32_t) ~(USART_CR1_PCE | USART_CR1_PS | USART_CR1_M | USART_CR1_M0))
                    | UART_PARITY_EVEN | UART_WORDLENGTH_9B;
        } else {
            /* Other parity modes are not supported */
            assert(0);
        }

        if (p_line_coding->stop_bits == 0) {
            /* 1 stop bit */
            USB_SERIAL_UART->CR2 = (USB_SERIAL_UART->CR2 & (uint32_t) ~USART_CR2_STOP) | UART_STOPBITS_1;
        } else if (p_line_coding->stop_bits == 1) {
            /* 1.5 stop bit */
            USB_SERIAL_UART->CR2 = (USB_SERIAL_UART->CR2 & (uint32_t) ~USART_CR2_STOP) | UART_STOPBITS_1_5;
        } else if (p_line_coding->stop_bits == 2) {
            /* 2 stop bit */
            USB_SERIAL_UART->CR2 = (USB_SERIAL_UART->CR2 & (uint32_t) ~USART_CR2_STOP) | UART_STOPBITS_2;
        } else {
            /* Other stop bits unsupported */
            assert(0);
        }

        /* Re-enable UUART and IRQs */
        USB_SERIAL_UART->CR1 |= USART_CR1_UE;
        __enable_irq();
    }
}

// Invoked when cdc when line state changed e.g connected/disconnected
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    if (dtr & !rts) {
        /* PTT1 */
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);
        LED_SET2(LED_FULL_LEVEL);
    } else {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);
        LED_SET2(LED_IDLE_LEVEL);
    }

    if (!dtr & rts) {
        /* PTT2 */
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_SET);
        LED_SET1(LED_FULL_LEVEL);
    } else {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_RESET);
        LED_SET1(LED_IDLE_LEVEL);
    }
}

void USB_SerialInit(void)
{
    /* Set up GPIO */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef SerialGpio;
    SerialGpio.Pin = (GPIO_PIN_9 | GPIO_PIN_10);
    SerialGpio.Mode = GPIO_MODE_AF_PP;
    SerialGpio.Pull = GPIO_PULLUP;
    SerialGpio.Speed = GPIO_SPEED_FREQ_LOW;
    SerialGpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &SerialGpio);

    /* Set up RTS and DTR controlled GPIOs */
    GPIO_InitTypeDef RtsDtrGpio = {
        .Pin = (GPIO_PIN_0 | GPIO_PIN_1),
        .Mode = GPIO_MODE_OUTPUT_PP,
        .Pull = GPIO_PULLDOWN,
        .Speed = GPIO_SPEED_FREQ_LOW,
        .Alternate = 0
    };
    HAL_GPIO_Init(GPIOA, &RtsDtrGpio);

    /* Initialize UART */
    __HAL_RCC_USART1_CLK_ENABLE();
    USB_SERIAL_UART->CR1 = USART_CR1_RTOIE | UART_OVERSAMPLING_16 | UART_WORDLENGTH_8B
            | UART_PARITY_NONE | USART_CR1_RXNEIE | UART_MODE_TX_RX;
    USB_SERIAL_UART->CR2 = UART_RECEIVER_TIMEOUT_ENABLE | UART_STOPBITS_1;
    USB_SERIAL_UART->BRR = (HAL_RCCEx_GetPeriphCLKFreq(USB_SERIAL_UART_PERIPHCLK) + USB_SERIAL_UART_DEFBAUD/2) / USB_SERIAL_UART_DEFBAUD;
    USB_SERIAL_UART->RTOR = ((uint32_t) USB_SERIAL_UART_RXTIMEOUT << USART_RTOR_RTO_Pos) & USART_RTOR_RTO_Msk;
    USB_SERIAL_UART->CR1 |= USART_CR1_UE;

    /* Enable interrupt */
    NVIC_EnableIRQ(USART1_IRQn);


}

void USB_SerialTask(void)
{

}