#include "nfc_manager.h"

static const char* TAG = "NfcManager";

NfcManager::NfcManager() {
    spi_handle_ = nullptr;
    spi_host_ = RC522_DEFAULT_SPI_HOST;
    initialized_ = false;
    detecting_ = false;
    detection_interval_ms_ = 500;
    card_detected_callback_ = nullptr;
    detection_task_handle_ = nullptr;
    cooldown_time_ms_ = 3000;

#if CONFIG_NFC_COOLDOWN_TIME
    cooldown_time_ms_ = CONFIG_NFC_COOLDOWN_TIME;
#endif
}

NfcManager::~NfcManager() {
    StopDetection();
    if (spi_handle_ != nullptr) {
        spi_bus_remove_device(spi_handle_);
        spi_bus_free(spi_host_);
        spi_handle_ = nullptr;
    }
}

bool NfcManager::Initialize(spi_host_device_t spi_host) {
    if (initialized_) {
        return true;
    }
    
    if (!InitSPI(spi_host)) {
        ESP_LOGE(TAG, "Failed to initialize SPI");
        return false;
    }
    
    if (!InitRC522()) {
        ESP_LOGE(TAG, "Failed to initialize RC522");
        spi_bus_remove_device(spi_handle_);
        spi_bus_free(spi_host_);
        spi_handle_ = nullptr;
        return false;
    }
    
    initialized_ = true;
    ESP_LOGI(TAG, "NFC Manager initialized successfully");
    return true;
}

bool NfcManager::InitSPI(spi_host_device_t spi_host) {
    spi_host_ = spi_host;
    
    // 配置SPI总线
    spi_bus_config_t bus_config = {
        .mosi_io_num = RC522_MOSI_GPIO,
        .miso_io_num = RC522_MISO_GPIO,
        .sclk_io_num = RC522_SCK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0,
        .flags = 0,
        .intr_flags = 0
    };
    
    // 初始化SPI总线
    esp_err_t ret = spi_bus_initialize(spi_host_, &bus_config, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return false;
    }
    
    // 配置SPI设备
    spi_device_interface_config_t dev_config = {
        .command_bits = 0,
        .address_bits = 8,
        .dummy_bits = 0,
        .mode = 0,
        .duty_cycle_pos = 128,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .clock_speed_hz = 1000000,  // 1MHz
        .input_delay_ns = 0,
        .spics_io_num = RC522_SPI_CS_GPIO,
        .flags = 0,
        .queue_size = 1,
        .pre_cb = NULL,
        .post_cb = NULL
    };
    
    ret = spi_bus_add_device(spi_host_, &dev_config, &spi_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
        spi_bus_free(spi_host_);
        return false;
    }
    
    // 配置RST引脚
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RC522_RST_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure RST pin: %s", esp_err_to_name(ret));
        spi_bus_remove_device(spi_handle_);
        spi_bus_free(spi_host_);
        return false;
    }
    
    // 初始状态CS和RST都为高电平
    gpio_set_level(RC522_RST_GPIO, 1);
    
    return true;
}

bool NfcManager::InitRC522() {
    // 复位RC522
    gpio_set_level(RC522_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(RC522_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(RC522_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // 软复位
    WriteRegister(MFRC522_REG_COMMAND, MFRC522_CMD_SOFT_RESET);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // 设置定时器
    WriteRegister(MFRC522_REG_TIMER_MODE, 0x8D);       // TAuto=1; timer starts automatically at the end of the transmission
    WriteRegister(MFRC522_REG_TIMER_PRESCALER, 0x3E);  // TPreScaler = TModeReg[3..0]:TPrescalerReg, 
    WriteRegister(MFRC522_REG_TIMER_RELOAD_L, 30);     // Reload timer with 30
    WriteRegister(MFRC522_REG_TIMER_RELOAD_H, 0);
    
    WriteRegister(MFRC522_REG_TX_AUTO, 0x40);          // 100%ASK
    WriteRegister(MFRC522_REG_MODE, 0x3D);             // CRC initial value 0x6363
    
    // 开启天线
    uint8_t value = ReadRegister(MFRC522_REG_TX_CONTROL);
    if ((value & 0x03) != 0x03) {
        WriteRegister(MFRC522_REG_TX_CONTROL, value | 0x03);
    }
    
    return true;
}

void NfcManager::WriteRegister(uint8_t reg, uint8_t val) {
    uint8_t addr = (reg << 1) & 0x7E;  // 写操作最低位为0
    
    spi_transaction_t t = {
        .flags = 0,
        .cmd = 0,
        .addr = addr,
        .length = 8,  // 8 bits
        .rxlength = 0,
        .user = NULL,
        .tx_buffer = &val,
        .rx_buffer = NULL
    };
    
    esp_err_t ret = spi_device_polling_transmit(spi_handle_, &t);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write register 0x%02x: %s", reg, esp_err_to_name(ret));
    }
}

uint8_t NfcManager::ReadRegister(uint8_t reg) {
    uint8_t addr = ((reg << 1) & 0x7E) | 0x80;  // 读操作最低位为1
    uint8_t val = 0;
    
    spi_transaction_t t = {
        .flags = 0,
        .cmd = 0,
        .addr = addr,
        .length = 8,  // 8 bits
        .rxlength = 8,
        .user = NULL,
        .tx_buffer = NULL,
        .rx_buffer = &val
    };
    
    esp_err_t ret = spi_device_polling_transmit(spi_handle_, &t);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read register 0x%02x: %s", reg, esp_err_to_name(ret));
        return 0;
    }
    
    return val;
}

void NfcManager::SetBitMask(uint8_t reg, uint8_t mask) {
    uint8_t value = ReadRegister(reg);
    WriteRegister(reg, value | mask);
}

void NfcManager::ClearBitMask(uint8_t reg, uint8_t mask) {
    uint8_t value = ReadRegister(reg);
    WriteRegister(reg, value & (~mask));
}

bool NfcManager::Transceive(uint8_t* send_data, uint8_t send_len, uint8_t* back_data, uint8_t* back_len) {
    uint8_t irq_en = 0x77;
    
    WriteRegister(MFRC522_REG_COM_IEN, irq_en | 0x80);
    ClearBitMask(MFRC522_REG_COM_IRQ, 0x80);
    WriteRegister(MFRC522_REG_COMMAND, MFRC522_CMD_IDLE);
    SetBitMask(MFRC522_REG_FIFO_LEVEL, 0x80);  // 清空FIFO
    
    for (int i = 0; i < send_len; i++) {
        WriteRegister(MFRC522_REG_FIFO_DATA, send_data[i]);
    }
    
    WriteRegister(MFRC522_REG_COMMAND, MFRC522_CMD_TRANSCEIVE);
    SetBitMask(MFRC522_REG_BIT_FRAMING, 0x80);  // 开始发送
    
    // 等待数据发送完成
    const uint8_t max_iterations = 20;
    uint8_t n = 0;
    do {
        uint8_t irq = ReadRegister(MFRC522_REG_COM_IRQ);
        n++;
        if (irq & 0x30) {  // 0x30为RxIRq和IdleIRq
            break;
        }
        if (irq & 0x01) {  // 超时了
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    } while (n < max_iterations);
    
    // 清除开始位
    ClearBitMask(MFRC522_REG_BIT_FRAMING, 0x80);
    
    if (n >= max_iterations) {
        ESP_LOGE(TAG, "Transceive timeout after maximum iterations");
        return false;
    }
    
    uint8_t error = ReadRegister(MFRC522_REG_COM_IRQ) & 0x1B;
    if (error) {
        ESP_LOGE(TAG, "Communication error: 0x%02x", error);
        return false;
    }
    
    // 读取接收到的数据
    uint8_t received = ReadRegister(MFRC522_REG_FIFO_LEVEL);
    if (received > *back_len) {
        ESP_LOGE(TAG, "Receive buffer too small: needed %d, available %d", received, *back_len);
        return false;
    }
    
    *back_len = received;
    for (int i = 0; i < received; i++) {
        back_data[i] = ReadRegister(MFRC522_REG_FIFO_DATA);
    }
    
    return true;
}

bool NfcManager::DetectCard(NfcCard& card) {
    uint8_t buffer[9];
    uint8_t buffer_size = sizeof(buffer);
    
    // 发送REQA命令
    buffer[0] = 0x26;  // REQA命令
    ClearBitMask(MFRC522_REG_COM_IRQ, 0x80);  // 清除MFCrypto1On位
    WriteRegister(MFRC522_REG_BIT_FRAMING, 0x07);  // 发送的最后一字节的位数=7
    SetBitMask(MFRC522_REG_TX_CONTROL, 0x03);  // 确保天线已开启
    
    bool success = Transceive(buffer, 1, buffer, &buffer_size);
    if (!success || buffer_size != 2) {
        return false;  // 没有检测到卡片
    }
    
    // 现在读取卡片序列号
    buffer[0] = 0x93;  // 防冲突命令
    buffer[1] = 0x20;  // 位指示器
    
    buffer_size = sizeof(buffer);
    success = Transceive(buffer, 2, buffer, &buffer_size);
    if (!success || buffer_size < 5) {
        return false;  // 无法读取序列号
    }
    
    // 卡片UID在buffer的5个字节中，我们计算并校验BCC
    uint8_t bcc_calc = buffer[0] ^ buffer[1] ^ buffer[2] ^ buffer[3];
    if (bcc_calc != buffer[4]) {
        ESP_LOGE(TAG, "BCC check failed: %02x != %02x", bcc_calc, buffer[4]);
        return false;  // BCC校验失败
    }
    
    // 复制UID数据
    for (int i = 0; i < 4; i++) {
        card.raw_uid[i] = buffer[i];
    }
    card.uid_len = 4;
    card.card_type = "ISO14443A";
    card.uid = BytesToHexString(card.raw_uid, card.uid_len);
    
    ESP_LOGI(TAG, "Card detected, UID: %s", card.uid.c_str());
    return true;
}

void NfcManager::StartDetection(int interval_ms) {
    if (detecting_ || !initialized_) {
        return;
    }
    
    detection_interval_ms_ = interval_ms;
    detecting_ = true;
    
    // 创建检测任务
    xTaskCreate(DetectionTask, "nfc_detection", 4096, this, 5, &detection_task_handle_);
    
    ESP_LOGI(TAG, "NFC detection started, interval: %d ms", interval_ms);
}

void NfcManager::StopDetection() {
    if (!detecting_) {
        return;
    }
    
    detecting_ = false;
    
    // 等待任务结束
    if (detection_task_handle_ != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(detection_interval_ms_ + 50));
        vTaskDelete(detection_task_handle_);
        detection_task_handle_ = nullptr;
    }
    
    ESP_LOGI(TAG, "NFC detection stopped");
}

void NfcManager::DetectionTask(void* params) {
    NfcManager* manager = static_cast<NfcManager*>(params);
    
    while (manager->detecting_) {
        NfcCard card;
        
        if (manager->DetectCard(card)) {
            // 检查冷却期
            if (manager->CheckCooldown(card.uid)) {
                // 调用回调函数
                std::lock_guard<std::mutex> lock(manager->callback_mutex_);
                if (manager->card_detected_callback_) {
                    manager->card_detected_callback_(card);
                }
            }
        }
        
        // 等待一段时间再次检测
        vTaskDelay(pdMS_TO_TICKS(manager->detection_interval_ms_));
    }
    
    vTaskDelete(nullptr);
}

bool NfcManager::CheckCooldown(const std::string& uid) {
    std::lock_guard<std::mutex> lock(cooldown_mutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto it = last_detected_cards_.find(uid);
    
    if (it != last_detected_cards_.end()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count();
        
        if (elapsed < cooldown_time_ms_) {
            // 在冷却期内，忽略这次检测
            return false;
        }
    }
    
    // 更新时间戳
    last_detected_cards_[uid] = now;
    return true;
}

std::string NfcManager::BytesToHexString(const uint8_t* data, uint8_t length) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(length * 2);
    
    for (uint8_t i = 0; i < length; i++) {
        result.push_back(hex[(data[i] >> 4) & 0xF]);
        result.push_back(hex[data[i] & 0xF]);
    }
    
    return result;
} 