#ifndef NFC_MANAGER_H
#define NFC_MANAGER_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <string>
#include <functional>
#include <map>
#include <mutex>
#include <chrono>

// RC522 引脚定义
#define RC522_SPI_CS_GPIO    GPIO_NUM_35   // SDA (原来为GPIO0，现在改为GPIO35)
#define RC522_SCK_GPIO       GPIO_NUM_45   // SCK
#define RC522_MOSI_GPIO      GPIO_NUM_21   // MOSI
#define RC522_MISO_GPIO      GPIO_NUM_20   // MISO
#define RC522_RST_GPIO       GPIO_NUM_19   // RST

// 默认使用的SPI主机 - 使用SPI3_HOST避免与LCD显示器冲突
#define RC522_DEFAULT_SPI_HOST SPI3_HOST

// RC522 寄存器定义
#define MFRC522_REG_COMMAND        0x01
#define MFRC522_REG_COM_IEN        0x02
#define MFRC522_REG_FIFO_LEVEL     0x0A
#define MFRC522_REG_FIFO_DATA      0x09
#define MFRC522_REG_COM_IRQ        0x04
#define MFRC522_REG_BIT_FRAMING    0x0D
#define MFRC522_REG_MODE           0x11
#define MFRC522_REG_TX_CONTROL     0x14
#define MFRC522_REG_TX_AUTO        0x15
#define MFRC522_REG_CRC_RESULT_H   0x21
#define MFRC522_REG_CRC_RESULT_L   0x22
#define MFRC522_REG_TIMER_MODE     0x2A
#define MFRC522_REG_TIMER_PRESCALER 0x2B
#define MFRC522_REG_TIMER_RELOAD_H 0x2C
#define MFRC522_REG_TIMER_RELOAD_L 0x2D

// RC522 命令定义
#define MFRC522_CMD_IDLE           0x00
#define MFRC522_CMD_MEM            0x01
#define MFRC522_CMD_GENERATE_RANDOM_ID 0x02
#define MFRC522_CMD_CALC_CRC       0x03
#define MFRC522_CMD_TRANSMIT       0x04
#define MFRC522_CMD_NO_CMD_CHANGE  0x07
#define MFRC522_CMD_RECEIVE        0x08
#define MFRC522_CMD_TRANSCEIVE     0x0C
#define MFRC522_CMD_SOFT_RESET     0x0F

// NFC卡片结构体
struct NfcCard {
    std::string uid;        // 卡片UID
    std::string card_type;  // 卡片类型
    uint8_t raw_uid[10];    // 原始UID数据
    uint8_t uid_len;        // UID长度
};

class NfcManager {
public:
    static NfcManager& GetInstance() {
        static NfcManager instance;
        return instance;
    }

    // 禁用拷贝构造函数和赋值运算符
    NfcManager(const NfcManager&) = delete;
    NfcManager& operator=(const NfcManager&) = delete;

    // 初始化NFC管理器
    bool Initialize(spi_host_device_t spi_host = RC522_DEFAULT_SPI_HOST);
    
    // 启动NFC卡片检测
    void StartDetection(int interval_ms = 500);
    
    // 停止NFC卡片检测
    void StopDetection();
    
    // 设置卡片检测回调函数
    void SetCardDetectedCallback(std::function<void(const NfcCard&)> callback) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        card_detected_callback_ = callback;
    }

private:
    NfcManager();
    ~NfcManager();

    // 初始化SPI
    bool InitSPI(spi_host_device_t spi_host);
    
    // 初始化RC522模块
    bool InitRC522();
    
    // 写RC522寄存器
    void WriteRegister(uint8_t reg, uint8_t val);
    
    // 读RC522寄存器
    uint8_t ReadRegister(uint8_t reg);
    
    // 修改寄存器位
    void SetBitMask(uint8_t reg, uint8_t mask);
    
    // 清除寄存器位
    void ClearBitMask(uint8_t reg, uint8_t mask);
    
    // 将数据发送到RC522并接收应答
    bool Transceive(uint8_t* send_data, uint8_t send_len, uint8_t* back_data, uint8_t* back_len);
    
    // 检测卡片
    bool DetectCard(NfcCard& card);
    
    // 卡片检测任务
    static void DetectionTask(void* params);
    
    // 防重复读取的冷却期检查
    bool CheckCooldown(const std::string& uid);
    
    // 将字节数组转换为十六进制字符串
    static std::string BytesToHexString(const uint8_t* data, uint8_t length);
    
    // 成员变量 - 必须与NfcManager::NfcManager()构造函数的初始化顺序一致
    spi_device_handle_t spi_handle_;
    spi_host_device_t spi_host_;
    bool initialized_;
    bool detecting_;
    int detection_interval_ms_;
    std::function<void(const NfcCard&)> card_detected_callback_;
    TaskHandle_t detection_task_handle_;
    int cooldown_time_ms_;
    
    // 额外没有在构造函数初始化的成员
    std::mutex callback_mutex_;
    std::map<std::string, std::chrono::steady_clock::time_point> last_detected_cards_;
    std::mutex cooldown_mutex_;
};

#endif // NFC_MANAGER_H 