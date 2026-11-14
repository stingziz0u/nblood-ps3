/*
 * EDuke32-PS3: PSL1GHT has no <poll.h>. Only enet.h asks for it, and the
 * port is built without network play (NETCODE_DISABLE, no enet.cpp):
 * it is included for enet_gettime and never used.
 */
#ifndef PS3_COMPAT_POLL_H
#define PS3_COMPAT_POLL_H
#endif
