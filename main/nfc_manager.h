#ifndef _NFC_MANAGER_H_
#define _NFC_MANAGER_H_

#include "components/rc522/rc522.h"
#include <functional>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>

#define NFC_TASK_STACK_SIZE (4 * 1024)
#define NFC_TASK_PRIORITY 5
#define NFC_EVENT_STOP_BIT (1 << 0)

class NfcManager {
public:
    /**
     * 获取NfcManager单例
     */
    static NfcManager& GetInstance() {
        static NfcManager instance;
        return instance;
    }

    /**
     * 初始化NFC管理器
     * 
     * @param sda SPI片选引脚（对应RC522的SDA/NSS）
     * @param sck SPI时钟引脚
     * @param mosi SPI主出从入引脚
     * @param miso SPI主入从出引脚
     * @param rst 复位引脚
     * @return 是否初始化成功
     */
    bool init(gpio_num_t sda, gpio_num_t sck, gpio_num_t mosi, gpio_num_t miso, gpio_num_t rst);

    /**
     * 开始卡片检测
     */
    void startDetection();

    /**
     * 停止卡片检测
     */
    void stopDetection();

    /**
     * 设置卡片检测回调函数
     * 
     * @param callback 当检测到卡片时调用的回调函数，参数为卡片ID字符串
     */
    void setCardDetectedCallback(std::function<void(const std::string&)> callback);

    /**
     * 销毁NFC管理器
     */
    ~NfcManager();

private:
    NfcManager();
    // 删除拷贝构造函数和赋值运算符
    NfcManager(const NfcManager&) = delete;
    NfcManager& operator=(const NfcManager&) = delete;

    RC522* rc522_ = nullptr;
    TaskHandle_t detection_task_handle_ = nullptr;
    EventGroupHandle_t event_group_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    std::function<void(const std::string&)> card_detected_callback_;
    bool is_running_ = false;
    std::string last_card_id_;
    uint32_t debounce_time_ms_ = 3000; // 3秒内不重复触发同一张卡

    static void detectionTask(void* arg);
    void detectionLoop();
};

#endif // _NFC_MANAGER_H_ 