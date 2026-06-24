// netdev.h
#ifndef NETDEV_H
#define NETDEV_H
#include <stdint.h>

typedef void (*netdev_rx_cb)(const uint8_t *data, uint16_t len);

typedef struct {
    const char *name;
    int   (*init)(void);
    int   (*present)(void);
    int   (*send)(const uint8_t *data, uint16_t len);
    void  (*poll)(void);
    void  (*get_mac)(uint8_t mac[6]);
    void  (*set_rx_callback)(netdev_rx_cb cb);
} NetDriver;

void        netdev_register(NetDriver *drv);
void        netdev_init(void);
int         netdev_present(void);
const char *netdev_driver_name(void);

int  netdev_send(const uint8_t *data, uint16_t len);
void netdev_poll(void);
void netdev_get_mac(uint8_t mac[6]);
void netdev_set_rx_callback(netdev_rx_cb cb);
#endif
