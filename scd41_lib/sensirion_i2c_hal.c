/*
 * Copyright (c) 2018, Sensirion AG
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of Sensirion AG nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stm32f1xx_hal.h>
#include <stdbool.h>

#include "sensirion_common.h"
#include "sensirion_config.h"
#include "sensirion_i2c_hal.h"

static I2C_HandleTypeDef* g_i2c_handle = NULL;
static osMutexId_t g_i2c_mutex = NULL;

static int16_t sensirion_i2c_hal_lock(bool* locked_here) {
    if (locked_here == NULL) {
        return NOT_IMPLEMENTED_ERROR;
    }

    *locked_here = false;

    if (g_i2c_mutex == NULL) {
        return NO_ERROR;
    }

    if (osKernelGetState() != osKernelRunning) {
        return NO_ERROR;
    }

    if (osMutexGetOwner(g_i2c_mutex) == osThreadGetId()) {
        return NO_ERROR;
    }

    if (osMutexAcquire(g_i2c_mutex, osWaitForever) != osOK) {
        return NOT_IMPLEMENTED_ERROR;
    }

    *locked_here = true;
    return NO_ERROR;
}

static void sensirion_i2c_hal_unlock(bool locked_here) {
    if (locked_here && (g_i2c_mutex != NULL)) {
        (void)osMutexRelease(g_i2c_mutex);
    }
}

static void sensirion_i2c_hal_force_reset_instance(I2C_HandleTypeDef* i2c) {
    if ((i2c == NULL) || (i2c->Instance == NULL)) {
        return;
    }

#ifdef __HAL_RCC_I2C1_FORCE_RESET
    if (i2c->Instance == I2C1) {
        __HAL_RCC_I2C1_FORCE_RESET();
        __HAL_RCC_I2C1_RELEASE_RESET();
        return;
    }
#endif

#ifdef __HAL_RCC_I2C2_FORCE_RESET
    if (i2c->Instance == I2C2) {
        __HAL_RCC_I2C2_FORCE_RESET();
        __HAL_RCC_I2C2_RELEASE_RESET();
        return;
    }
#endif
}

int16_t sensirion_i2c_hal_select_bus(uint8_t bus_idx) {
    (void)bus_idx;
    return NO_ERROR;
}

void sensirion_i2c_hal_set_handle(I2C_HandleTypeDef* i2c_handle) {
    g_i2c_handle = i2c_handle;
}

void sensirion_i2c_hal_set_mutex(osMutexId_t i2c_mutex) {
    g_i2c_mutex = i2c_mutex;
}

I2C_HandleTypeDef* sensirion_i2c_hal_get_handle(void) {
    return g_i2c_handle;
}

osMutexId_t sensirion_i2c_hal_get_mutex(void) {
    return g_i2c_mutex;
}

void sensirion_i2c_hal_init(void) {
}

/**
 * Initialize all hard- and software components that are needed for the I2C
 * communication.
 */

/**
 * Release all resources initialized by sensirion_i2c_hal_init().
 */
void sensirion_i2c_hal_free(void) {
}

/**
 * Execute one read transaction on the I2C bus, reading a given number of bytes.
 * If the device does not acknowledge the read command, an error shall be
 * returned.
 *
 * @param address 7-bit I2C address to read from
 * @param data    pointer to the buffer where the data is to be stored
 * @param count   number of bytes to read from I2C and store in the buffer
 * @returns 0 on success, error code otherwise
 */
int8_t sensirion_i2c_hal_read(uint8_t address, uint8_t* data, uint8_t count) {
    bool locked_here = false;
    int8_t status;

    if (g_i2c_handle == NULL) {
        return (int8_t)HAL_ERROR;
    }

    if (sensirion_i2c_hal_lock(&locked_here) != NO_ERROR) {
        return (int8_t)HAL_ERROR;
    }

    status = (int8_t)HAL_I2C_Master_Receive(g_i2c_handle, (uint16_t)(address << 1),
                                            data, count, 100);

    if (status != (int8_t)HAL_OK) {
        (void)sensirion_i2c_hal_recover_bus();
    }

    sensirion_i2c_hal_unlock(locked_here);

    return status;
}

/**
 * Execute one write transaction on the I2C bus, sending a given number of
 * bytes. The bytes in the supplied buffer must be sent to the given address. If
 * the slave device does not acknowledge any of the bytes, an error shall be
 * returned.
 *
 * @param address 7-bit I2C address to write to
 * @param data    pointer to the buffer containing the data to write
 * @param count   number of bytes to read from the buffer and send over I2C
 * @returns 0 on success, error code otherwise
 */
int8_t sensirion_i2c_hal_write(uint8_t address, const uint8_t* data,
                               uint8_t count) {
    bool locked_here = false;
    int8_t status;

    if (g_i2c_handle == NULL) {
        return (int8_t)HAL_ERROR;
    }

    if (sensirion_i2c_hal_lock(&locked_here) != NO_ERROR) {
        return (int8_t)HAL_ERROR;
    }

    status = (int8_t)HAL_I2C_Master_Transmit(g_i2c_handle, (uint16_t)(address << 1),
                                             (uint8_t*)data, count, 100);

    if (status != (int8_t)HAL_OK) {
        (void)sensirion_i2c_hal_recover_bus();
    }

    sensirion_i2c_hal_unlock(locked_here);

    return status;
}

/**
 * GPIO clock-stretching recovery: toggle SCL up to 9 times to release a
 * slave device that is holding SDA low (e.g. after sensor power loss when
 * the module's on-board pull-ups are also gone).
 * After freeing SDA, send a STOP condition by hand, then re-init the
 * I2C peripheral normally.
 *
 * STM32F103 default pins: I2C1 → PB6(SCL) / PB7(SDA)
 *                          I2C2 → PB10(SCL) / PB11(SDA)
 */
static void sensirion_i2c_hal_gpio_recover(I2C_HandleTypeDef* i2c) {
    GPIO_TypeDef* port;
    uint16_t scl_pin, sda_pin;
    GPIO_InitTypeDef gpio = {0};

    if ((i2c == NULL) || (i2c->Instance == NULL)) {
        return;
    }

    if (i2c->Instance == I2C1) {
        port    = GPIOB;
        scl_pin = GPIO_PIN_6;
        sda_pin = GPIO_PIN_7;
        __HAL_RCC_GPIOB_CLK_ENABLE();
    } else if (i2c->Instance == I2C2) {
        port    = GPIOB;
        scl_pin = GPIO_PIN_10;
        sda_pin = GPIO_PIN_11;
        __HAL_RCC_GPIOB_CLK_ENABLE();
    } else {
        return;
    }

    /* Temporarily take the pins as open-drain outputs */
    gpio.Mode  = GPIO_MODE_OUTPUT_OD;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    gpio.Pin = scl_pin | sda_pin;
    HAL_GPIO_Init(port, &gpio);

    HAL_GPIO_WritePin(port, scl_pin | sda_pin, GPIO_PIN_SET);
    HAL_Delay(1);

    /* Toggle SCL up to 9 times until SDA goes high */
    for (int i = 0; i < 9; i++) {
        if (HAL_GPIO_ReadPin(port, sda_pin) == GPIO_PIN_SET) {
            break;  /* SDA released */
        }
        HAL_GPIO_WritePin(port, scl_pin, GPIO_PIN_RESET);
        HAL_Delay(1);
        HAL_GPIO_WritePin(port, scl_pin, GPIO_PIN_SET);
        HAL_Delay(1);
    }

    /* Generate STOP: SDA low while SCL high, then SDA high */
    HAL_GPIO_WritePin(port, sda_pin, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(port, sda_pin, GPIO_PIN_SET);
    HAL_Delay(1);
}

int16_t sensirion_i2c_hal_recover_bus(void) {
    if (g_i2c_handle == NULL) {
        return (int16_t)HAL_ERROR;
    }

    (void)HAL_I2C_DeInit(g_i2c_handle);
    sensirion_i2c_hal_force_reset_instance(g_i2c_handle);

    /* Bit-bang recovery BEFORE re-init so the peripheral sees a clean bus */
    sensirion_i2c_hal_gpio_recover(g_i2c_handle);

    (void)HAL_I2C_Init(g_i2c_handle);

    return (int16_t)HAL_OK;
}

/**
 * Sleep for a given number of microseconds. The function should delay the
 * execution for at least the given time, but may also sleep longer.
 *
 * @param useconds the sleep time in microseconds
 */
void sensirion_i2c_hal_sleep_usec(uint32_t useconds) {
    uint32_t msec = useconds / 1000;
    if (useconds % 1000 > 0) {
        msec++;
    }

    /*
     * Increment by 1 if STM32F1 driver version less than 1.1.1
     * Old firmwares of STM32F1 sleep 1ms shorter than specified in HAL_Delay.
     * This was fixed with firmware 1.6 (driver version 1.1.1), so we have to
     * fix it ourselves for older firmwares
     */
    if (HAL_GetHalVersion() < 0x01010100) {
        msec++;
    }
    osDelay(msec);
}