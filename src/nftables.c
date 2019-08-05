/**
 *  This file is part of nftables-dhcpsnoopingd.
 *
 *  Ebtables-dhcpsnoopingd is free software: you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Ebtables-dhcpsnoopingd is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with nftables-dhcpsnoopingd.
 *  If not, see <http://www.gnu.org/licenses/>.
 *
 *  (C) 2013, Michael Braun <michael-dev@fami-braun.de>
 */


#include "config.h"
#include "debug.h"
#include "cmdline.h"
#include "dhcp.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/ether.h>
#include "ether_ntoa.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef CHAINNAME
#define CHAINNAME "dhcpsnooping"
#endif
#ifndef SETNAME
#define SETNAME "leases"
#endif
#ifndef MAPNAME
#define MAPNAME "leases"
#endif
#ifndef NFTABLES
#define NFTABLES "nfts"
#endif

#ifdef __USE_NFTABLES__

static int disabled = 0;
static int legacy = 0;

static void nftables_run(const char* cmd) {
	eprintf(DEBUG_GENERAL, "run \"%s\"", cmd);
	if (system(cmd)) {
		eprintf(DEBUG_ERROR, "cmd \"%s\" failed", cmd);
	} else {
		eprintf(DEBUG_GENERAL, "cmd \"%s\" ok", cmd);
	}
}

static void nftables_novlan(const char* op, const struct in_addr* ip, const uint8_t* mac, const char* ifname) {
	char cmd[65535];

	if (legacy) {
		snprintf(cmd, sizeof(cmd), NFTABLES " %s rule bridge filter " CHAINNAME "ether saddr %s ip saddr %s meta ibrname \"%s\" return",
			 op, ether_ntoa_z((struct ether_addr *)mac), inet_ntoa(*ip), ifname);
		nftables_run(cmd);
		snprintf(cmd, sizeof(cmd), NFTABLES " %s rule bridge filter " CHAINNAME "ether saddr %s arp saddr ip %s meta ibrname \"%s\" return",
			 op, ether_ntoa_z((struct ether_addr *)mac), inet_ntoa(*ip), ifname);
		nftables_run(cmd);
		snprintf(cmd, sizeof(cmd), NFTABLES " %s rule bridge nat " CHAINNAME "arp daddr ip %s meta ibrname %s dnat %s return",
			 op, inet_ntoa(*ip), ifname, ether_ntoa_z((struct ether_addr *)mac));
		nftables_run(cmd);
	} else {
		snprintf(cmd, sizeof(cmd), NFTABLES " %s element bridge filter " SETNAME " { \"%s\" . %s . %s }",
			 op, ifname, ether_ntoa_z((struct ether_addr *)mac), inet_ntoa(*ip));
		nftables_run(cmd);
		snprintf(cmd, sizeof(cmd), NFTABLES " %s element bridge nat " MAPNAME " { \"%s\" . %s : %s }",
			 op, ifname, inet_ntoa(*ip), ether_ntoa_z((struct ether_addr *)mac));
		nftables_run(cmd);
	}
}

static void nftables_vlan(const char* op, const struct in_addr* ip, const uint8_t* mac, const char* ifname, const int vlanid) {
	char cmd[65535];

	if (legacy) {
		snprintf(cmd, sizeof(cmd), NFTABLES " %s rule bridge filter " CHAINNAME "ether saddr %s vlan id %d ip saddr %s meta ibrname \"%s\" return",
			 op, ether_ntoa_z((struct ether_addr *)mac), vlanid, inet_ntoa(*ip), ifname);
		nftables_run(cmd);
		snprintf(cmd, sizeof(cmd), NFTABLES " %s rule bridge filter " CHAINNAME "ether saddr %s vlan id %d arp saddr ip %s meta ibrname \"%s\" return",
			 op, ether_ntoa_z((struct ether_addr *)mac), vlanid, inet_ntoa(*ip), ifname);
		nftables_run(cmd);
		snprintf(cmd, sizeof(cmd), NFTABLES " %s rule bridge nat " CHAINNAME "vlan id %d arp daddr ip %s meta ibrname %s dnat %s return",
			 op, vlanid, inet_ntoa(*ip), ifname, ether_ntoa_z((struct ether_addr *)mac));
		nftables_run(cmd);
	} else {
		snprintf(cmd, sizeof(cmd), NFTABLES " %s element bridge filter " SETNAME " { \"%s\" . %d . %s . %s }",
			 op, ifname, vlanid, ether_ntoa_z((struct ether_addr *)mac), inet_ntoa(*ip));
		nftables_run(cmd);
		snprintf(cmd, sizeof(cmd), NFTABLES " %s element bridge nat " MAPNAME " { \"%s\" . %d . %s : %s }",
			 op, ifname, vlanid, inet_ntoa(*ip), ether_ntoa_z((struct ether_addr *)mac));
		nftables_run(cmd);
	}
}

static void nftables_do(const char* ifname, const int vlanid, const uint8_t* mac, const struct in_addr* ip, const int start)
{
	if (disabled)
		return;

	assert(ip); assert(mac); assert(ifname);
	const char *op = start ? "add" : "delete";

	eprintf(DEBUG_VERBOSE, "%s nftables rule: MAC: %s IP: %s BRIDGE: %s VLAN: %d", op, ether_ntoa_z((struct ether_addr *)mac), inet_ntoa(*ip), ifname, vlanid);
	if (vlanid == 0)
		nftables_novlan(op, ip, mac, ifname);
	else
		nftables_vlan(op, ip, mac, ifname, vlanid);
}

static void disable_nftables(int c)
{
	disabled = 1;
}

static void enable_nftables_legacy(int c)
{
	/* delete rules by statement is not yet implemented by nft, but we do not have a cache here so just ignore it for now
	 * see https://wiki.nftables.org/wiki-nftables/index.php/Simple_rule_management
	 */
	legacy = 1;
}

static __attribute__((constructor)) void nftables_init()
{
        static struct option de_option = {"disable-nftables", no_argument, 0, 0};
        add_option_cb(de_option, disable_nftables);
        static struct option le_option = {"nftables-legacy", no_argument, 0, 0};
        add_option_cb(le_option, enable_nftables_legacy);
	add_lease_start_stop_hook(nftables_do);
}

#endif
