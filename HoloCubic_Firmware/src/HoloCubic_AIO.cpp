/***************************************************
  HoloCubic多功能固件源码

  聚合多种APP，内置天气、时钟、相册、特效动画、视频播放、视频投影、
  浏览器文件修改。（各APP具体使用参考说明书）

  Github repositories：https://github.com/ClimbSnail/HoloCubic_AIO

  Last review/edit by ClimbSnail: 2021/08/21
 ****************************************************/

#include "driver/lv_port_indev.h"
#include "driver/lv_port_fatfs.h"
#include "common.h"

#include "sys/app_controller.h"

#include "app/weather/weather.h"
#include "app/bilibili_fans/bilibili.h"
#include "app/server/server.h"
#include "app/idea_anim/idea.h"
#include "app/settings/settings.h"
#include "app/game_2048/game_2048.h"
#include "app/picture/picture.h"
#include "app/media_player/media_player.h"
#include "app/screen_share/screen_share.h"
#include "app/file_manager/file_manager.h"
#include "app/weather_old/weather_old.h"
#include "app/anniversary/anniversary.h"
#include "app/heartbeat/heartbeat.h"
#include "app/tomato/tomato.h"

#include <SPIFFS.h>
#include <esp32-hal.h>

const String WDAY_NAMES[] = {"星期天", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"};                //星期
const String MONTH_NAMES[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"}; //月份

/*** Component objects **7*/
ImuAction *act_info;           // 存放mpu6050返回的数据
AppController *app_controller; // APP控制器

TaskHandle_t handleTaskLvgl;
void TaskLvglUpdate(void *parameter)
{
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    for (;;)
    {
        screen.routine();
        // lv_task_handler();

        delay(5);
    }
}

//time_t now; //实例化时间
// void setClock()
// {

//   struct tm timeInfo; //声明一个结构体
//   if (!getLocalTime(&timeInfo))
//   { //一定要加这个条件判断，否则内存溢出
//     Serial.println("Failed to obtain time");
//     return;
//   }
//   //Serial.print(asctime(&timeInfo)); //默认打印格式：Mon Oct 25 11:13:29 2021
//   String date = WDAY_NAMES[timeInfo.tm_wday];
//   Serial.println(date.c_str());
//   // sprintf_P(buff1, PSTR("%04d-%02d-%02d %s"), timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday, WDAY_NAMES[timeInfo.tm_wday].c_str());
//   String shuju = String(timeInfo.tm_year + 1900); //年
//   shuju += "-";
//   shuju += timeInfo.tm_mon + 1; //月
//   shuju += "-";
//   shuju += timeInfo.tm_mday; //日
//   shuju += " ";
//   shuju += timeInfo.tm_hour; //时
//   shuju += ":";
//   shuju += timeInfo.tm_min;
//   shuju += ":";
//   shuju += timeInfo.tm_sec;
//   shuju += " ";
//   shuju += WDAY_NAMES[timeInfo.tm_wday].c_str(); //星期
//   Serial.println(shuju.c_str());
// }

void setup()
{
    Serial.begin(115200);

    Serial.println(F("\nAIO (All in one) version " AIO_VERSION "\n"));
    // MAC ID可用作芯片唯一标识
    Serial.print(F("ChipID(EfuseMac): "));
    Serial.println(ESP.getEfuseMac());
    // flash运行模式
    // Serial.print(F("FlashChipMode: "));
    // Serial.println(ESP.getFlashChipMode());
    // Serial.println(F("FlashChipMode value: FM_QIO = 0, FM_QOUT = 1, FM_DIO = 2, FM_DOUT = 3, FM_FAST_READ = 4, FM_SLOW_READ = 5, FM_UNKNOWN = 255"));

    app_controller = new AppController(); // APP控制器

    // 需要放在Setup里初始化
    if (!SPIFFS.begin(true))
    {
        Serial.println("SPIFFS Mount Failed");
        return;
    }

#ifdef PEAK
    pinMode(CONFIG_BAT_CHG_DET_PIN, INPUT);
    pinMode(CONFIG_ENCODER_PUSH_PIN, INPUT_PULLUP);
    /*电源使能保持*/
    Serial.println("Power: Waiting...");
    pinMode(CONFIG_POWER_EN_PIN, OUTPUT);
    digitalWrite(CONFIG_POWER_EN_PIN, LOW);
    digitalWrite(CONFIG_POWER_EN_PIN, HIGH);
    Serial.println("Power: ON");
    log_e("Power: ON");
#endif

    // config_read(NULL, &g_cfg);   // 旧的配置文件读取方式
    app_controller->read_config(&app_controller->sys_cfg);
    app_controller->read_config(&app_controller->mpu_cfg);
    app_controller->read_config(&app_controller->rgb_cfg);

    /*** Init screen ***/
    screen.init(app_controller->sys_cfg.rotation,
                app_controller->sys_cfg.backLight);

    /*** Init on-board RGB ***/
    rgb.init();
    rgb.setBrightness(0.05).setRGB(0, 64, 64);

    /*** Init ambient-light sensor ***/
    ambLight.init(ONE_TIME_H_RESOLUTION_MODE);

    /*** Init micro SD-Card ***/
    tf.init();
    lv_fs_if_init();

    app_controller->init();
    // 将APP"安装"到controller里
    app_controller->app_install(&weather_app);
    // app_controller->app_install(&weather_old_app);
    app_controller->app_install(&picture_app);
    app_controller->app_install(&media_app);
    // app_controller->app_install(&screen_share_app);
    // app_controller->app_install(&file_manager_app);
    app_controller->app_install(&server_app);
    // app_controller->app_install(&idea_app);
    // app_controller->app_install(&bilibili_app);
    // app_controller->app_install(&settings_app);
    app_controller->app_install(&game_2048_app);
    app_controller->app_install(&anniversary_app);
    app_controller->app_install(&heartbeat_app);
    app_controller->app_install(&tomato_app);

    // 优先显示屏幕 加快视觉上的开机时间
    app_controller->main_process(&mpu.action_info);

    /*** Init IMU as input device ***/
    lv_port_indev_init();
    mpu.init(app_controller->sys_cfg.mpu_order,
             app_controller->sys_cfg.auto_calibration_mpu,
             &app_controller->mpu_cfg); // 初始化比较耗时

    if (app_controller->wifi_event(APP_MESSAGE_WIFI_CONN))
    {
        configTime(8 * 3600, 0, "ntp1.aliyun.com", "ntp2.aliyun.com", "ntp3.aliyun.com");
        app_controller->connect_mqtt();
    }

    /*** 以此作为MPU6050初始化完成的标志 ***/
    RgbConfig *rgb_cfg = &app_controller->rgb_cfg;
    // 初始化RGB灯 HSV色彩模式
    RgbParam rgb_setting = {LED_MODE_HSV,
                            rgb_cfg->min_value_0, rgb_cfg->min_value_1, rgb_cfg->min_value_2,
                            rgb_cfg->max_value_0, rgb_cfg->max_value_1, rgb_cfg->max_value_2,
                            rgb_cfg->step_0, rgb_cfg->step_1, rgb_cfg->step_2,
                            rgb_cfg->min_brightness, rgb_cfg->max_brightness,
                            rgb_cfg->brightness_step, rgb_cfg->time};
    // 初始化RGB任务
    rgb_thread_init(&rgb_setting);
    // char info[128] = {0};
    // uint16_t size = g_flashCfg.readFile("/heartbeat.cfg", (uint8_t *)info);
    // if (size != 0) // 如果已经设置过heartbeat了，则开启mqtt客户端
    // {
    //     app_controller->connect_mqtt();
    // }

    // Update display in parallel thread.
    // xTaskCreate(
    //     TaskLvglUpdate,
    //     "LvglThread",
    //     20000,
    //     nullptr,
    //     configMAX_PRIORITIES - 1,
    //     &handleTaskLvgl);
}

void loop()
{
    screen.routine();
    act_info = mpu.update(200);

#ifdef PEAK
    if (!mpu.Encoder_GetIsPush())
    {
        Serial.println("mpu.Encoder_GetIsPush()1");
        delay(1000);
        if (!mpu.Encoder_GetIsPush())
        {
            Serial.println("mpu.Encoder_GetIsPush()2");
            // 适配Peak的关机功能
            digitalWrite(CONFIG_POWER_EN_PIN, LOW);
        }
    }
#endif
    // setClock();
    app_controller->main_process(act_info); // 运行当前进程
    // Serial.println(ambLight.getLux() / 50.0);
    // rgb.setBrightness(ambLight.getLux() / 500.0);
}