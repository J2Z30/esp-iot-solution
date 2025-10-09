# ESP LCD JD9853

[![Component Registry](https://components.espressif.com/components/espressif/esp_lcd_jd9853/badge.svg)](https://components.espressif.com/components/espressif/esp_lcd_jd9853)

Implementation of the JD9853 LCD controller with esp_lcd component.

| LCD controller | Communication interface | Component name |                                   Link to datasheet                                   |
| :------------: | :---------------------: | :------------: | :-----------------------------------------------------------------------------------: |
|     JD9853     |        SPI         | esp_lcd_jd9853 | [PDF](https://admin.osptek.com/uploads/JD_9853_DS_Preliminary_V0_00_20230424_161c1b3786.pdf) |

**Note**: MIPI-DSI interface only supports ESP-IDF v5.3 and above versions.

For more information on LCD, please refer to the [LCD documentation](https://docs.espressif.com/projects/esp-iot-solution/en/latest/display/lcd/index.html).

## Add to project

Packages from this repository are uploaded to [Espressif's component service](https://components.espressif.com/).
You can add them to your project via `idf.py add-dependancy`, e.g.

```
    idf.py add-dependency "espressif/esp_lcd_jd9853"
```

Alternatively, you can create `idf_component.yml`. More is in [Espressif's documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/tools/idf-component-manager.html).

## Example use

```c
/**
 * Uncomment these line if use custom initialization commands.
 * The array should be declared as static const and positioned outside the function.
 */
// static const jd9853_lcd_init_cmd_t lcd_init_cmds[] = {
//  {cmd, { data }, data_size, delay_ms}
    // {0xDF, (uint8_t[]){0x98, 0x53}, 2, 0},
    // {0xB2, (uint8_t[]){0x23}, 1, 0},
    // {0xB7, (uint8_t[]){0x00, 0x47, 0x00, 0x6F}, 4, 0},
    // {0xBB, (uint8_t[]){0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0}, 6, 0},
//     ...
// };

    ESP_LOGI(TAG, "Install JD9853 panel driver");

    spi_bus_config_t spi_bus = { 0 };
    esp_lcd_panel_io_handle_t panel_io = nullptr;
    esp_lcd_panel_handle_t panel_handle = NULL;
#if LCD_USING_QSPI
    ESP_LOGI(TAG, "QSPI BUS init");
    spi_bus.data0_io_num = SPI_LCD_PIN_NUM_SDA;
    spi_bus.data1_io_num = SPI_LCD_PIN_NUM_DB1;
    spi_bus.sclk_io_num = SPI_LCD_PIN_NUM_CLK;
    spi_bus.data2_io_num = SPI_LCD_PIN_NUM_DB2;
    spi_bus.data3_io_num = SPI_LCD_PIN_NUM_DB3;
#else
    ESP_LOGI(TAG, "SPI BUS init");
    spi_bus.mosi_io_num = SPI_LCD_PIN_NUM_SDA;
    spi_bus.miso_io_num = GPIO_NUM_NC;
    spi_bus.sclk_io_num = SPI_LCD_PIN_NUM_CLK;
    spi_bus.quadwp_io_num = GPIO_NUM_NC;
    spi_bus.quadhd_io_num = GPIO_NUM_NC;
#endif
    spi_bus.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &spi_bus, SPI_DMA_CH_AUTO));

    ESP_LOGD(TAG, "Install panel IO");
    // 液晶屏控制IO初始化
    esp_lcd_panel_io_spi_config_t io_config = JD9853_PANEL_IO_SPI_CONFIG(SPI_LCD_PIN_NUM_CS, SPI_LCD_PIN_NUM_RS, NULL, NULL);
    esp_lcd_new_panel_io_spi(SPI_LCD_HOST, &io_config, &panel_io);
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = SPI_LCD_PIN_NUM_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9853(panel_io, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
```
