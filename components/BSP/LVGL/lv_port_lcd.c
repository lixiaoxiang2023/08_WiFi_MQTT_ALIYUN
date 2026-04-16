#include "lv_port_lcd.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

// 将你的旧初始化序列转换为正规格式
static const struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t data_bytes;
} custom_init_cmds[] = {
#if SPI_LCD_TYPE // 2.4寸
    {0x11, {0}, 0x80},
    {0x36, {0x00}, 1},
    {0x3A, {0x65}, 1},
    {0X21, {0}, 0x80},
    {0x29, {0}, 0x80},
#else           // 1.3寸
    {0x11, {0}, 0x80},
    {0x36, {0x00}, 1},
    {0x3A, {0x65}, 1},
    {0xB2, {0x0C, 0x0C, 0x00, 0x33, 0x33}, 5},
    {0xB7, {0x75}, 1},
    {0xBB, {0x1C}, 1},
    {0xC0, {0x2c}, 1},
    {0xC2, {0x01}, 1},
    {0xC3, {0x0F}, 1},
    {0xC4, {0x20}, 1},
    {0xC6, {0X01}, 1},
    {0xD0, {0xA4, 0xA1}, 2},
    {0xE0, {0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23}, 14},
    {0xE1, {0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23}, 14},
    {0X21, {0}, 0x80},
    {0x29, {0}, 0x80},
#endif
    {0, {0}, 0xff} // 结束标志
};
lv_disp_t * lv_port_lcd_init(void)
{
    // 1. 引脚和总线配置 (对应你提供的 lcd_init)
    spi_bus_config_t buscfg = {
        .sclk_io_num = 12,        // 根据正点原子 S3 开发板
        .mosi_io_num = 11, 
        .miso_io_num = 13,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 320 * 240 * 2,
    };
    // 注意：如果之前报错 0x103，请确保此处没有被重复初始化
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

    // 2. IO 接口配置 (对应你代码里的 devcfg)
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = 11,        // 对应你的 lcd_self.wr (DC引脚)
        .cs_gpio_num = 10,        // 对应你的 lcd_self.cs
        .pclk_hz = 60 * 1000 * 1000, // 60MHz
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle);

    // 3. 面板配置
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = 1,      // 复位引脚
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    
    // 创建通用的 ST7789/ILI9341 面板对象
    esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle);

    // 4. 执行初始化 (这里不再手动写 while 循环，直接调用官方序列)
    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);

    // 5. 【关键】发送你提供的 custom_init_cmds 自定义初始化序列
    int i = 0;
    while (custom_init_cmds[i].data_bytes != 0xff) {
        esp_lcd_panel_io_tx_param(io_handle, custom_init_cmds[i].cmd, 
                                  custom_init_cmds[i].data, 
                                  custom_init_cmds[i].data_bytes & 0x1F);
        if (custom_init_cmds[i].data_bytes & 0x80) {
         //   vTaskDelay(pdMS_TO_TICKS(120));
        }
        i++;
    }

    // 设置方向和打开背光
    esp_lcd_panel_mirror(panel_handle, true, false); // 对应 lcd_display_dir(1)
    gpio_set_direction(40, GPIO_MODE_OUTPUT);
    gpio_set_level(40, 1); // LCD_PWR(1)

    // 6. 注册到 LVGL Port
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&lvgl_cfg);

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = 320 * 40,
        .double_buffer = true,
        .hres = 320,
        .vres = 240,
        .monochrome = false,
        .flags = { .buff_dma = true }
    };

    return lvgl_port_add_disp(&disp_cfg);
}