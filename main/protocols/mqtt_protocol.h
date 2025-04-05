#ifndef MQTT_PROTOCOL_H
#define MQTT_PROTOCOL_H

#include "protocol.h"

#include <mqtt_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <string>

#define MQTT_PROTOCOL_CONNECTED_EVENT (1 << 0)
#define MQTT_PROTOCOL_SESSION_ID_EVENT (1 << 1)
#define MQTT_PROTOCOL_AUDIO_CHANNEL_READY_EVENT (1 << 2)
#define MQTT_PROTOCOL_UDP_CONNECTED_EVENT (1 << 3)

#define UDP_PACKET_TIMEOUT_MS 500

class MqttProtocol : public Protocol {
public:
    MqttProtocol();
    ~MqttProtocol();

    void Start() override;
    void SendAudio(const std::vector<uint8_t>& data) override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel() override;
    bool IsAudioChannelOpened() const override;
    
    // IoT 相关
    void SendIotDescriptors(const std::string& descriptors) override;
    void SendIotStates(const std::string& states) override;
    
    // NFC卡片检测
    void SendNfcCardDetected(const std::string& card_id) override;

private:
    esp_mqtt_client_handle_t mqtt_client_ = nullptr;
    EventGroupHandle_t event_group_handle_ = nullptr;
    QueueHandle_t udp_message_queue_ = nullptr;
    TaskHandle_t udp_receive_task_handle_ = nullptr;
    int udp_socket_ = -1;
    int udp_audio_port_ = 0;

    bool init_failed_ = false;
    bool connecting_ = false;
    bool connected_ = false;
    bool udp_connected_ = false;
    struct sockaddr_in udp_server_addr_;

    void SendMqttMessage(const std::string& topic, const std::string& message);
    void SendText(const std::string& text) override;
    void StartUdpReceiveThread();
    void StopUdpReceiveThread();
    bool ConnectUdp();
    bool DisconnectUdp();
    void CloseUdpSocket();
    void UdpReceiveLoop();

    static void UdpReceiveThread(void* arg);
    static void MqttEventHandler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
};

#endif
