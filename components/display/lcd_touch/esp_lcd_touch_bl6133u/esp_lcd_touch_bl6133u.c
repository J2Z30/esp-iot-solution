/*
 * SPDX-FileCopyrightText: 2015-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"


#define BL6133U_GESTURE_CODE       (0x01)
#define BL6133U_POINT_NUMBER       (0x02)
#define BL6133U_EV_FLAG_TP_XH      (0x03)
#define BL6133U_TP_XL              (0x04)
#define BL6133U_TPID_TP_YH         (0x05)
#define BL6133U_TP_YL              (0x06)
#define BL6133U_RESERVED_07        (0x07)
#define BL6133U_RESERVED_08        (0x08)

#define BL6133U_MAX_POINT_NUMBER   (1)

static const char *TAG = "BL6133U";

/*******************************************************************************
* Function definitions
*******************************************************************************/
static esp_err_t esp_lcd_touch_bl6133u_read_data(esp_lcd_touch_handle_t tp);
static bool esp_lcd_touch_bl6133u_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num);
static esp_err_t esp_lcd_touch_bl6133u_del(esp_lcd_touch_handle_t tp);

/* I2C read/write */
static esp_err_t touch_bl6133u_i2c_read(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t *data, uint8_t len);
static esp_err_t touch_bl6133u_i2c_write(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t data);

/* GT911 reset */
static esp_err_t touch_bl6133u_reset(esp_lcd_touch_handle_t tp);
/* Read status and config register */
static esp_err_t touch_bl6133u_read_cfg(esp_lcd_touch_handle_t tp);

/*******************************************************************************
* Public API functions
*******************************************************************************/
esp_err_t esp_lcd_touch_new_i2c_bl6133u(const esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *tp)
{
    ESP_RETURN_ON_FALSE(io, ESP_ERR_INVALID_ARG, TAG, "Invalid io");
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "Invalid config");
    ESP_RETURN_ON_FALSE(tp, ESP_ERR_INVALID_ARG, TAG, "Invalid touch handle");

    /* Prepare main structure */
    esp_err_t ret = ESP_OK;
    esp_lcd_touch_handle_t bl6133u = calloc(1, sizeof(esp_lcd_touch_t));
    ESP_GOTO_ON_FALSE(bl6133u, ESP_ERR_NO_MEM, err, TAG, "Touch handle malloc failed");

    /* Communication interface */
    bl6133u->io = io;
    /* Only supported callbacks are set */
    bl6133u->read_data = esp_lcd_touch_bl6133u_read_data;
    bl6133u->get_xy = esp_lcd_touch_bl6133u_get_xy;
    bl6133u->del = esp_lcd_touch_bl6133u_del;
    /* Mutex */
    bl6133u->data.lock.owner = portMUX_FREE_VAL;
    /* Save config */
    memcpy(&bl6133u->config, config, sizeof(esp_lcd_touch_config_t));

    /* Prepare pin for touch interrupt */
    if (bl6133u->config.int_gpio_num != GPIO_NUM_NC) {
        const gpio_config_t int_gpio_config = {
            .mode = GPIO_MODE_INPUT,
            .intr_type = (bl6133u->config.levels.interrupt ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE),
            .pin_bit_mask = BIT64(bl6133u->config.int_gpio_num)
        };
        ESP_GOTO_ON_ERROR(gpio_config(&int_gpio_config), err, TAG, "GPIO intr config failed");

        /* Register interrupt callback */
        if (bl6133u->config.interrupt_callback) {
            esp_lcd_touch_register_interrupt_callback(bl6133u, bl6133u->config.interrupt_callback);
        }
    }
    /* Prepare pin for touch controller reset */
    if (bl6133u->config.rst_gpio_num != GPIO_NUM_NC) {
        const gpio_config_t rst_gpio_config = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = BIT64(bl6133u->config.rst_gpio_num)
        };
        ESP_GOTO_ON_ERROR(gpio_config(&rst_gpio_config), err, TAG, "GPIO reset config failed");
    }

    /* Reset controller */
    ESP_GOTO_ON_ERROR(touch_bl6133u_reset(bl6133u), err, TAG, "Reset failed");

        /* Read status and config info */
    ret = touch_bl6133u_read_cfg(bl6133u);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "GT911 init failed");

    *tp = bl6133u;

    return ESP_OK;

err:
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error (0x%x)! Touch controller BL6133U initialization failed!", ret);
        if (bl6133u) {
            esp_lcd_touch_bl6133u_del(bl6133u);
        }
    }

    return ret;
}

static esp_err_t esp_lcd_touch_bl6133u_read_data(esp_lcd_touch_handle_t tp)
{
    uint8_t points = 0;
    uint8_t data[4] = { 0 };
    size_t i = 0;
    esp_err_t err = ESP_OK;

    assert(tp != NULL);

    err = touch_bl6133u_i2c_read(tp, BL6133U_POINT_NUMBER, &points, 1);
    ESP_RETURN_ON_ERROR(err, TAG, "I2C read error!");

    if (points > BL6133U_MAX_POINT_NUMBER || points == 0) {
        return ESP_OK;
    }

    /* Number of touched points */
    points = (points > CONFIG_ESP_LCD_TOUCH_MAX_POINTS ? CONFIG_ESP_LCD_TOUCH_MAX_POINTS : points);

    err = touch_bl6133u_i2c_read(tp, BL6133U_EV_FLAG_TP_XH, data, sizeof data);
    ESP_RETURN_ON_ERROR(err, TAG, "I2C read error!");

    portENTER_CRITICAL(&tp->data.lock);

    /* Number of touched points */
    tp->data.points = points;

    /* Fill all coordinates */
    for (i = 0; i < points; i++) {
        tp->data.coords[i].x = (((uint16_t)data[0] & 0x3f) << 8) + data[1];
        tp->data.coords[i].y = (((uint16_t)data[2] & 0x0f) << 8) + data[3];
    }

    portEXIT_CRITICAL(&tp->data.lock);

    return ESP_OK;
}

static bool esp_lcd_touch_bl6133u_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num)
{
    assert(tp != NULL);
    assert(x != NULL);
    assert(y != NULL);
    assert(point_num != NULL);
    assert(max_point_num > 0);

    portENTER_CRITICAL(&tp->data.lock);

    /* Count of points */
    *point_num = (tp->data.points > max_point_num ? max_point_num : tp->data.points);

    for (size_t i = 0; i < *point_num; i++) {
        x[i] = tp->data.coords[i].x;
        y[i] = tp->data.coords[i].y;

        if (strength) {
            strength[i] = tp->data.coords[i].strength;
        }
    }

    /* Invalidate */
    tp->data.points = 0;

    portEXIT_CRITICAL(&tp->data.lock);

    return (*point_num > 0);
}

static esp_err_t esp_lcd_touch_bl6133u_del(esp_lcd_touch_handle_t tp)
{
    assert(tp != NULL);

    /* Reset GPIO pin settings */
    if (tp->config.int_gpio_num != GPIO_NUM_NC) {
        gpio_reset_pin(tp->config.int_gpio_num);
        if (tp->config.interrupt_callback) {
            gpio_isr_handler_remove(tp->config.int_gpio_num);
        }
    }

    /* Reset GPIO pin settings */
    if (tp->config.rst_gpio_num != GPIO_NUM_NC) {
        gpio_reset_pin(tp->config.rst_gpio_num);
    }

    free(tp);

    return ESP_OK;
}


/*******************************************************************************
* Private API function
*******************************************************************************/

/* Reset controller */
static esp_err_t touch_bl6133u_reset(esp_lcd_touch_handle_t tp)
{
    assert(tp != NULL);

    if (tp->config.rst_gpio_num != GPIO_NUM_NC) {
        ESP_RETURN_ON_ERROR(gpio_set_level(tp->config.rst_gpio_num, tp->config.levels.reset), TAG, "GPIO set level error!");
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_RETURN_ON_ERROR(gpio_set_level(tp->config.rst_gpio_num, !tp->config.levels.reset), TAG, "GPIO set level error!");
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return ESP_OK;
}

static esp_err_t touch_bl6133u_read_cfg(esp_lcd_touch_handle_t tp)
{
    uint8_t buf[1] = { 0 };

    assert(tp != NULL);

    ESP_RETURN_ON_ERROR(touch_bl6133u_i2c_read(tp, BL6133U_TPID_TP_YH, buf, 1), TAG, "GT911 read error!");

    ESP_LOGI(TAG, "TouchID: 0x%02x", (buf[0] >> 4) & 0x0F);

    return ESP_OK;
}

static esp_err_t touch_bl6133u_i2c_read(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t *data, uint8_t len)
{
    assert(tp != NULL);
    assert(data != NULL);

    /* Read data */
    return esp_lcd_panel_io_rx_param(tp->io, reg, data, len);
}

static esp_err_t touch_bl6133u_i2c_write(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t data)
{
    assert(tp != NULL);

    // *INDENT-OFF*
    /* Write data */
    return esp_lcd_panel_io_tx_param(tp->io, reg, (uint8_t[]){data}, 1);
    // *INDENT-ON*
}