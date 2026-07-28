/**
 ****************************************************************************************************
 * @file        tud_flash.c
 ****************************************************************************************************
 * @attention
 ****************************************************************************************************
 */

#include "tud_flash.h"
#include "file_worker.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "spi_sdcard.h"
#include "spi_sdcard.h"
#include "lvgl_manager.h"
static const char *TAG = "usb_msc";
const char *disk_path = "/0:";                /* TF卡的路径 (SPI模式) */
static uint8_t s_pdrv = 0;                      /* 用于识别驱动器的物理驱动器 */
static int s_disk_block_size = 0;               /* 磁盘块的大小 */
#define LOGICAL_DISK_NUM        1               /* 磁盘个数 */
static bool ejected[LOGICAL_DISK_NUM] = {true}; /* 弹出状态 */
__usbdev g_usbdev;                              /* USB控制器 */

typedef enum {
    STORAGE_SOURCE_NONE = 0,
    STORAGE_SOURCE_TF_CARD,
    STORAGE_SOURCE_INTERNAL_FLASH
} storage_source_t;

static storage_source_t s_storage_source = STORAGE_SOURCE_NONE;

static esp_err_t tud_sdcard_init(const char *base_path);
static esp_err_t tud_fat_partitions_init(const char *base_path);

// 封装一个简单的加锁宏，带 100ms 超时，防止死锁

//--------------------------------------------------------------------+
// 以下是USB回调函数，一般用来判断连接过程
//--------------------------------------------------------------------+

/**
 * @brief       在安装USB设备时调用此函数
 * @param       无
 * @retval      无
 */
void tud_mount_cb(void)
{
    /* 每次插入USB时重置及跟踪。这样可以获取插入，弹出状态，有利于重新插上获得驱动器 */
    for (uint8_t i = 0; i < LOGICAL_DISK_NUM; i++)
    {
        ejected[i] = false;
    }

    g_usbdev.status |= 0x01;
    ui_set_usb_connected(true);

    ESP_LOGI(__func__, "");
}

/**
 * @brief       USB设备卸载时调用此函数
 * @param       无
 * @retval      无
 */
void tud_umount_cb(void)
{
    ui_set_usb_connected(false);
    ESP_LOGW(__func__, "");
}

/**
 * @brief       USB总线挂起时调用此函数
 * @param       无
 * @retval      无
 */
void tud_suspend_cb(bool remote_wakeup_en)
{
    g_usbdev.status &= 0x00;
    ui_set_usb_connected(false);
    ESP_LOGW(__func__, "");
}

/**
 * @brief       恢复USB总线时调用
 * @param       无
 * @retval      无
 */
void tud_resume_cb(void)
{
    ui_set_usb_connected(true);
    ESP_LOGW(__func__, "");
}

/**
 * @brief       用于刷新任何挂起的缓存
 * @param       无
 * @retval      无
 */
void tud_msc_write10_complete_cb(uint8_t lun)
{
    if (lun >= LOGICAL_DISK_NUM)
    {
        ESP_LOGE(__func__, "invalid lun number %u", lun);
        return;
    }

    /* 此写入完成，启动自动重新加载时钟 */
    ESP_LOGD(__func__, "");
}

/**
 * @brief       已弹出磁盘
 * @param       无
 * @retval      无
 */
static bool _logical_disk_ejected(void)
{
    bool all_ejected = true;

    for (uint8_t i = 0; i < LOGICAL_DISK_NUM; i++)
    {
        all_ejected &= ejected[i];
    }

    return all_ejected;
}

/**
 * @brief       收到SCSI_CMD_INQUIRY时调用此函数，用于从目标设备获取基本信息
 * @param       lun         :磁盘数量
 * @param       vendor_id   :供应商id
 * @param       product_id  :产品id
 * @param       product_rev :修订版
 * @retval      无
 */
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
    ESP_LOGD(__func__, "");

    if (lun >= LOGICAL_DISK_NUM)
    {
        ESP_LOGE(__func__, "invalid lun number %u", lun);
        return;
    }

    const char vid[] = "ESPS3-S3";
    const char pid[] = "Mass Storage";
    const char rev[] = "1.0";

    memcpy(vendor_id, vid, strlen(vid));
    memcpy(product_id, pid, strlen(pid));
    memcpy(product_rev, rev, strlen(rev));
}

/**
 * @brief       收到测试单元就绪命令时调用
 * @param       lun:磁盘数量
 * @retval      true允许主机读取/写入此LUN，例如插入的SD卡
 */
bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    ESP_LOGD(__func__, "");

    if (lun >= LOGICAL_DISK_NUM)
    {
        ESP_LOGE(__func__, "invalid lun number %u", lun);
        return false;
    }

    if (_logical_disk_ejected())
    {
        /* 为不存在的磁盘设置0x3a */
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);
        return false;
    }

    return true;
}

/**
 * @brief       当收到SCSI_CMD_READ_CAPACITY_10和SCSI_CMD _READ_FORMAT_CCAPITATION时调用此函数，以确定磁盘大小
 * @param       lun         :磁盘数量
 * @param       block_count :块数量
 * @param       block_size  :块大小
 * @retval      true允许主机读取/写入此LUN，例如插入的SD卡
 */
void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    ESP_LOGD(__func__, "");

    if (lun >= LOGICAL_DISK_NUM)
    {
        ESP_LOGE(__func__, "invalid lun number %u", lun);
        return;
    }

    disk_ioctl(s_pdrv, GET_SECTOR_COUNT, block_count);
    disk_ioctl(s_pdrv, GET_SECTOR_SIZE, block_size);
    s_disk_block_size = *block_size;
    ESP_LOGD(__func__, "GET_SECTOR_COUNT = %"PRIu32"，GET_SECTOR_SIZE = %d", *block_count, *block_size);
}

/**
 * @brief       调用以检查设备是否可作为SCSI
 * @param       lun         :磁盘数量
 * @retval      true:可以;false:不可以
 */
bool tud_msc_is_writable_cb(uint8_t lun)
{
    ESP_LOGD(__func__, "");

    if (lun >= LOGICAL_DISK_NUM)
    {
        ESP_LOGE(__func__, "invalid lun number %u", lun);
        return false;
    }

    return true;
}

/**
 * @brief       收到“启动-停止单元”命令时调用
 * @param       lun             :磁盘数量
 * @param       power_condition :电源条件
 * @param       start           :Start = 0：停止电源模式;Start = 1：活动模式
 * @param       load_eject      :Start = 0,load_eject = 1：卸载磁盘存储;Start = 1,load_eject=1：加载磁盘存储
 * @retval      true:加载磁盘存储成功;false:卸载磁盘存储成功
 */
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
    ESP_LOGI(__func__, "");
    (void) power_condition;

    if (lun >= LOGICAL_DISK_NUM)
    {
        ESP_LOGE(__func__, "invalid lun number %u", lun);
        return false;
    }

    if (load_eject)
    {
        if (!start)
        {
            /* 弹出磁盘 */
            if (disk_ioctl(s_pdrv, CTRL_SYNC, NULL) != RES_OK)
            {
                return false;
            }
            else
            {
                ejected[lun] = true;
            }
        }
        else
        {
            /* 只有在它没有弹出的情况下才能加载 */
            return !ejected[lun];
        }
    }
    else
    {
        if (!start)
        {
            /* 停止装置，但不弹出 */
            if (disk_ioctl(s_pdrv, CTRL_SYNC, NULL) != RES_OK)
            {
                return false;
            }
        }

        /* 始终启动设备，即使弹出 */
    }

    return true;
}

/**
 * @brief       收到READ10命令时调用回调此函数
 * @param       lun     :磁盘数量
 * @param       lba     :块地址
 * @param       offset  :数据偏移
 * @param       buffer  :读取数据的存储区
 * @param       bufsize :读取数据大小
 * @retval      返回读取的字节数
 */
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    if (lun >= LOGICAL_DISK_NUM) return 0;

    const uint32_t block_count = bufsize / s_disk_block_size;
    //vTaskDelay(pdMS_TO_TICKS(10));
    /* 磁盘读取 */
    disk_read(s_pdrv, buffer, lba, block_count);

    return block_count * s_disk_block_size;
}

/**
 * @brief       收到WRITE10命令时调用回调此函数
 * @param       lun     :磁盘数量
 * @param       lba     :块地址
 * @param       offset  :写入偏移
 * @param       buffer  :写入数据的存储区
 * @param       bufsize :写入数据大小
 * @retval      返回写入的字节数
 */
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    (void) offset;
    if (lun >= LOGICAL_DISK_NUM) return 0;
    const uint32_t block_count = bufsize / s_disk_block_size;
    //vTaskDelay(pdMS_TO_TICKS(10));

    /* 磁盘写入 */
    disk_write(s_pdrv, buffer, lba, block_count);

    return block_count * s_disk_block_size;
}

/**
 * @brief       当收到不在下面内置列表中的SCSI命令时调用此函数
 * @param       lun         :磁盘数量
 * @param       scsi_cmd    :scsi命令内容，应用程序必须检查该命令内容才能做出相应响应
 * @param       buffer      :SCSI数据阶段的缓冲区
 * @param       bufsize     :缓冲区的长度
 * @retval      返回写入的字节数
 */
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize)
{
    /* read10&write10有自己的回调，不能在这里处理 */
    ESP_LOGD(__func__, "");

    if (lun >= LOGICAL_DISK_NUM)
    {
        ESP_LOGE(__func__, "invalid lun number %u", lun);
        return 0;
    }

    void const *response = NULL;
    uint16_t resplen = 0;

    /* 处理的大多数scsi都是输入 */
    bool in_xfer = true;

    switch (scsi_cmd[0])
    {
        case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
            /* 主机即将读/写等。。。最好不要断开磁盘连接 */
            resplen = 0;
            break;

        default:
            /* 设置无效的命令操作 */
            tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);

            /* 否定意味着错误->tinyusb可能会暂停和/或以失败状态响应 */
            resplen = -1;
            break;
    }

    /* resplen不得大于bufsize */
    if (resplen > bufsize)
    {
        /* 如果大于，则resplen = bufsize */
        resplen = bufsize;
    }

    if (response && (resplen > 0))
    {
        if (in_xfer)
        {
            memcpy(buffer, response, resplen);
        }
        else
        {
            /* SCSI output */
        }
    }

    return resplen;
}

//--------------------------------------------------------------------+
// 以上是USB回调函数，一般用来判断连接过程
//--------------------------------------------------------------------+
/**
 * @brief       初始化内部 flash 分区为 FAT 文件系统
 * @param       base_path:定义分区的名称
 * @retval      无
 */
static esp_err_t tud_fat_partitions_init(const char *base_path)
{
    ESP_LOGI(TAG, "Mounting internal flash as FAT filesystem");
    esp_err_t ret = ESP_FAIL;
    wl_handle_t wl_handle_1 = WL_INVALID_HANDLE;

    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 9,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE
    };

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    ret = esp_vfs_fat_spiflash_mount_rw_wl(base_path, "vfs", &mount_config, &wl_handle_1);
#else
    ret = esp_vfs_fat_spiflash_mount(base_path, "vfs", &mount_config, &wl_handle_1);
#endif

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to mount internal flash FATFS (%s)", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

/**
 * @brief       选择 USB MSC 的存储后端，优先使用 TF 卡；无卡时回退到内部 flash
 * @param       base_path:挂载点路径
 * @retval      esp_err_t
 */
static esp_err_t tud_select_storage_backend(const char *base_path)
{
    esp_err_t ret = tud_sdcard_init(base_path);
    if (ret == ESP_OK) {
        s_storage_source = STORAGE_SOURCE_TF_CARD;
        ESP_LOGI(TAG, "USB MSC storage backend: TF card");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "TF card unavailable (%s), use internal flash as USB MSC storage", esp_err_to_name(ret));

    ret = tud_fat_partitions_init(base_path);
    if (ret == ESP_OK) {
        s_storage_source = STORAGE_SOURCE_INTERNAL_FLASH;
        ESP_LOGI(TAG, "USB MSC storage backend: internal flash");
        return ESP_OK;
    }

    s_storage_source = STORAGE_SOURCE_NONE;
    return ret;
}

/**
 * @brief       初始化TF卡的函数
 * @param       base_path:挂载点路径
 * @retval      esp_err_t
 */
static esp_err_t tud_sdcard_init(const char *base_path)
{
    (void)base_path;
    ESP_LOGI(TAG, "Initializing SD card using SPI mode");

    // 检查SPI总线是否已经初始化
    ESP_LOGI(TAG, "Checking SPI bus status...");

    // 使用项目中已有的SPI SD卡初始化函数
    esp_err_t ret = sd_spi_init();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SD card via SPI (%s)", esp_err_to_name(ret));

        // 提供更详细的错误诊断
        switch(ret) {
            case ESP_ERR_INVALID_STATE:
                ESP_LOGE(TAG, "SPI bus not initialized or SD card already mounted");
                break;
            case ESP_ERR_NOT_FOUND:
                ESP_LOGE(TAG, "SD card not detected - check if card is inserted");
                break;
            case ESP_ERR_TIMEOUT:
                ESP_LOGE(TAG, "SD card communication timeout - check wiring");
                break;
            default:
                ESP_LOGE(TAG, "Unknown SD card error");
                break;
        }
        return ret;
    }

    ESP_LOGI(TAG, "SD card initialized successfully via SPI");
    return ESP_OK;
}

/**
 * @brief       FLASH模拟U盘函数初始化
 * @param       无
 * @retval      无
 */
void tud_usb_flash(void)
{
    esp_err_t ret = tud_select_storage_backend(disk_path);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize USB MSC storage backend (%s)", esp_err_to_name(ret));
        return;
    }

    vTaskDelay(100);

    const tinyusb_config_t tusb_cfg = {0};
    /* USB设备登记 */
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ui_set_usb_connected(false);
    ESP_LOGI(TAG, "USB MSC initialization DONE");
}


void usb_copy_task(void *arg)
{
    file_copy_msg_t msg;
    tud_usb_flash();

    usb_copy_queue = xQueueCreate(2, sizeof(file_copy_msg_t));

    // 建议在外部申请一个较大的缓冲区，使用内部 RAM 以获得最快速度
    // 如果内部 RAM 不足，可以使用 heap_caps_malloc 申请 PSRAM
    const size_t buf_size = 16 * 1024; // 16KB 缓冲区
    uint8_t *buf = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE("USB", "Failed to allocate copy buffer!");
        vTaskDelete(NULL);
    }

    while (1) {
        if (xQueueReceive(usb_copy_queue, &msg, portMAX_DELAY)) {
            // 给系统一点准备时间，但不要在循环里延时
            vTaskDelay(pdMS_TO_TICKS(100)); 

            ESP_LOGI("USB", "Copying %s -> %s", msg.src, msg.dst);

            FILE *src = fopen(msg.src, "rb");
            FILE *dst = fopen(msg.dst, "wb");

            if (!src || !dst) {
                ESP_LOGE("USB", "File open failed!");
                if (src) fclose(src);
                if (dst) fclose(dst);
                continue;
            }

            size_t len;
            // 记录时间用于调试速度
            uint32_t start_tick = xTaskGetTickCount();

            while ((len = fread(buf, 1, buf_size, src)) > 0) {
                if (fwrite(buf, 1, len, dst) != len) {
                    ESP_LOGE("USB", "Write failed");
                    break;
                }
                // 关键：不要在这里使用 vTaskDelay(1)！它会让速度强制降到 <100KB/s
                // 如果担心喂狗问题，可以使用 taskYIELD()
                taskYIELD(); 
            }

            uint32_t end_tick = xTaskGetTickCount();
            ESP_LOGI("USB", "Copy done in %ld ms", (end_tick - start_tick) * portTICK_PERIOD_MS);

            fflush(dst);
            fclose(dst);
            fclose(src);

            /* ========= 强制主机重新识别 ========= */
            ESP_LOGW("USB", "Re-enumerating USB MSC...");
            tud_disconnect();
            vTaskDelay(pdMS_TO_TICKS(1000)); // 延长断开时间确保 Windows 感知到
            tud_connect();
        }
    }
    free(buf); // 实际上不会执行到这里
}
