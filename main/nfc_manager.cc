#include "nfc_manager.h"
#include <esp_log.h>
#include <chrono>

#define TAG "NfcManager"

NfcManager::NfcManager() {
    event_group_ = xEventGroupCreate();
    mutex_ = xSemaphoreCreateMutex();
}

NfcManager::~NfcManager() {
    stopDetection();
    if (rc522_ != nullptr) {
        rc522_->end();
        delete rc522_;
        rc522_ = nullptr;
    }
    
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
    
    if (event_group_ != nullptr) {
        vEventGroupDelete(event_group_);
        event_group_ = nullptr;
    }
}

bool NfcManager::init(gpio_num_t sda, gpio_num_t sck, gpio_num_t mosi, gpio_num_t miso, gpio_num_t rst) {
    if (rc522_ != nullptr) {
        // 已经初始化过
        return true;
    }
    
    // 创建RC522实例
    rc522_ = new RC522(sda, sck, mosi, miso, rst);
    if (rc522_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create RC522 instance");
        return false;
    }
    
    // 初始化RC522
    if (!rc522_->init()) {
        ESP_LOGE(TAG, "Failed to initialize RC522");
        delete rc522_;
        rc522_ = nullptr;
        return false;
    }
    
    ESP_LOGI(TAG, "NFC manager initialized successfully");
    return true;
}

void NfcManager::startDetection() {
    if (is_running_ || rc522_ == nullptr) {
        return;
    }
    
    // 重置停止标志
    xEventGroupClearBits(event_group_, NFC_EVENT_STOP_BIT);
    is_running_ = true;
    
    // 创建检测任务
    BaseType_t ret = xTaskCreate(detectionTask, "nfc_detection", NFC_TASK_STACK_SIZE, this, NFC_TASK_PRIORITY, &detection_task_handle_);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create NFC detection task");
        is_running_ = false;
        return;
    }
    
    ESP_LOGI(TAG, "NFC card detection started");
}

void NfcManager::stopDetection() {
    if (!is_running_) {
        return;
    }
    
    // 设置停止标志
    xEventGroupSetBits(event_group_, NFC_EVENT_STOP_BIT);
    
    // 等待任务结束
    if (detection_task_handle_ != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(100)); // 给任务一些时间来检测停止标志
        vTaskDelete(detection_task_handle_);
        detection_task_handle_ = nullptr;
    }
    
    is_running_ = false;
    ESP_LOGI(TAG, "NFC card detection stopped");
}

void NfcManager::setCardDetectedCallback(std::function<void(const std::string&)> callback) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    card_detected_callback_ = callback;
    xSemaphoreGive(mutex_);
}

void NfcManager::detectionTask(void* arg) {
    NfcManager* nfc_manager = static_cast<NfcManager*>(arg);
    nfc_manager->detectionLoop();
    vTaskDelete(NULL);
}

void NfcManager::detectionLoop() {
    std::chrono::steady_clock::time_point last_detection_time;
    bool first_detection = true;
    
    while (true) {
        // 检查是否要停止
        if (xEventGroupGetBits(event_group_) & NFC_EVENT_STOP_BIT) {
            break;
        }
        
        // 检查是否有卡片
        if (rc522_->isCardPresent()) {
            std::string card_id = rc522_->getCardIdString();
            if (!card_id.empty()) {
                auto now = std::chrono::steady_clock::now();
                
                // 检查是否是新卡片或者是同一张卡但已经超过防抖时间
                if (first_detection || 
                    card_id != last_card_id_ || 
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - last_detection_time).count() > debounce_time_ms_) {
                    
                    ESP_LOGI(TAG, "Card detected: %s", card_id.c_str());
                    
                    // 调用回调函数
                    xSemaphoreTake(mutex_, portMAX_DELAY);
                    if (card_detected_callback_) {
                        card_detected_callback_(card_id);
                    }
                    xSemaphoreGive(mutex_);
                    
                    // 更新状态
                    last_card_id_ = card_id;
                    last_detection_time = now;
                    first_detection = false;
                }
            }
        }
        
        // 等待一段时间再检测
        vTaskDelay(pdMS_TO_TICKS(100));
    }
} 