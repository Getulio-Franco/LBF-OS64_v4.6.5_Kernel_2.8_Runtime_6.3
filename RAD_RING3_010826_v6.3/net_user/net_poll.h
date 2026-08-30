/* ============================================================================
 * ARCHITECTURE: Ring 3 (User Space Network Stack)
 * FILE: net_user/net_poll.h
 * DESCRIPTION: Loop de recepção e despachante principal de pacotes de rede
 * ============================================================================ */

#ifndef NET_POLL_H
#define NET_POLL_H

#include <stdint.h>
#include <stdbool.h>

void net_poll(void);

#endif // NET_POLL_H
