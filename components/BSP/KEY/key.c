/**
 ****************************************************************************************************
 * @file        key.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2023-08-26
 * @brief       按键驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 ESP32-S3 开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 * 
 ****************************************************************************************************
 */

#include "key.h"


/**
 * @brief       初始化按键
 * @param       无
 * @retval      无
 */
// void key_init(void)
// {
//     gpio_config_t gpio_init_struct;

//     gpio_init_struct.intr_type = GPIO_INTR_DISABLE;         /* 失能引脚中断 */
//     gpio_init_struct.mode = GPIO_MODE_INPUT;                /* 输入模式 */
//     gpio_init_struct.pull_up_en = GPIO_PULLUP_ENABLE;       /* 使能上拉 */
//     gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;  /* 失能下拉 */
//     gpio_init_struct.pin_bit_mask = 1ull << BOOT_GPIO_PIN;  /* BOOT按键引脚 */
//     gpio_config(&gpio_init_struct);                         /* 配置使能 */
// }
void key_init(void)
{
    gpio_config_t gpio_init_struct;

    gpio_init_struct.intr_type = GPIO_INTR_DISABLE;         /* 失能引脚中断 */
    gpio_init_struct.mode = GPIO_MODE_INPUT;                /* 输入模式 */
    gpio_init_struct.pull_up_en = GPIO_PULLUP_ENABLE;       /* 使能上拉 */
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;  /* 失能下拉 */
    gpio_init_struct.pin_bit_mask = 1ull << KEY1_GPIO_PIN | (1ULL << KEY2_GPIO_PIN) | (1ULL << KEY3_GPIO_PIN);
    gpio_config(&gpio_init_struct);                         /* 配置使能 */
}
/**
 * @brief       按键扫描函数
 * @param       mode:0->不连续;1->连续
 * @retval      键值, 定义如下:
 *              KEY0_PRES, 1, KEY0按下
 *              KEY1_PRES, 2, KEY1按下
 *              KEY2_PRES, 3, KEY2按下
 *              KEY3_PRES, 4, KEY3按下
 */
uint8_t key_scan(uint8_t mode)
{
    uint8_t keyval = 0;
    static uint8_t key_up = 1;                                          /* 按键按松开标志 */

    if (mode)
    {
        key_up = 1;                                                     /* 支持连按 */
    }
    
    if (key_up && (KEY1_CODE == 0 || KEY2_CODE == 0 || KEY3_CODE == 0 )) /* 按键松开标志为1, 且有任意一个按键按下了 */
    {
        vTaskDelay(10);                                                 /* 去抖动 */
        key_up = 0;

        if (KEY1_CODE == 0)
        {
            keyval = KEY1_PRES;
        }
        if (KEY2_CODE == 0)
        {
            keyval = KEY2_PRES;
        }
        if (KEY3_CODE == 0)
        {
            keyval = KEY3_PRES;
        }

    }
    else if (KEY1_CODE == 1 && KEY2_CODE == 1 && KEY3_CODE == 1)          /* 没有任何按键按下, 标记按键松开 */
    {
        key_up = 1;
    }

    return keyval;                                                      /* 返回键值 */
}