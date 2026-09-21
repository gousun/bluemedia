#ifndef BLE_SVC_H
#define BLE_SVC_H

/*BlueMedia BLE 服务（NimBLE）
  服务 FFE0：FFE1=写命令（上位机→设备），FFE2=通知（设备→上位机）
  设备名 BlueMedia，单连接。
  设备→上位机事件：EV:ROT:+1 / EV:ROT:-1 / EV:KEY:S / EV:KEY:D / EV:KEY:L / EV:MODE:n
  上位机→设备命令：VOL:<0-100> / MODE:<1-n> / STATUS*/

void ble_svc_init(void);
/*上报一条事件文本给上位机（可从任意任务调用；未连接时静默忽略）*/
void ble_svc_notify_event(const char *event);
/*上报当前状态JSON（模式/音量）*/
void ble_svc_notify_status(void);

#endif
