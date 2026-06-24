#ifndef RTL8139_H
#define RTL8139_H
#include <stdint.h>

typedef void (*rtl8139_rx_cb)(const uint8_t *data, uint16_t len);

int  rtl8139_init(void);
int  rtl8139_present(void);
int  rtl8139_send(const uint8_t *data, uint16_t len);
void rtl8139_poll(void);
void rtl8139_set_rx_callback(rtl8139_rx_cb cb);
void rtl8139_get_mac(uint8_t mac[6]);
void rtl8139_irq_handler(void);
void rtl8139_register(void);
#endif
