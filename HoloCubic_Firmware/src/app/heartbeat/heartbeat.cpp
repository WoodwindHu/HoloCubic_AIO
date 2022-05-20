#include "heartbeat.h"
#include "heartbeat_gui.h"
#include "sys/app_controller.h"
#include "common.h"
#include "network.h"
#include <PubSubClient.h>

#define HEARTBEAT_APP_NAME "Heartbeat"

#define HEARTBEAT_CONFIG_PATH "/heartbeat.cfg"


extern AppController *app_controller; // APP控制器

// 常驻数据，可以不随APP的生命周期而释放或删除
struct HeartbeatAppForeverData
{
    char subtopic[128]; // "/beat"
    char pubtopic[128]; // "/heart"
    char client_id[128];  // "hc_heart"
    char username[128];
    char passwd[128];
    char mqtt_server_domain[128] = {0};
    IPAddress mqtt_server_ip; 
    WiFiClient espClient; // 定义wifiClient实例
    PubSubClient *mqtt_client = NULL; //(mqtt_server_ip, 1883, callback, espClient);
    static void callback(char* topic, byte* payload, unsigned int length);
    void mqtt_reconnect();
};

void HeartbeatAppForeverData::callback(char* topic, byte* payload, unsigned int length) 
{
    Serial.print("Message arrived [");
    Serial.print(topic);   // 打印主题信息
    Serial.print("] ");
    for (int i = 0; i < length; i++) {
        Serial.print((char)payload[i]); // 打印主题内容
    }
    Serial.println();

    app_controller->send_to("Heartbeat", CTRL_NAME, APP_MESSAGE_MQTT_DATA, NULL, NULL);
}

HeartbeatAppForeverData hb_cfg;


static void write_config(HeartbeatAppForeverData *cfg)
{
    char tmp[128];
    String w_data;
    if (strlen(cfg->mqtt_server_domain) > 0)
    {
        memset(tmp, 0, 128);
        snprintf(tmp, 128, "%s\n", cfg->mqtt_server_domain);
        w_data += tmp;
    }
    else 
    {
        w_data = w_data + cfg->mqtt_server_ip.toString() + "\n";
    }
    memset(tmp, 0, 128);
    snprintf(tmp, 128, "%s\n", cfg->subtopic);
    w_data += tmp;
    memset(tmp, 0, 128);
    snprintf(tmp, 128, "%s\n", cfg->pubtopic);
    w_data += tmp;
    memset(tmp, 0, 128);
    snprintf(tmp, 128, "%s\n", cfg->client_id);
    w_data += tmp;
    memset(tmp, 0, 128);
    snprintf(tmp, 128, "%s\n", cfg->username);
    w_data += tmp;
    memset(tmp, 0, 128);
    snprintf(tmp, 128, "%s\n", cfg->passwd);
    w_data += tmp;
    g_flashCfg.writeFile(HEARTBEAT_CONFIG_PATH, w_data.c_str());
}




static void read_config(HeartbeatAppForeverData *cfg)
{
    // 如果有需要持久化配置文件 可以调用此函数将数据存在flash中
    // 配置文件名最好以APP名为开头 以".cfg"结尾，以免多个APP读取混乱
    char info[1024] = {0};
    uint16_t size = g_flashCfg.readFile(HEARTBEAT_CONFIG_PATH, (uint8_t *)info);
    Serial.printf("size %d\n", size);
    info[size] = 0;
    if (size == 0)
    {
        // 设置了mqtt服务器才能运行！
        Serial.println("Please config first!");
    }
    else
    {
        // 解析数据
        char *param[6] = {0};
        analyseParam(info, 6, param);
        if (cfg->mqtt_server_ip.fromString(param[0]))
        {
            Serial.printf("mqtt_server_ip %s", cfg->mqtt_server_ip.toString().c_str());
            Serial.println();
            cfg->mqtt_server_domain[0] = 0;
        }
        else 
        {
            strcpy(cfg->mqtt_server_domain, param[0]);
            Serial.printf("mqtt_server_domain %s", cfg->mqtt_server_domain);
            Serial.println();
        }
        strcpy(cfg->subtopic,param[1]);
        Serial.printf("subtopic %s", cfg->subtopic);
        Serial.println();
        strcpy(cfg->pubtopic,param[2]);
        Serial.printf("pubtopic %s", cfg->pubtopic);
        Serial.println();
        strcpy(cfg->client_id, param[3]);
        Serial.printf("client_id %s", cfg->client_id);
        Serial.println();
        strcpy(cfg->username, param[4]);
        Serial.printf("username %s", cfg->username);
        Serial.println();
        strcpy(cfg->passwd, param[5]);
        Serial.printf("password %s", cfg->passwd);
        Serial.println();
        if (cfg->mqtt_client == NULL)
        {
            if (strlen(cfg->mqtt_server_domain) > 0)
            {
                cfg->mqtt_client = new PubSubClient(cfg->mqtt_server_domain, 1883, cfg->callback, cfg->espClient);
            }
            else
            {
                cfg->mqtt_client = new PubSubClient(cfg->mqtt_server_ip, 1883, cfg->callback, cfg->espClient);
            }
            cfg->mqtt_client->setKeepAlive(60);
        }
    }
}


void HeartbeatAppForeverData::mqtt_reconnect() 
{
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if ((strlen(username) > 0) ? mqtt_client->connect(client_id, username, passwd) : mqtt_client->connect(client_id)) {
        Serial.println("mqtt connected");
        // 连接成功时订阅主题
        mqtt_client->subscribe(subtopic);
        Serial.printf("%s subcribed", subtopic);
        Serial.println();
        app_controller->set_mqtt_status(1);
    } 
    else 
    {
        Serial.print("failed, rc=");
        Serial.print(mqtt_client->state());
        Serial.println();
        app_controller->set_mqtt_status(0);
    }
}

// 动态数据，APP的生命周期结束也需要释放它
struct HeartbeatAppRunData
{
    uint8_t send_cnt = 0;
    uint8_t recv_cnt = 0;
    uint8_t rgb_flag = 0; 
    unsigned long preTimestamp; 
    RgbParam rgb_setting; // rgb参数
    struct tm timeInfo; //声明一个结构体
};


// 保存APP运行时的参数信息，理论上关闭APP时推荐在 xxx_exit_callback 中释放掉
static HeartbeatAppRunData *run_data = NULL;

// 当然你也可以添加恒定在内存中的少量变量（退出时不用释放，实现第二次启动时可以读取）


static int heartbeat_init(void)
{
    // 获取配置参数
    read_config(&hb_cfg);  // 已经读过了
    heartbeat_gui_init();
    // 初始化运行时参数
    run_data = (HeartbeatAppRunData *)calloc(1, sizeof(HeartbeatAppRunData));
    run_data->send_cnt = 0;
    run_data->recv_cnt = 0;
    run_data->rgb_flag = 0;
}

void heartbeat_rgb()
{
    if (run_data->rgb_flag == 0) 
    {
        run_data->rgb_flag = 1;
        // 初始化RGB灯 HSV色彩模式
        run_data->rgb_setting = {LED_MODE_HSV,
                                app_controller->rgb_cfg.min_value_0, app_controller->rgb_cfg.min_value_1, app_controller->rgb_cfg.min_value_2,
                                app_controller->rgb_cfg.max_value_0, app_controller->rgb_cfg.max_value_1, app_controller->rgb_cfg.max_value_2,
                                0,0,0,
                                0.01, 0.95,
                                0.05, 10};
        set_rgb(&(run_data->rgb_setting));
        run_data->preTimestamp = millis();
    }
    else 
    {
        run_data->preTimestamp = millis();
    }
}

void heartbeat_rgb_reset()
{
    run_data->rgb_flag = 0;
    RgbConfig *rgb_cfg = &app_controller->rgb_cfg;
    // 初始化RGB灯 HSV色彩模式
    Serial.printf("rgb_cfg time %d", app_controller->rgb_cfg.time);
    Serial.println();
    RgbParam rgb_setting = {LED_MODE_HSV,
                            app_controller->rgb_cfg.min_value_0, app_controller->rgb_cfg.min_value_1, app_controller->rgb_cfg.min_value_2,
                            app_controller->rgb_cfg.max_value_0, app_controller->rgb_cfg.max_value_1, app_controller->rgb_cfg.max_value_2,
                            app_controller->rgb_cfg.step_0, app_controller->rgb_cfg.step_1, app_controller->rgb_cfg.step_2,
                            app_controller->rgb_cfg.min_brightness, app_controller->rgb_cfg.max_brightness,
                            app_controller->rgb_cfg.brightness_step, app_controller->rgb_cfg.time};
    set_rgb(&rgb_setting);
}

static void heartbeat_process(AppController *sys,
                            const ImuAction *act_info)
{
    if (run_data->rgb_flag == 1 && doDelayMillisTime(3000, &(run_data->preTimestamp), false))
    {
        heartbeat_rgb_reset();
    }
    lv_scr_load_anim_t anim_type = LV_SCR_LOAD_ANIM_NONE;
    if (RETURN == act_info->active)
    {
        sys->app_exit(); // 退出APP
        return;
    }
    else if (GO_FORWORD == act_info->active) // 向前按发送一条消息
    {
        // anim_type = LV_SCR_LOAD_ANIM_MOVE_TOP;
        if (hb_cfg.mqtt_client->state() == MQTT_CONNECTED) 
        {
            run_data->send_cnt += 1;
            hb_cfg.mqtt_client->publish(hb_cfg.pubtopic, MQTT_SEND_MSG);
            Serial.printf("sent publish %s successful", hb_cfg.pubtopic); 
            Serial.println();
            // 发送指示灯
            heartbeat_rgb();
        }
    }
    if (run_data->recv_cnt > 0 && run_data->send_cnt > 0) 
    {
        heartbeat_set_sr_type(HEART);
    }
    else if (run_data->recv_cnt > 0)
    {
        heartbeat_set_sr_type(RECV);
    }
    else if (run_data->send_cnt == 0) // 进入app时自动发送mqtt消息
    {
        sys->send_to("Heartbeat", CTRL_NAME, APP_MESSAGE_WIFI_CONN, NULL, NULL);
        heartbeat_set_sr_type(SEND);
        if (hb_cfg.mqtt_client->state() == MQTT_CONNECTED) 
        {
            run_data->send_cnt += 1;
            hb_cfg.mqtt_client->publish(hb_cfg.pubtopic, MQTT_SEND_MSG);
            Serial.printf("sent publish %s successful", hb_cfg.pubtopic); 
            heartbeat_rgb();
        }
    }
    // 发送请求。如果是wifi相关的消息，当请求完成后自动会调用 heartbeat_message_handle 函数
    // sys->send_to(HEARTBEAT_APP_NAME, CTRL_NAME,
    //              APP_MESSAGE_WIFI_ALIVE, NULL, NULL);
    getLocalTime(&run_data->timeInfo);
    // 程序需要时可以适当加延时
    display_heartbeat("heartbeat", anim_type, run_data->send_cnt, run_data->recv_cnt, &run_data->timeInfo);
    // heartbeat_set_send_recv_cnt_label(run_data->send_cnt, run_data->recv_cnt);
    // display_heartbeat_img();
    delay(30);
}

static int heartbeat_exit_callback(void *param)
{
    if (run_data->rgb_flag == 1) 
    {
        heartbeat_rgb_reset();
    }
    // 释放资源
    heartbeat_gui_del();
    free(run_data);
    run_data = NULL;
}

static void heartbeat_message_handle(const char *from, const char *to,
                                   APP_MESSAGE_TYPE type, void *message,
                                   void *ext_info)
{
    // 目前主要是wifi开关类事件（用于功耗控制）
    switch (type)
    {
    case APP_MESSAGE_WIFI_CONN:
    {
        // Serial.println(F("MQTT keep alive"));
        if (!hb_cfg.mqtt_client->connected()) {
            hb_cfg.mqtt_reconnect();
        }
        hb_cfg.mqtt_client->loop(); // 开启mqtt客户端
    }
    break;
    case APP_MESSAGE_WIFI_AP:
    {
        // todo
    }
    break;
    case APP_MESSAGE_WIFI_ALIVE:
    {
        // Serial.println(F("MQTT keep alive(APP_MESSAGE_WIFI_ALIVE)"));
        if (!hb_cfg.mqtt_client->connected()) {
            hb_cfg.mqtt_reconnect();
        }
        hb_cfg.mqtt_client->loop(); // 开启mqtt客户端
    }
    break;
    case APP_MESSAGE_GET_PARAM:
    {
        char *param_key = (char *)message;
        if (!strcmp(param_key, "mqtt_server"))
        {
            if (strlen(hb_cfg.mqtt_server_domain)>0)
            {
                snprintf((char *)ext_info, 128, "%s", hb_cfg.mqtt_server_domain);
            }
            else
            {
                snprintf((char *)ext_info, 128, "%s", hb_cfg.mqtt_server_ip.toString().c_str());
            }
        }
        else if (!strcmp(param_key, "subtopic"))
        {
            snprintf((char *)ext_info, 128, "%s", hb_cfg.subtopic);
        }
        else if (!strcmp(param_key, "pubtopic"))
        {
            snprintf((char *)ext_info, 128, "%s", hb_cfg.pubtopic);
        }
        else if (!strcmp(param_key, "client_id"))
        {
            snprintf((char *)ext_info, 128, "%s", hb_cfg.client_id);
        }
        else if (!strcmp(param_key, "username"))
        {
            snprintf((char *)ext_info, 128, "%s", hb_cfg.username);
        }
        else if (!strcmp(param_key, "passwd"))
        {
            snprintf((char *)ext_info, 128, "%s", hb_cfg.passwd);
        }
    }
    break;
    case APP_MESSAGE_SET_PARAM:
    {
        char *param_key = (char *)message;
        char *param_val = (char *)ext_info;
        if (!strcmp(param_key, "mqtt_server"))
        {
            if (hb_cfg.mqtt_server_ip.fromString(param_val))
            {
                Serial.printf("mqtt_server_ip %s", hb_cfg.mqtt_server_ip.toString().c_str());
                Serial.println();
                hb_cfg.mqtt_server_domain[0] = 0;
            }
            else 
            {
                strcpy(hb_cfg.mqtt_server_domain, param_val);
                Serial.printf("mqtt_server_domain %s", hb_cfg.mqtt_server_domain);
                Serial.println();
            }
        }
        else if (!strcmp(param_key, "subtopic"))
        {
            strcpy(hb_cfg.subtopic, param_val);
        }
        else if (!strcmp(param_key, "pubtopic"))
        {
            strcpy(hb_cfg.pubtopic, param_val);
        }
        else if (!strcmp(param_key, "client_id"))
        {
            strcpy(hb_cfg.client_id, param_val);
        }
        else if (!strcmp(param_key, "username"))
        {
            strcpy(hb_cfg.username, param_val);
        }
        else if (!strcmp(param_key, "passwd"))
        {
            strcpy(hb_cfg.passwd, param_val);
        }
    }
    break;
    case APP_MESSAGE_READ_CFG:
    {
        read_config(&hb_cfg);
    }
    break;
    case APP_MESSAGE_WRITE_CFG:
    {
        write_config(&hb_cfg);
    }
    break;
    case APP_MESSAGE_MQTT_DATA:
    {
        if (run_data->send_cnt > 0)   //已经手动发送过了
        {
            heartbeat_set_sr_type(HEART); 
        }
        else 
        {
            heartbeat_set_sr_type(RECV);
        }
        /* 亮一下 */
        heartbeat_rgb();
        run_data->recv_cnt++;
        Serial.println("received heartbeat");
    }
    break;
    default:
        break;
    }
}

APP_OBJ heartbeat_app = {HEARTBEAT_APP_NAME, &app_heartbeat, "Author HQ\nVersion 2.0.0\n",
                       heartbeat_init, heartbeat_process,
                       heartbeat_exit_callback, heartbeat_message_handle};
