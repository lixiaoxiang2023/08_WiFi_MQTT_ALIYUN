#include "lv_port_lcd.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "lcd.h"


// 将你的旧初始化序列转换为正规格式
static const struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t data_bytes;
} custom_init_cmds[] = {
    {0x11, {0}, 0x80},
    {0x36, {0x00}, 1},
    {0x3A, {0x65}, 1},
    {0X21, {0}, 0x80},
    {0x29, {0}, 0x80},   
    {0, {0}, 0xff} // 结束标志
};


lv_disp_t * lv_port_lcd_init(void)
{
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_NUM_WR,       
        .cs_gpio_num = LCD_NUM_CS,       
        .pclk_hz = 40 * 1000 * 1000, 
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        // ⭐ 最小改动 1：强制指定大端序。如果颜色错乱，这是关键。
        .flags.lsb_first = 0, 
    };
    
    // 确保使用你的总线 HOST
    ESP_ERROR_CHECK(
        esp_lcd_new_panel_io_spi(
            (esp_lcd_spi_bus_handle_t)SPI3_HOST,
            &io_config,
            &io_handle
        )
    );
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,             
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    
    ESP_ERROR_CHECK(
        esp_lcd_new_panel_st7789(
            io_handle,
            &panel_config,
            &panel_handle
        )
    );
    // ⭐ 最小改动 2：执行顺序调整
    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);

    // ⭐ 最小改动 3：强制反转颜色并关闭反色（根据ST7789特质切换测试）
    esp_lcd_panel_invert_color(panel_handle, true); 
    esp_lcd_panel_set_gap(panel_handle,0, 0); 

    // 发送自定义序列（保留你的逻辑，但确保延时生效）
    int i = 0;
    while (custom_init_cmds[i].data_bytes != 0xff) {
        esp_lcd_panel_io_tx_param(io_handle, custom_init_cmds[i].cmd, 
                                  custom_init_cmds[i].data, 
                                  custom_init_cmds[i].data_bytes & 0x1F);
        
        if (custom_init_cmds[i].data_bytes & 0x80) {
             vTaskDelay(pdMS_TO_TICKS(120)); // 必须保留，否则初始化无效
        }
        i++;
    }

    esp_lcd_panel_mirror(panel_handle, true, false); 
    esp_lcd_panel_swap_xy(panel_handle, false);
    LCD_PWR(1); // 确保电源引脚被正确设置   
  //  gpio_set_level(LCD_NUM_PWR, 1);   // 背光/电源打开
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&lvgl_cfg);

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = 320 * 20,
        .double_buffer = false,
        .hres = 240, 
        .vres = 320,
        .monochrome = false,
        .flags = { .buff_dma = true }
    };

    lv_disp_t *disp = lvgl_port_add_disp(&disp_cfg);

    lv_disp_set_rotation(disp, LV_DISP_ROT_90);

    return disp;
}