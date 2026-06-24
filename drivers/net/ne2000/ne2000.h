#ifndef NE2000_H
#define NE2000_H
#include <stdint.h>

int  ne2000_init(void);
int  ne2000_present(void);
int  ne2000_send(const uint8_t *data, uint16_t len);
void ne2000_poll(void);
void ne2000_get_mac(uint8_t mac[6]);
void ne2000_set_rx_callback(void (*cb)(const uint8_t *data, uint16_t len));
void ne2000_register(void);   // registra no netdev HAL

#endif
