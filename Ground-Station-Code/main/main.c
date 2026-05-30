// SPDX-License-Identifier: Apache-2.0
// =============================================================================
//  地面站進入點 app_main —— 【停用藍牙】版本
//
//  原 Bluepad32 範本的 app_main 會 btstack_init() + uni_init()，在 Arduino setup()
//  之前就把 BT 控制器開機（log 可見 "BTstack up and running" 早於 setup）。本專案
//  控制全走「手機瀏覽器 Gamepad API → WebSocket」，完全不使用藍牙；雙模 BT 控制器
//  即使閒置仍與 Wi-Fi AP 分時共用 2.4GHz 無線電而影響吞吐（並出現 HCI/AP 互卡訊息）。
//
//  故此處不初始化 BTstack/Bluepad32：釋放 BT 控制器保留的 RAM，直接 bootstrap
//  Arduino（啟動 setup()/loop() 任務，原本由 uni_init 的 on_init_complete 觸發）。
//  → BT 控制器永不開機，與 Wi-Fi 零共存衝突。
//
//  註：sdkconfig 仍 CONFIG_BT_ENABLED=y（BT 程式碼仍編入 flash，但不會被初始化）。
//      若要連 flash 一併回收，需移除 bluepad32/btstack 元件並改 BT_ENABLED=n（較大改動）。
// =============================================================================
#include "sdkconfig.h"

#include <stddef.h>

#include "esp_bt.h"            // esp_bt_controller_mem_release / ESP_BT_MODE_BTDM
#include "arduino_bootstrap.h" // arduino_bootstrap()（啟動 Arduino 任務）

#if CONFIG_AUTOSTART_ARDUINO
#error "本專案以自訂 app_main 啟動 Arduino，請維持 CONFIG_AUTOSTART_ARDUINO=n"
#endif

int app_main(void) {
    // 不開藍牙 → 回收 BT 控制器保留的 RAM。必須在任何 BT controller init 之前；
    // 本專案不初始化 BT，故此呼叫應回 ESP_OK 並實際釋放記憶體。
    esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);

    // 啟動 Arduino setup()/loop() 任務（不經 Bluepad32/BTstack）。
    arduino_bootstrap();
    return 0;
}
