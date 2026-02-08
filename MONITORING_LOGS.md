# 📊 内存和任务监控日志说明

## 概述
本工程已添加详细的监控日志，用于追踪：
- 任务创建、优先级和栈大小
- 堆内存使用情况
- 文件操作流程
- OTA 进度和状态

---

## 📋 启动阶段日志

### app_main() 初始化
```
I (MAIN): ========================================
I (MAIN): 📋 TASK CREATION
I (MAIN):    Queue 1: usb_copy (depth=2)
I (MAIN):    Queue 2: file_task (depth=3)
I (MAIN): 📊 Current Heap Status:
I (MAIN):    Free: 182456 bytes
I (MAIN):    Largest block: 180000 bytes
I (MAIN):    SPIRAM: 2000000 bytes
I (MAIN):    Creating file_task_worker...
I (MAIN):    ✅ Stack: 6 KB | Priority: 5
I (MAIN):    Creating usb_copy_task...
I (MAIN):    ✅ Stack: 2 KB | Priority: 4 | Core: 0
I (MAIN): ========================================
```

**说明：**
- ✅ 堆空间足够（>180KB）
- 👁️  监控任务栈大小和优先级

---

### WiFi 连接阶段
```
I (MAIN): ========================================
I (MAIN): ✅ WiFi Connected
I (MAIN): 📊 Heap after WiFi: 156234 bytes
I (MAIN): ========================================

I (MAIN): ========================================
I (MAIN): 🚀 Starting main event loop (lwip_demo)...
I (MAIN): 📊 Heap: 155000 bytes (largest: 150000)
I (MAIN): ========================================
```

**说明：**
- WiFi 连接消耗约 26KB 内存
- 确保剩余堆足以处理 MQTT 和文件操作

---

## 🚀 OTA 启动日志

### huawei_ota_start() 创建任务
```
I (HUAWEI_OTA): ========================================
I (HUAWEI_OTA): 🚀 OTA START REQUEST
I (HUAWEI_OTA): 📌 URL: https://obs-xxx.myhuaweicloud.com/fw.bin
I (HUAWEI_OTA): 📌 Heap before task: 145000 bytes (largest: 140000)
I (HUAWEI_OTA): ✅ OTA task created
I (HUAWEI_OTA):    Stack: 6 KB | Priority: 3
I (HUAWEI_OTA): ========================================
```

**关键点：**
- ✅ 优先级为 3（低于文件任务的优先级 5）
- ✅ 栈大小为 6KB（节省 2KB 内存）
- 📊 显示任务创建前的堆状态

---

## 📥 文件任务启动日志

### file_task_worker() 初始化
```
I (MAIN): ========================================
I (MAIN): 📋 FILE_TASK_WORKER STARTED
I (MAIN):    Name: file_task_worker | Priority: 5
I (MAIN):    Initial Heap: 143000 bytes
I (MAIN): ========================================
```

---

### 消息处理流程
```
I (MAIN): ───────────────────────────────
I (MAIN): 📥 Message received, processing...
I (MAIN):    Current Heap: 142500 bytes
I (MAIN): ✅ Huawei OTA command handled
I (MAIN): ───────────────────────────────
```

或

```
I (MAIN): ───────────────────────────────
I (MAIN): 📥 Message received, processing...
I (MAIN):    Current Heap: 142500 bytes
I (MAIN): 📤 UPLOAD: firmware.bin
I (MAIN):    Heap before: 142500 bytes
I (HUAWEI_OTA): ⬇️  Starting firmware download: 1048576 bytes
I (MAIN): ✅ Upload completed
I (MAIN):    Heap after: 141800 bytes
I (MAIN): ───────────────────────────────
```

---

## 📊 OTA 进度监控

### 下载进度日志（每 5% 报告一次）
```
I (HUAWEI_OTA): ========================================
I (HUAWEI_OTA): ⭐ OTA TASK STARTED
I (HUAWEI_OTA): Task Name: huawei_ota | Priority: 3
I (HUAWEI_OTA): Heap Before: 145000 bytes (largest block: 140000)
I (HUAWEI_OTA): ========================================

I (HUAWEI_OTA): 📌 OTA Buffer allocated: 2048 bytes
I (HUAWEI_OTA): 📌 Heap after malloc: 142952 bytes (largest: 140000)
I (HUAWEI_OTA): 📌 Target partition: ota_1
I (HUAWEI_OTA): 📌 HTTP status: 200, Content-Length: 1048576
I (HUAWEI_OTA): ⬇️  Starting firmware download: 1048576 bytes

I (HUAWEI_OTA): 📈 OTA Progress: 5% (52428/1048576 bytes)
I (HUAWEI_OTA): 📈 OTA Progress: 10% (104857/1048576 bytes)
I (HUAWEI_OTA): 📈 OTA Progress: 15% (157286/1048576 bytes)
I (HUAWEI_OTA):     Heap: 140500 bytes (largest block: 135000)
I (HUAWEI_OTA): 📈 OTA Progress: 20% (209715/1048576 bytes)
...
I (HUAWEI_OTA): 📈 OTA Progress: 100% (1048576/1048576 bytes)
```

**说明：**
- 每 5% 报告一次进度
- 每 20% 额外报告堆状态（检查碎片化）

---

## ✅ OTA 完成日志

### 成功完成
```
I (HUAWEI_OTA): ========================================
I (HUAWEI_OTA): ✅ OTA SUCCESS! Total: 1048576 bytes
I (HUAWEI_OTA): 📌 Next boot: ota_1 (slot 1)
I (HUAWEI_OTA): 📌 Rebooting in 1 second...
I (HUAWEI_OTA): ========================================
```

---

### 失败情况
```
I (HUAWEI_OTA): ❌ HTTP read error at 500000/1048576 bytes
I (HUAWEI_OTA): ❌ OTA image too small: 1024 bytes
I (HUAWEI_OTA): ❌ esp_ota_end failed: ESP_ERR_OTA_VALIDATE_FAILED
I (HUAWEI_OTA): 🔧 OTA cleanup: closing HTTP connection
```

---

## 📊 日志符号说明

| 符号 | 含义 | 用途 |
|------|------|------|
| 🚀 | 启动/开始 | OTA 启动、任务创建 |
| 📋 | 信息/列表 | 任务信息、配置信息 |
| 📊 | 数据/统计 | 堆内存、进度统计 |
| 📌 | 标记/重点 | 关键参数、配置值 |
| 📤 | 上传 | 文件上传操作 |
| ⬇️ | 下载 | 文件/固件下载 |
| 📈 | 进度 | 进度百分比 |
| ✅ | 成功 | 操作成功 |
| ❌ | 失败/错误 | 操作失败 |
| ⚠️ | 警告 | 异常状态 |
| 🔧 | 配置/清理 | 清理操作 |

---

## 🎯 监控指标

### 1. 堆内存监控
```
指标：Free / Largest block / SPIRAM

标准值：
- 启动时：>160KB 可用
- WiFi 后：>150KB 可用
- OTA 中：>100KB 可用（最小）
- 上传中：>100KB 可用（最小）

⚠️ 警告值：<80KB → 可能导致 malloc() 失败
```

### 2. 任务优先级
```
优先级 1（最高）: 中断处理
优先级 3: huawei_ota_task（OTA 下载）
优先级 4: usb_copy_task（USB 复制）
优先级 5: file_task_worker（文件操作）
优先级 ∞（最低）: idle 任务
```

### 3. 任务栈
```
file_task_worker: 6 KB （优化后）
usb_copy_task: 2 KB
huawei_ota_task: 6 KB （优化后）
lwip/mqtt_task: 6 KB（系统）
```

---

## 🔍 故障排查

### 问题 1: 上传卡死
```
日志显示：
I (MAIN): 📤 UPLOAD: xxx
I (MAIN):    Heap before: 50000 bytes    ← ⚠️ 堆太少！

解决：
- 检查是否有其他任务消耗堆
- 减小 OTA HTTP 缓冲
- 增加总堆大小（menuconfig）
```

### 问题 2: OTA 失败
```
日志显示：
I (HUAWEI_OTA): ❌ HTTP read error at 500000/1048576 bytes

原因分析：
- 网络连接不稳定
- URL 过期或无效
- HTTP 缓冲不足

检查日志：
- 查看 HTTP status 码
- 查看 Content-Length
- 查看堆是否足够
```

### 问题 3: 任务抢占
```
日志显示：
I (MAIN): 📤 UPLOAD started
I (HUAWEI_OTA): ⭐ OTA TASK STARTED (Priority: 5)  ← 优先级太高！
I (MAIN): ✅ Upload completed (晚于 OTA)

解决：
- 确保 OTA 优先级 < 文件操作优先级
- 当前已优化：OTA=3, 文件操作=5 ✅
```

---

## 📝 日志输出示例

### 完整的成功流程
```
启动阶段：
═══════════════════════════════════════
📋 TASK CREATION
...
✅ WiFi Connected
🚀 Starting main event loop...
═══════════════════════════════════════

文件上传阶段：
───────────────────────────────
📥 Message received
📤 UPLOAD: data.bin
✅ Upload completed
───────────────────────────────

OTA 下载阶段：
═══════════════════════════════════════
🚀 OTA START REQUEST
   Heap: 145000 bytes
✅ OTA task created (Priority: 3)
⭐ OTA TASK STARTED
📌 HTTP status: 200, Content-Length: 1048576
⬇️  Starting firmware download
📈 OTA Progress: 5% (52428/1048576 bytes)
...
📈 OTA Progress: 100% (1048576/1048576 bytes)
✅ OTA SUCCESS! Total: 1048576 bytes
═══════════════════════════════════════
```

---

## 🚀 运行编译和测试

1. **编译**：
```bash
idf.py build
```

2. **烧写**：
```bash
idf.py flash
```

3. **监控日志**：
```bash
idf.py monitor
```

4. **滤波日志** (只看关键日志)：
```bash
idf.py monitor | grep -E "✅|❌|📊|OTA|UPLOAD"
```

---

## 📞 常见问题

**Q: 日志太多怎么办？**
A: 使用 `idf.py monitor` 时可以按 `Ctrl+A Ctrl+L` 清屏，或在菜单中配置日志级别

**Q: 如何保存日志到文件？**
A: `idf.py monitor | tee logfile.txt`

**Q: 优先级 3 会影响其他任务吗？**
A: 不会。OTA 优先级 3，低于系统任务和文件操作，不会影响正常运行

---

**最后修改**: 2026年2月7日
