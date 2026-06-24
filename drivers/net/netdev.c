// netdev.c
#include "netdev.h"

#define MAX_DRV 5
static NetDriver *drivers[MAX_DRV];
static int        ndrv   = 0;
static NetDriver *active = 0;

void netdev_register(NetDriver *d) { if (ndrv < MAX_DRV) drivers[ndrv++] = d; }

void netdev_init(void) {
    for (int i = 0; i < ndrv; i++) {
        if (drivers[i]->init && drivers[i]->init() == 0) {
            active = drivers[i]; return;
        }
    }
}

int         netdev_present(void)     { return active != 0; }
const char *netdev_driver_name(void) { return active ? active->name : "nenhum"; }

int  netdev_send(const uint8_t *d, uint16_t l) {
    return (active && active->send) ? active->send(d, l) : -1;
}
void netdev_poll(void) { if (active && active->poll) active->poll(); }
void netdev_get_mac(uint8_t mac[6]) {
    if (active && active->get_mac) active->get_mac(mac);
    else for (int i=0;i<6;i++) mac[i]=0;
}
void netdev_set_rx_callback(netdev_rx_cb cb) {
    if (active && active->set_rx_callback) active->set_rx_callback(cb);
}
