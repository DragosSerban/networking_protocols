#include "queue.h"
#include "lib.h"
#include "protocols.h"
#include <arpa/inet.h>
#include <string.h>

#define ETHERTYPE_IPv4 2048
#define ETHERTYPE_ARP 2054

/* routing table */
struct route_table_entry *rtable;
int rtable_len;

/* arp table */
struct arp_entry *arp_table;
int arp_table_len;

/* data stored in the queue, waiting to be sent */
struct queue_data {
	char *data;
	int len;
};

/* function that converts ip string into integer */
uint32_t get_interface_ip_as_int(int interface)
{
	struct in_addr addr;
	inet_pton(AF_INET, get_interface_ip(interface), &addr);
	return addr.s_addr;
}

/* function used for qsort; to sort the routing table in ascending order based on prefix and then on mask */
int compare(const void *p1, const void *p2)
{
	if (ntohl(((struct route_table_entry*)p1)->prefix) - ntohl(((struct route_table_entry*)p2)->prefix))
		return ntohl(((struct route_table_entry*)p1)->prefix) - ntohl(((struct route_table_entry*)p2)->prefix);
	return ntohl(((struct route_table_entry*)p1)->mask) - ntohl(((struct route_table_entry*)p2)->mask);
}

/* get best route to an ip address, using efficient binary search algorithm */
struct route_table_entry *get_best_route(uint32_t ip_dest)
{
	int left = 0;
	int right = rtable_len - 1;
	struct route_table_entry *best_route = NULL;
	while (left <= right) {
		int mid = (left + right) / 2;
		if (ntohl(rtable[mid].prefix) == (ntohl(ip_dest) & ntohl(rtable[mid].mask))) {
			best_route = &rtable[mid];
			left = mid + 1;
		} else if (ntohl(rtable[mid].prefix) > ntohl(ip_dest)) {
			right = mid -1;
		} else {
			left = mid + 1;
		}
	}
	return best_route;
}

/* function used for getting data from the arp table for a specific ip address */
struct arp_entry *get_arp_entry(uint32_t given_ip)
{
	for (int i = 0; i < arp_table_len; i++)
		if (arp_table[i].ip == given_ip)
			return &(arp_table[i]);
	return NULL;
}

/* function to send queued ipv4 packets waiting for mac address of destination */
void send_queued_packets(struct queue *waiting_packets)
{
	while (!queue_empty(waiting_packets)) {
		struct queue_data *q_data = queue_deq(waiting_packets);
		char *buffer = q_data->data;
		int length = q_data->len;
		struct ether_header *eth_hdr = (struct ether_header *) buffer;
		struct iphdr *ip_hdr = (struct iphdr *)(buffer + sizeof(struct ether_header));

		struct route_table_entry *best_route = get_best_route(ip_hdr->daddr);
		struct arp_entry *crt_arp_entry = get_arp_entry(best_route->next_hop);

		if (!crt_arp_entry) {
			queue_enq(waiting_packets, q_data);
			return;
		} else {
			get_interface_mac(best_route->interface, eth_hdr->ether_shost);
			memcpy(eth_hdr->ether_dhost, crt_arp_entry->mac, 6 * sizeof(uint8_t));
			free(q_data);

			send_to_link(best_route->interface, buffer, length);
		}
	}
}

/* function used to send icmp packet */
void send_icmp(char *buf, unsigned int len, int interface, int type, int code)
{
	struct icmphdr *icmp_hdr = calloc(1, sizeof(struct icmphdr));
	icmp_hdr->type = type;
	icmp_hdr->code = code;
	icmp_hdr->checksum = 0;

	struct ether_header *eth_hdr = (struct ether_header *) buf;
	for (int i = 0; i < 6; i++) {
		uint8_t aux = eth_hdr->ether_dhost[i];
		eth_hdr->ether_dhost[i] = eth_hdr->ether_shost[i];
		eth_hdr->ether_shost[i] = aux;
	}

	struct iphdr *ip_hdr = (struct iphdr *)(buf + sizeof(struct ether_header));
	struct iphdr *new_ip_hdr = calloc(1, sizeof(struct iphdr));
	memcpy(new_ip_hdr, ip_hdr, sizeof(struct iphdr));

	new_ip_hdr->daddr = new_ip_hdr->saddr;
	new_ip_hdr->saddr = get_interface_ip_as_int(interface);
	new_ip_hdr->ttl = 64;
	new_ip_hdr->tos = 0;
	new_ip_hdr->frag_off = 0;
	new_ip_hdr->tot_len = htons(2*sizeof(struct iphdr) + sizeof(struct icmphdr) + 8);
	new_ip_hdr->protocol = 1;
	new_ip_hdr->check = 0;
	new_ip_hdr->check = htons(checksum((uint16_t*)new_ip_hdr, sizeof(struct iphdr)));

	char *icmp_buffer = calloc(1, sizeof(struct ether_header) + sizeof(struct icmphdr) + 2*sizeof(struct iphdr) + 8);
	memcpy(icmp_buffer, buf, sizeof(struct ether_header));
	memcpy(icmp_buffer + sizeof(struct ether_header), new_ip_hdr, sizeof(struct iphdr));
	memcpy(icmp_buffer + sizeof(struct ether_header) + sizeof(struct iphdr), icmp_hdr, sizeof(struct icmphdr));
	memcpy(icmp_buffer + sizeof(struct ether_header) + sizeof(struct iphdr) + sizeof(struct icmphdr), buf + sizeof(struct ether_header), sizeof(struct iphdr));
	memcpy(icmp_buffer + sizeof(struct ether_header) + 2*sizeof(struct iphdr) + sizeof(struct icmphdr), buf + sizeof(struct ether_header) + sizeof(struct iphdr), 8);
	icmp_hdr->checksum = htons(checksum((uint16_t*)(icmp_buffer + sizeof(struct ether_header) + sizeof(struct iphdr)), sizeof(struct icmphdr) + sizeof(struct iphdr) + 8));
	memcpy (icmp_buffer + sizeof(struct ether_header) + sizeof(struct iphdr) + 2, &icmp_hdr->checksum, 2);

	free(buf);
	free(icmp_hdr);
	free(new_ip_hdr);

	send_to_link(interface, icmp_buffer, sizeof(struct ether_header) + sizeof(struct icmphdr) + 2*sizeof(struct iphdr) + 8);
}

int main(int argc, char *argv[])
{
	char buf[MAX_PACKET_LEN];

	// Do not modify this line
	init(argc - 2, argv + 2);

	/* Code to allocate the ARP and route tables */
	rtable = malloc(sizeof(struct route_table_entry) * 100000);
	/* DIE is a macro for sanity checks */
	DIE(rtable == NULL, "memory");

	arp_table = malloc(sizeof(struct  arp_entry) * 100000);
	DIE(arp_table == NULL, "memory");
	
	/* read the static routing table and sort it */
	rtable_len = read_rtable(argv[1], rtable);
	qsort(rtable, rtable_len, sizeof(struct route_table_entry), compare);

	/* initialize queue */
	struct queue *waiting_packets = queue_create();

	while (1) {

		int interface;
		size_t len;

		interface = recv_from_any_link(buf, &len);
		DIE(interface < 0, "recv_from_any_links");

		struct ether_header *eth_hdr = (struct ether_header *) buf;
		/* Note that packets received are in network order,
		any header field which has more than 1 byte will need to be conerted to
		host order. For example, ntohs(eth_hdr->ether_type). The oposite is needed when
		sending a packet on the link, */

		/* verify if destination mac of packet is different than router's interface */
		uint8_t *crt_mac = malloc(6 * sizeof(uint8_t));
		get_interface_mac(interface, crt_mac);
		int broadcast_mac = 0;
		int is_different = 0;
		for (int i = 0; i < 6; i++) {
			if (eth_hdr->ether_dhost[i] == 0xff)
				broadcast_mac++;
			if (crt_mac[i] != eth_hdr->ether_dhost[i])
				is_different = 1;
		}
		free(crt_mac);
		if (broadcast_mac != 6 && is_different == 1)
			continue;

		if (ntohs(eth_hdr->ether_type) == ETHERTYPE_IPv4) {
			/* ipv4 packet was received */
			struct iphdr *ip_hdr = (struct iphdr *)(buf + sizeof(struct ether_header));

			uint32_t interface_ip = get_interface_ip_as_int(interface);

			if (interface_ip == ip_hdr->daddr) {
				// ramane pachetul in router
				char *icmp_buf = malloc(len * sizeof(char));
				memcpy(icmp_buf, buf, len);
				send_icmp(icmp_buf, len, interface, 0, 0);
				continue;
			}

			/* verify checksum */
			uint16_t check = ntohs(ip_hdr->check);
			ip_hdr->check = 0;

			if (check != checksum((uint16_t *)ip_hdr, sizeof(struct iphdr))) {
				// checksum gresit
				continue;
			}

			/* verify TTL; decrement it; recalculate checksum */
			if (ip_hdr->ttl <= 1) {
				char *icmp_buf = malloc(len * sizeof(char));
				memcpy(icmp_buf, buf, len);
				send_icmp(icmp_buf, len, interface, 11, 0);
				continue;
			} else {
				ip_hdr->ttl--;
				ip_hdr->check = ~(~(htons(check)) + ~((uint16_t)ip_hdr->ttl + 1) + (uint16_t)ip_hdr->ttl) - 1;
			}

			/* get best route */
			struct route_table_entry *best_route = get_best_route(ip_hdr->daddr);
			if (!best_route) {
				char *icmp_buf = malloc(len * sizeof(char));
				memcpy(icmp_buf, buf, len);
				send_icmp(icmp_buf, len, interface, 3, 0);
				continue;
			}

			/* search for entry in arp table */
			struct arp_entry *crt_arp_entry = get_arp_entry(best_route->next_hop);

			/* if there is no entry,
					send arp packet in order to find mac address
				else,
					send the packet */
			if (!crt_arp_entry) {
				// queue current ipv4 packet
				struct queue_data *aux = malloc(sizeof(struct queue_data));
				aux->data = malloc(len);
				if (!aux->data)
					return 0;
				aux->len = len;
				memcpy(aux->data, buf, len);
				queue_enq(waiting_packets, (void *)aux);

				// create arp packet
				unsigned int arp_packet_len = sizeof(struct ether_header) + sizeof(struct arp_header);
				char *arp_buf = malloc(arp_packet_len);
				if (!arp_buf)
					return 0;

				struct ether_header *arp_packet_eth_hdr = (struct ether_header *)arp_buf;
				struct arp_header *arp_packet_arp_hdr = (struct arp_header *)(arp_buf + sizeof(struct ether_header));

				// set ethernet header field
				for (int i = 0; i < 6; i++)
					arp_packet_eth_hdr->ether_dhost[i] = 0xff;
				get_interface_mac(best_route->interface, arp_packet_eth_hdr->ether_shost);
				arp_packet_eth_hdr->ether_type = htons(0x806);

				// set arp header fields
				arp_packet_arp_hdr->htype = htons(1);
				arp_packet_arp_hdr->ptype = htons(0x0800);
				arp_packet_arp_hdr->hlen = 6;
				arp_packet_arp_hdr->plen = 4;
				arp_packet_arp_hdr->op = htons(1);
				for (int i = 0; i < 6; i++) {
					arp_packet_arp_hdr->sha[i] = arp_packet_eth_hdr->ether_shost[i];
					arp_packet_arp_hdr->tha[i] = 0;
				}
				arp_packet_arp_hdr->spa = get_interface_ip_as_int(best_route->interface);
				arp_packet_arp_hdr->tpa = best_route->next_hop;

				// send arp packet
				send_to_link(best_route->interface, arp_buf, arp_packet_len);
			} else {
				// arp entry found in arp table => we send the ipv4 packet
				get_interface_mac(best_route->interface, eth_hdr->ether_shost);
				memcpy(eth_hdr->ether_dhost, crt_arp_entry->mac, 6 * sizeof(uint8_t));

				send_to_link(best_route->interface, buf, len);
			}
		} else if (ntohs(eth_hdr->ether_type) == ETHERTYPE_ARP) {
			/* arp packet was received */
			struct arp_header *arp_hdr = (struct arp_header *)(buf + sizeof(struct ether_header));

			if (ntohs(arp_hdr->op) == 1) {
				// arp request received => send a reply with the mac address of the destination ip
				arp_hdr->op = htons(2);
				uint32_t aux = arp_hdr->spa;
				arp_hdr->spa = arp_hdr->tpa;
				arp_hdr->tpa = aux;
				memcpy(arp_hdr->tha, arp_hdr->sha, 6 * sizeof(uint8_t));

				get_interface_mac(interface, arp_hdr->sha);

				memcpy(eth_hdr->ether_dhost, arp_hdr->tha, 6 * sizeof(uint8_t));
				memcpy(eth_hdr->ether_shost, arp_hdr->sha, 6 * sizeof(uint8_t));
				eth_hdr->ether_type = ntohs(ETHERTYPE_ARP);

				// verify if ip address of interface equals ip address of destination in arp request
				uint32_t ip_addr = get_interface_ip_as_int(interface);

				// send arp reply
				if (arp_hdr->spa == ip_addr)
					send_to_link(interface, buf, len);

			} else if (ntohs(arp_hdr->op) == 2) {
				// verify if the packet is for this router or not
				uint32_t ip_addr = get_interface_ip_as_int(interface);
				if (arp_hdr->tpa != ip_addr || get_arp_entry(arp_hdr->spa) != NULL)
					continue;

				// save the arp packet inside the arp table
				arp_table[arp_table_len].ip = arp_hdr->spa;
				memcpy(arp_table[arp_table_len].mac, arp_hdr->sha, 6 * sizeof(uint8_t));
				arp_table_len += 1;

				// send the packets saved in the queue
				send_queued_packets(waiting_packets);
			}
		}
	}
	/* free the memory used by rtable and ARP table */
	free(rtable);
	free(arp_table);

	return 0;
}
