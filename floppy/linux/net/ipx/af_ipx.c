/*
 *	Implements an IPX socket layer.
 *
 *	This code is derived from work by
 *		Ross Biro	: 	Writing the original IP stack
 *		Fred Van Kempen :	Tidying up the TCP/IP
 *
 *	Many thanks go to Keith Baker, Institute For Industrial Information
 *	Technology Ltd, Swansea University for allowing me to work on this
 *	in my own time even though it was in some ways related to commercial
 *	work I am currently employed to do there.
 *
 *	All the material in this file is subject to the Gnu license version 2.
 *	Neither Alan Cox nor the Swansea University Computer Society admit 
 *	liability nor provide warranty for any of this software. This material
 *	is provided as is and at no charge.
 *
 *	Revision 0.21:	Uses the new generic socket option code.
 *	Revision 0.22:	Gcc clean ups and drop out device registration. Use the
 *			new multi-protocol edition of hard_header
 *	Revision 0.23:  IPX /proc by Mark Evans. Adding a route will
 *     			will overwrite any existing route to the same network.
 *	Revision 0.24:	Supports new /proc with no 4K limit
 *	Revision 0.25:	Add ephemeral sockets, passive local network
 *			identification, support for local net 0 and
 *			multiple datalinks <Greg Page>
 *	Revision 0.26:  Device drop kills IPX routes via it. (needed for module)
 *	Revision 0.27:  Autobind <Mark Evans>
 *	Revision 0.28:  Small fix for multiple local networks <Thomas Winder>
 *	Revision 0.29:  Assorted major errors removed <Mark Evans>
 *			Small correction to promisc mode error fix <Alan Cox>
 *			Asynchronous I/O support. Changed to use notifiers
 *			and the newer packet_type stuff. Assorted major
 *			fixes <Alejandro Liu>
 *	Revision 0.30:	Moved to net/ipx/...	<Alan Cox>
 *			Don't set address length on recvfrom that errors.
 *			Incorrect verify_area.
 *	Revision 0.31:	New sk_buffs. This still needs a lot of 
 *			testing. <Alan Cox>
 *	Revision 0.32:  Using sock_alloc_send_skb, firewall hooks. <Alan Cox>
 *			Supports sendmsg/recvmsg
 *	Revision 0.33:	Internal network support, routing changes, uses a
 *			protocol private area for ipx data.
 *	Revision 0.34:	Module support. <Jim Freeman>
 *	Revision 0.35:  Checksum support. <Neil Turton>, hooked in by <Alan Cox>
 *			Handles WIN95 discovery packets <Volker Lendecke>
 *	Revision 0.36:	Internal bump up for 2.1
 *	Revision 0.37:	Began adding POSIXisms.
 *	Revision 0.38:  Asynchronous socket stuff made current.
 *	Revision 0.39:  SPX interfaces
 *	Revision 0.40:  Tiny SIOCGSTAMP fix (chris@cybernet.co.nz)
 *      Revision 0.41:  802.2TR removed (p.norton@computer.org)
 *			Fixed connecting to primary net,
 *			Automatic binding on send & receive,
 *			Martijn van Oosterhout <kleptogimp@geocities.com>
 *	Revision 042:   Multithreading - use spinlocks and refcounting to
 *			protect some structures: ipx_interface sock list, list
 *			of ipx interfaces, etc. 
 *			Bugfixes - do refcounting on net_devices, check function
 *			results, etc. Thanks to davem and freitag for
 *			suggestions and guidance.
 *			Arnaldo Carvalho de Melo <acme@conectiva.com.br>,
 *			November, 2000
 *	Revision 043:	Shared SKBs, don't mangle packets, some cleanups
 *			Arnaldo Carvalho de Melo <acme@conectiva.com.br>,
 *			December, 2000
 *	Revision 044:	Call ipxitf_hold on NETDEV_UP (acme)
 *
 *	Protect the module by a MOD_INC_USE_COUNT/MOD_DEC_USE_COUNT
 *	pair. Also, now usage count is managed this way
 *	-Count one if the auto_interface mode is on
 *      -Count one per configured interface
 *
 *	Jacques Gelinas (jacques@solucorp.qc.ca)
 *
 *
 * 	Portions Copyright (c) 1995 Caldera, Inc. <greg@caldera.com>
 *	Neither Greg Page nor Caldera, Inc. admit liability nor provide
 *	warranty for any of this software. This material is provided
 *	"AS-IS" and at no charge.
 */

#include <linux/config.h>
#if defined (CONFIG_IPX) || defined (CONFIG_IPX_MODULE)
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <net/ipx.h>
#include <linux/inet.h>
#include <linux/route.h>
#include <net/sock.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/termios.h>	/* For TIOCOUTQ/INQ */
#include <linux/interrupt.h>
#include <net/p8022.h>
#include <net/psnap.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/if_arp.h>

#ifdef MODULE
static void ipx_proto_finito(void);
#endif /* def MODULE */

/* Configuration Variables */
static unsigned char ipxcfg_max_hops = 16;
static char ipxcfg_auto_select_primary;
static char ipxcfg_auto_create_interfaces;

/* Global Variables */
static struct datalink_proto *p8022_datalink;
static struct datalink_proto *pEII_datalink;
static struct datalink_proto *p8023_datalink;
static struct datalink_proto *pSNAP_datalink;

static struct proto_ops ipx_dgram_ops;

static struct net_proto_family *spx_family_ops;

static ipx_route *ipx_routes;
static rwlock_t ipx_routes_lock = RW_LOCK_UNLOCKED;

static ipx_interface *ipx_interfaces;
static spinlock_t ipx_interfaces_lock = SPIN_LOCK_UNLOCKED;

static ipx_interface *ipx_primary_net;
static ipx_interface *ipx_internal_net;

#undef IPX_REFCNT_DEBUG
#ifdef IPX_REFCNT_DEBUG
atomic_t ipx_sock_nr;
#endif

static void ipxcfg_set_auto_create(char val)
{
	if (ipxcfg_auto_create_interfaces != val) {
		if (val)
			MOD_INC_USE_COUNT;
		else
			MOD_DEC_USE_COUNT;

		ipxcfg_auto_create_interfaces = val;
	}
}

static void ipxcfg_set_auto_select(char val)
{
	ipxcfg_auto_select_primary = val;
	if (val && !ipx_primary_net)
		ipx_primary_net = ipx_interfaces;
}

static int ipxcfg_get_config_data(ipx_config_data *arg)
{
	ipx_config_data	vals;

	vals.ipxcfg_auto_create_interfaces = ipxcfg_auto_create_interfaces;
	vals.ipxcfg_auto_select_primary = ipxcfg_auto_select_primary;

	return copy_to_user(arg, &vals, sizeof(vals)) ? -EFAULT : 0;
}

/**************************************************************************\
*                                                                          *
* Handlers for the socket list.                                            *
*                                                                          *
\**************************************************************************/

static inline void ipxitf_hold(ipx_interface *intrfc)
{
	atomic_inc(&intrfc->refcnt);
}

static void ipxitf_down(ipx_interface *intrfc);

static inline void ipxitf_put(ipx_interface *intrfc)
{
	if (atomic_dec_and_test(&intrfc->refcnt))
		ipxitf_down(intrfc);
}

static void __ipxitf_down(ipx_interface *intrfc);

static inline void __ipxitf_put(ipx_interface *intrfc)
{
	if (atomic_dec_and_test(&intrfc->refcnt))
		__ipxitf_down(intrfc);
}
/*
 * Note: Sockets may not be removed _during_ an interrupt or inet_bh
 * handler using this technique. They can be added although we do not
 * use this facility.
 */

void ipx_remove_socket(struct sock *sk)
{
	struct sock *s;
	ipx_interface *intrfc;

	/* Determine interface with which socket is associated */
	intrfc = sk->protinfo.af_ipx.intrfc;
	if (!intrfc)
		return;

	ipxitf_hold(intrfc);
	spin_lock_bh(&intrfc->if_sklist_lock);
	s = intrfc->if_sklist;
	if (s == sk) {
		intrfc->if_sklist = s->next;
		goto out;
	}

	while (s && s->next) {
		if (s->next == sk) {
			s->next = sk->next;
			goto out;
		}
		s = s->next;
	}
out:	spin_unlock_bh(&intrfc->if_sklist_lock);
	sock_put(sk);
	ipxitf_put(intrfc);
}

static void ipx_destroy_socket(struct sock *sk)
{
	ipx_remove_socket(sk);
	skb_queue_purge(&sk->receive_queue);
#ifdef IPX_REFCNT_DEBUG
        atomic_dec(&ipx_sock_nr);
        printk(KERN_DEBUG "IPX socket %p released, %d are still alive\n", sk,
			atomic_read(&ipx_sock_nr));
	if (atomic_read(&sk->refcnt) != 1)
		printk(KERN_DEBUG "Destruction sock ipx %p delayed, cnt=%d\n",
				sk, atomic_read(&sk->refcnt));
#endif
	sock_put(sk);
}

/* 
 * The following code is used to support IPX Interfaces (IPXITF).  An
 * IPX interface is defined by a physical device and a frame type.
 */
static ipx_route * ipxrtr_lookup(__u32);

/* ipxitf_clear_primary_net has to be called with ipx_interfaces_lock held */

static void ipxitf_clear_primary_net(void)
{
	if (ipxcfg_auto_select_primary && ipx_interfaces)
		ipx_primary_net = ipx_interfaces;
	else
		ipx_primary_net = NULL;
}

static ipx_interface *__ipxitf_find_using_phys(struct net_device *dev,
						unsigned short datalink)
{
	ipx_interface *i;

	for (i = ipx_interfaces;
		i && (i->if_dev != dev || i->if_dlink_type != datalink);
		i = i->if_next)
		;

	return i;
}

static ipx_interface *ipxitf_find_using_phys(struct net_device *dev,
						unsigned short datalink)
{
	ipx_interface *i;

	spin_lock_bh(&ipx_interfaces_lock);
	i = __ipxitf_find_using_phys(dev, datalink);
	if (i)
		ipxitf_hold(i);
	spin_unlock_bh(&ipx_interfaces_lock);
	return i;
}

static ipx_interface *ipxitf_find_using_net(__u32 net)
{
	ipx_interface *i;

	spin_lock_bh(&ipx_interfaces_lock);
	if (net)
		for (i = ipx_interfaces; i && i->if_netnum != net;
		     i = i->if_next)
		;
	else
		i = ipx_primary_net;
	if (i)
		ipxitf_hold(i);
	spin_unlock_bh(&ipx_interfaces_lock);

	return i;
}

/* Sockets are bound to a particular IPX interface. */
static void ipxitf_insert_socket(ipx_interface *intrfc, struct sock *sk)
{
	struct sock *s;

	ipxitf_hold(intrfc);
	sock_hold(sk);
	spin_lock_bh(&intrfc->if_sklist_lock);
	sk->protinfo.af_ipx.intrfc = intrfc;
	sk->next = NULL;
	if (!intrfc->if_sklist)
		intrfc->if_sklist = sk;
	else {
		for (s = intrfc->if_sklist; s->next; s = s->next)
			;
		s->next = sk;
	}
	spin_unlock_bh(&intrfc->if_sklist_lock);
	ipxitf_put(intrfc);
}

/* caller must hold intrfc->if_sklist_lock */
static struct sock *__ipxitf_find_socket(ipx_interface *intrfc,
					 unsigned short port)
{
	struct sock *s;

	for (s = intrfc->if_sklist;
	     s && s->protinfo.af_ipx.port != port;
	     s = s->next)
		;
	return s;
}

/* caller must hold a reference to intrfc */
static struct sock *ipxitf_find_socket(ipx_interface *intrfc,
					unsigned short port)
{
	struct sock *s;

	spin_lock_bh(&intrfc->if_sklist_lock);
	s = __ipxitf_find_socket(intrfc, port);
	if (s)
		sock_hold(s);
	spin_unlock_bh(&intrfc->if_sklist_lock);

	return s;
}

#ifdef CONFIG_IPX_INTERN
static struct sock *ipxitf_find_internal_socket(ipx_interface *intrfc,
			    unsigned char *node, unsigned short port)
{
	struct sock *s;

	ipxitf_hold(intrfc);
	spin_lock_bh(&intrfc->if_sklist_lock);
	s = intrfc->if_sklist;

	while (s) {
		if (s->protinfo.af_ipx.port == port &&
		    !memcmp(node, s->protinfo.af_ipx.node, IPX_NODE_LEN))
			break;
		s = s->next;
	}
	spin_unlock_bh(&intrfc->if_sklist_lock);
	ipxitf_put(intrfc);

	return s;
}
#endif

static void ipxrtr_del_routes(ipx_interface *);

static void __ipxitf_down(ipx_interface *intrfc)
{
	ipx_interface *i;
	struct sock *s, *t;

	/* Delete all routes associated with this interface */
	ipxrtr_del_routes(intrfc);

	spin_lock_bh(&intrfc->if_sklist_lock);
	/* error sockets */
	for (s = intrfc->if_sklist; s; ) {
		s->err = ENOLINK;
		s->error_report(s);
		s->protinfo.af_ipx.intrfc = NULL;
		s->protinfo.af_ipx.port = 0;
		s->zapped=1;	/* Indicates it is no longer bound */
		t = s;
		s = s->next;
		t->next = NULL;
	}
	intrfc->if_sklist = NULL;
	spin_unlock_bh(&intrfc->if_sklist_lock);

	/* remove this interface from list */
	if (intrfc == ipx_interfaces)
		ipx_interfaces = intrfc->if_next;
	else {
		for (i = ipx_interfaces;
		     i && i->if_next != intrfc;
		     i = i->if_next)
			;
		if (i && i->if_next == intrfc)
			i->if_next = intrfc->if_next;
	}

	/* remove this interface from *special* networks */
	if (intrfc == ipx_primary_net)
		ipxitf_clear_primary_net();
	if (intrfc == ipx_internal_net)
		ipx_internal_net = NULL;

	if (intrfc->if_dev)
		dev_put(intrfc->if_dev);
	kfree(intrfc);
	MOD_DEC_USE_COUNT;
}

static void ipxitf_down(ipx_interface *intrfc)
{
	spin_lock_bh(&ipx_interfaces_lock);
	__ipxitf_down(intrfc);
	spin_unlock_bh(&ipx_interfaces_lock);
}

static int ipxitf_device_event(struct notifier_block *notifier,
				unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;
	ipx_interface *i, *tmp;

	if (event != NETDEV_DOWN && event != NETDEV_UP)
		goto out;

	spin_lock_bh(&ipx_interfaces_lock);
	for (i = ipx_interfaces; i;) {
		tmp = i->if_next;
		if (i->if_dev == dev)
			if (event == NETDEV_UP)
				ipxitf_hold(i);
			else
				__ipxitf_put(i);
		i = tmp;
	}
	spin_unlock_bh(&ipx_interfaces_lock);
out:	return NOTIFY_DONE;
}

static void ipxitf_def_skb_handler(struct sock *sock, struct sk_buff *skb)
{
	if (sock_queue_rcv_skb(sock, skb) < 0)
		kfree_skb(skb);
}

/*
 * On input skb->sk is NULL. Nobody is charged for the memory.
 */

/* caller must hold a reference to intrfc */

#ifdef CONFIG_IPX_INTERN
static int ipxitf_demux_socket(ipx_interface *intrfc, struct sk_buff *skb, int copy)
{
	struct ipxhdr *ipx = skb->nh.ipxh;
	struct sock *s;
	int ret;

	int is_broadcast = (memcmp(ipx->ipx_dest.node, ipx_broadcast_node,
				   IPX_NODE_LEN) == 0);

	spin_lock_bh(&intrfc->if_sklist_lock);
	s = intrfc->if_sklist;

	while (s) {
		if (s->protinfo.af_ipx.port == ipx->ipx_dest.sock &&
		    (is_broadcast || !memcmp(ipx->ipx_dest.node,
					     s->protinfo.af_ipx.node,
					     IPX_NODE_LEN))) {
			/* We found a socket to which to send */
			struct sk_buff *skb1;

			if (copy) {
				skb1 = skb_clone(skb, GFP_ATOMIC);
				ret = -ENOMEM;
				if (!skb1)
					goto out;
			} else {
				skb1 = skb;
				copy = 1; /* skb may only be used once */
			}
			ipxitf_def_skb_handler(s, skb1);

			/* On an external interface, one socket can listen */
			if (intrfc != ipx_internal_net)
				break;
		}
		s = s->next;
	}

	/* skb was solely for us, and we did not make a copy, so free it. */
	if (!copy)
		kfree_skb(skb);

	ret = 0;
out:	spin_unlock_bh(&intrfc->if_sklist_lock);
	return ret;
}
#else
static int ipxitf_demux_socket(ipx_interface *intrfc, struct sk_buff *skb,
				int copy)
{
	struct ipxhdr *ipx = skb->nh.ipxh;
	struct sock *sock1 = NULL, *sock2 = NULL;
	struct sk_buff *skb1 = NULL, *skb2 = NULL;
	int ret;

	if (intrfc == ipx_primary_net && ntohs(ipx->ipx_dest.sock) == 0x451) {
		/* 
	 	 * The packet's target is a NCP connection handler. We want to
	 	 * hand it to the correct socket directly within the kernel,
	 	 * so that the mars_nwe packet distribution process
	 	 * does not have to do it. Here we only care about NCP and
	 	 * BURST packets.
	 	 * You might call this a hack, but believe me, you do not
	 	 * want a complete NCP layer in the kernel, and this is
	 	 * VERY fast as well.
	 	 */
	 	int connection = 0;
		u8 *ncphdr = (u8 *)(ipx + 1);

	 	if (*ncphdr == 0x22 && *(ncphdr + 1) == 0x22)
	  		/* The packet is a NCP request */
			connection = (((int) *(ncphdr + 5)) << 8) |
					(int) *(ncphdr+3);
		else if (*ncphdr == 0x77 && *(ncphdr + 1) == 0x77)
			/* The packet is a BURST packet */
			connection = (((int) *(ncphdr+9)) << 8) |
					(int) *(ncphdr+8);

        	if (connection) {
			/* Now we have to look for a special NCP connection
			 * handling socket. Only these sockets have
			 * ipx_ncp_conn != 0, set by SIOCIPXNCPCONN. */
			spin_lock_bh(&intrfc->if_sklist_lock);
			for (sock1 = intrfc->if_sklist;
			     sock1 &&
			     sock1->protinfo.af_ipx.ipx_ncp_conn != connection;
			     sock1 = sock1->next);
			if (sock1)
				sock_hold(sock1);
			spin_unlock_bh(&intrfc->if_sklist_lock);
		}
        }
        if (!sock1)
		/* No special socket found, forward the packet the normal way */
		sock1 = ipxitf_find_socket(intrfc, ipx->ipx_dest.sock);

	/*
	 * We need to check if there is a primary net and if
	 * this is addressed to one of the *SPECIAL* sockets because
	 * these need to be propagated to the primary net.
	 * The *SPECIAL* socket list contains: 0x452(SAP), 0x453(RIP) and
	 * 0x456(Diagnostic).
	 */

	if (ipx_primary_net && intrfc != ipx_primary_net) {
		switch (ntohs(ipx->ipx_dest.sock)) {
			case 0x452:
			case 0x453:
			case 0x456:
				/*
				 * The appropriate thing to do here is to
				 * dup the packet and route to the primary net
				 * interface via ipxitf_send; however, we'll 
				 * cheat and just demux it here.
				 */
				sock2 = ipxitf_find_socket(ipx_primary_net,
					ipx->ipx_dest.sock);
				break;
			default:
				break;
		}
	}

	/*
	 * If there is nothing to do return. The kfree will cancel any charging.
	 */
	if (!sock1 && !sock2) {
		if (!copy)
			kfree_skb(skb);
		return 0;
	}

	/*
	 * This next segment of code is a little awkward, but it sets it up
	 * so that the appropriate number of copies of the SKB are made and
	 * that skb1 and skb2 point to it (them) so that it (they) can be
	 * demuxed to sock1 and/or sock2.  If we are unable to make enough
	 * copies, we do as much as is possible.
	 */

	if (copy)
		skb1 = skb_clone(skb, GFP_ATOMIC);
	else
		skb1 = skb;

	ret = -ENOMEM;
	if (!skb1)
		goto out;

	/* Do we need 2 SKBs? */
	if (sock1 && sock2)
		skb2 = skb_clone(skb1, GFP_ATOMIC);
	else
		skb2 = skb1;

	if (sock1)
		ipxitf_def_skb_handler(sock1, skb1);

	ret = -ENOMEM;
	if (!skb2)
		goto out;

	if (sock2)
		ipxitf_def_skb_handler(sock2, skb2);

	ret = 0;
out:	if (sock1)
		sock_put(sock1);
	if (sock2)
		sock_put(sock2);
	return ret;
}
#endif	/* CONFIG_IPX_INTERN */

static struct sk_buff *ipxitf_adjust_skbuff(ipx_interface *intrfc,
					    struct sk_buff *skb)
{
	struct sk_buff *skb2;
	int in_offset = skb->h.raw - skb->head;
	int out_offset = intrfc->if_ipx_offset;
	int len;

	/* Hopefully, most cases */
	if (in_offset >= out_offset)
		return skb;

	/* Need new SKB */
	len  = skb->len + out_offset;
	skb2 = alloc_skb(len, GFP_ATOMIC);
	if (skb2) {
		skb_reserve(skb2, out_offset);
		skb2->nh.raw =
		skb2->h.raw = skb_put(skb2,skb->len);
		memcpy(skb2->h.raw, skb->h.raw, skb->len);
		memcpy(skb2->cb, skb->cb, sizeof(skb->cb));
	}
	kfree_skb(skb);
	return skb2;
}

/* caller must hold a reference to intrfc */

static int ipxitf_send(ipx_interface *intrfc, struct sk_buff *skb, char *node)
{
	struct ipxhdr *ipx = skb->nh.ipxh;
	struct ipx_cb *cb = (struct ipx_cb *) skb->cb;
	struct net_device *dev = intrfc->if_dev;
	struct datalink_proto *dl = intrfc->if_dlink;
	char dest_node[IPX_NODE_LEN];
	int send_to_wire = 1;
	int addr_len;
	
	/* 
	 * We need to know how many skbuffs it will take to send out this
	 * packet to avoid unnecessary copies.
	 */
	 
	if (!dl || !dev || dev->flags & IFF_LOOPBACK) 
		send_to_wire = 0;	/* No non looped */

	/*
	 * See if this should be demuxed to sockets on this interface 
	 *
	 * We want to ensure the original was eaten or that we only use
	 * up clones.
	 */
	 
	if (cb->ipx_dest_net == intrfc->if_netnum) {
		/*
		 * To our own node, loop and free the original.
		 * The internal net will receive on all node address.
		 */
		if (intrfc == ipx_internal_net ||
		    !memcmp(intrfc->if_node, node, IPX_NODE_LEN)) {
			/* Don't charge sender */
			skb_orphan(skb);

			/* Will charge receiver */
			return ipxitf_demux_socket(intrfc, skb, 0);
		}

		/* Broadcast, loop and possibly keep to send on. */
		if (!memcmp(ipx_broadcast_node, node, IPX_NODE_LEN)) {
			if (!send_to_wire)
				skb_orphan(skb);
			ipxitf_demux_socket(intrfc, skb, send_to_wire);
			if (!send_to_wire)
				return 0;
		}
	}

	/*
	 * If the originating net is not equal to our net; this is routed
	 * We are still charging the sender. Which is right - the driver
	 * free will handle this fairly.
	 */
	if (cb->ipx_source_net != intrfc->if_netnum) {
		/*
		 * Unshare the buffer before modifying the count in
		 * case its a flood or tcpdump
		 */
		skb = skb_unshare(skb, GFP_ATOMIC);
		if (!skb)
			return 0;
		if (++(cb->ipx_tctrl) > ipxcfg_max_hops)
			send_to_wire = 0;
	}

	if (!send_to_wire) {
		kfree_skb(skb);
		return 0;
	}

	/* Determine the appropriate hardware address */
	addr_len = dev->addr_len;
	if (!memcmp(ipx_broadcast_node, node, IPX_NODE_LEN))
		memcpy(dest_node, dev->broadcast, addr_len);
	else
		memcpy(dest_node, &(node[IPX_NODE_LEN-addr_len]), addr_len);

	/* Make any compensation for differing physical/data link size */
	skb = ipxitf_adjust_skbuff(intrfc, skb);
	if (!skb)
		return 0;

	ipx->ipx_tctrl = cb->ipx_tctrl;
	ipx->ipx_dest.net = cb->ipx_dest_net;
	ipx->ipx_source.net = cb->ipx_source_net;
	/* see if we need to include the netnum in the route list */
	if (cb->last_hop_index >= 0) {
		u32 *last_hop = (u32 *)(((u8 *) skb->data) +
				sizeof(struct ipxhdr) + cb->last_hop_index *
				sizeof(u32));
		*last_hop = intrfc->if_netnum;
	}

	/* set up data link and physical headers */
	skb->dev = dev;
	skb->protocol = htons(ETH_P_IPX);
	dl->datalink_header(dl, skb, dest_node);

	/* Send it out */
	dev_queue_xmit(skb);
	return 0;
}

static int ipxrtr_add_route(__u32, ipx_interface *, unsigned char *);

static int ipxitf_add_local_route(ipx_interface *intrfc)
{
	return ipxrtr_add_route(intrfc->if_netnum, intrfc, NULL);
}

static const char * ipx_frame_name(unsigned short);
static const char * ipx_device_name(ipx_interface *);

static int ipxitf_rcv(ipx_interface *intrfc, struct sk_buff *skb)
{
	struct ipxhdr	*ipx = skb->nh.ipxh;
	struct ipx_cb *cb = (struct ipx_cb *) skb->cb;
	int ret = 0;

	ipxitf_hold(intrfc);

	/* See if we should update our network number */
	if (!intrfc->if_netnum && /* net number of intrfc not known yet */
	   cb->ipx_source_net == cb->ipx_dest_net && /* intra packet */
	   cb->ipx_source_net) {
		ipx_interface *i = ipxitf_find_using_net(cb->ipx_source_net);
		/* NB: NetWare servers lie about their hop count so we
		 * dropped the test based on it.  This is the best way
		 * to determine this is a 0 hop count packet.
		 */
		if (!i) {
			intrfc->if_netnum = cb->ipx_source_net;
			ipxitf_add_local_route(intrfc);
		} else {
			printk(KERN_WARNING "IPX: Network number collision %lx\n        %s %s and %s %s\n",
				(long unsigned int) htonl(cb->ipx_source_net),
				ipx_device_name(i),
				ipx_frame_name(i->if_dlink_type),
				ipx_device_name(intrfc),
				ipx_frame_name(intrfc->if_dlink_type));
			ipxitf_put(i);
		}
	}
	
	cb->last_hop_index = -1;

	if (ipx->ipx_type == IPX_TYPE_PPROP && cb->ipx_tctrl < 8 &&
	    skb->pkt_type != PACKET_OTHERHOST &&
	    /* header + 8 network numbers */ 
	    ntohs(ipx->ipx_pktsize) >= sizeof(struct ipxhdr) + 8 * 4) {
		int i;
        	ipx_interface *ifcs;
		struct sk_buff *skb2;
		char *c = ((char *) skb->data) + sizeof(struct ipxhdr);
  		u32 *l = (u32 *) c;

		/* Dump packet if already seen this net */
		for (i = 0; i < cb->ipx_tctrl; i++)
			if (*l++ == intrfc->if_netnum)
				break;

		if (i == cb->ipx_tctrl) {
			/* < 8 hops && input itfc not in list */
 			/* insert recvd netnum into list */
			cb->last_hop_index = i;
			cb->ipx_tctrl++;
			/* xmit on all other interfaces... */
			spin_lock_bh(&ipx_interfaces_lock);
			for (ifcs = ipx_interfaces; ifcs;
			     ifcs = ifcs->if_next) {
				/* Except unconfigured interfaces */
				if (!ifcs->if_netnum)
					continue;
					
				/* That aren't in the list */
				l = (__u32 *) c;
				for (i = 0; i <= cb->ipx_tctrl; i++)
					if (ifcs->if_netnum == *l++)
						break;
				if (i - 1 == cb->ipx_tctrl) {
					cb->ipx_dest_net = ifcs->if_netnum;
					skb2=skb_clone(skb, GFP_ATOMIC);
					if (skb2)
						ipxrtr_route_skb(skb2);
				}
			}
			spin_unlock_bh(&ipx_interfaces_lock);
		}
	}

	if (!cb->ipx_dest_net)
		cb->ipx_dest_net = intrfc->if_netnum;
	if (!cb->ipx_source_net)
		cb->ipx_source_net = intrfc->if_netnum;

	if (intrfc->if_netnum != cb->ipx_dest_net) {
		/* We only route point-to-point packets. */
		if (skb->pkt_type == PACKET_HOST) {
			skb=skb_unshare(skb, GFP_ATOMIC);
			if (skb)
				ret = ipxrtr_route_skb(skb);
			goto out_intrfc;
		}

		goto out_free_skb;
	}

	/* see if we should keep it */
	if (!memcmp(ipx_broadcast_node, ipx->ipx_dest.node, IPX_NODE_LEN) ||
	    !memcmp(intrfc->if_node, ipx->ipx_dest.node, IPX_NODE_LEN)) {
		ret = ipxitf_demux_socket(intrfc, skb, 0);
		goto out_intrfc;
	}

	/* we couldn't pawn it off so unload it */
out_free_skb:
	kfree_skb(skb);
out_intrfc:
	ipxitf_put(intrfc);
	return ret;
}

static void ipxitf_insert(ipx_interface *intrfc)
{
	ipx_interface *i;

	intrfc->if_next = NULL;
	spin_lock_bh(&ipx_interfaces_lock);
	if (!ipx_interfaces)
		ipx_interfaces = intrfc;
	else {
		for (i = ipx_interfaces; i->if_next; i = i->if_next)
			;
		i->if_next = intrfc;
	}
	spin_unlock_bh(&ipx_interfaces_lock);

	if (ipxcfg_auto_select_primary && !ipx_primary_net)
		ipx_primary_net = intrfc;
}

static int ipxitf_create_internal(ipx_interface_definition *idef)
{
	ipx_interface *intrfc;
	int ret;

	/* Only one primary network allowed */
	if (ipx_primary_net)
		return -EEXIST;

	/* Must have a valid network number */
	if (!idef->ipx_network)
		return -EADDRNOTAVAIL;
	intrfc = ipxitf_find_using_net(idef->ipx_network);
	if (intrfc) {
		ipxitf_put(intrfc);
		return -EADDRINUSE;
	}

	intrfc = kmalloc(sizeof(ipx_interface),GFP_ATOMIC);
	if (!intrfc)
		return -EAGAIN;
	intrfc->if_dev		= NULL;
	intrfc->if_netnum	= idef->ipx_network;
	intrfc->if_dlink_type 	= 0;
	intrfc->if_dlink 	= NULL;
	intrfc->if_sklist 	= NULL;
	intrfc->if_internal 	= 1;
	intrfc->if_ipx_offset 	= 0;
	intrfc->if_sknum 	= IPX_MIN_EPHEMERAL_SOCKET;
	memcpy((char *)&(intrfc->if_node), idef->ipx_node, IPX_NODE_LEN);
	ipx_internal_net = ipx_primary_net = intrfc;
	spin_lock_init(&intrfc->if_sklist_lock);
	atomic_set(&intrfc->refcnt, 1);
	MOD_INC_USE_COUNT;
	ipxitf_hold(intrfc);
	ipxitf_insert(intrfc);

	ret = ipxitf_add_local_route(intrfc);
	ipxitf_put(intrfc);
	return ret;
}

static int ipx_map_frame_type(unsigned char type)
{
	switch (type) {
		case IPX_FRAME_ETHERII:
			return htons(ETH_P_IPX);

		case IPX_FRAME_8022:
			return htons(ETH_P_802_2);

		case IPX_FRAME_SNAP:
			return htons(ETH_P_SNAP);

		case IPX_FRAME_8023:
			return htons(ETH_P_802_3);
	}

	return 0;
}

static int ipxitf_create(ipx_interface_definition *idef)
{
	struct net_device *dev;
	unsigned short dlink_type = 0;
	struct datalink_proto *datalink = NULL;
	ipx_interface *intrfc;
	int err;

	if (idef->ipx_special == IPX_INTERNAL)
		return ipxitf_create_internal(idef);

	if (idef->ipx_special == IPX_PRIMARY && ipx_primary_net)
		return -EEXIST;

	intrfc = ipxitf_find_using_net(idef->ipx_network);
	if (idef->ipx_network && intrfc) {
		ipxitf_put(intrfc);
		return -EADDRINUSE;
	}

	if (intrfc)
		ipxitf_put(intrfc);

	dev = dev_get_by_name(idef->ipx_device);
	if (!dev)
		return -ENODEV;

	switch (idef->ipx_dlink_type) {
		case IPX_FRAME_TR_8022:
			printk(KERN_WARNING "IPX frame type 802.2TR is "
				"obsolete Use 802.2 instead.\n");
			/* fall through */

		case IPX_FRAME_8022:
			dlink_type 	= htons(ETH_P_802_2);
			datalink 	= p8022_datalink;
			break;

		case IPX_FRAME_ETHERII:
			if (dev->type != ARPHRD_IEEE802) {
				dlink_type 	= htons(ETH_P_IPX);
				datalink 	= pEII_datalink;
				break;
			} else 
				printk(KERN_WARNING "IPX frame type EtherII "
					"over token-ring is obsolete. Use SNAP "
					"instead.\n");
			/* fall through */

		case IPX_FRAME_SNAP:
			dlink_type 	= htons(ETH_P_SNAP);
			datalink 	= pSNAP_datalink;
			break;

		case IPX_FRAME_8023:
			dlink_type 	= htons(ETH_P_802_3);
			datalink 	= p8023_datalink;
			break;

		case IPX_FRAME_NONE:
		default:
			break;
	}

	err = -ENETDOWN;
	if (!(dev->flags & IFF_UP))
		goto out_dev;

	/* Check addresses are suitable */
	err = -EINVAL;
	if (dev->addr_len > IPX_NODE_LEN)
		goto out_dev;

	err = -EPROTONOSUPPORT;
	if (!datalink)
		goto out_dev;

	intrfc = ipxitf_find_using_phys(dev, dlink_type);
	if (!intrfc) {
		/* Ok now create */
		intrfc = kmalloc(sizeof(ipx_interface), GFP_ATOMIC);
		err = -EAGAIN;
		if (!intrfc)
			goto out_dev;
		intrfc->if_dev		= dev;
		intrfc->if_netnum 	= idef->ipx_network;
		intrfc->if_dlink_type 	= dlink_type;
		intrfc->if_dlink 	= datalink;
		intrfc->if_sklist 	= NULL;
		intrfc->if_sknum 	= IPX_MIN_EPHEMERAL_SOCKET;
		/* Setup primary if necessary */
		if (idef->ipx_special == IPX_PRIMARY)
			ipx_primary_net = intrfc;
		intrfc->if_internal 	= 0;
		intrfc->if_ipx_offset 	= dev->hard_header_len + datalink->header_length;
		if (!memcmp(idef->ipx_node, "\000\000\000\000\000\000",
			    IPX_NODE_LEN)) {
			memset(intrfc->if_node, 0, IPX_NODE_LEN);
			memcpy((char *)&(intrfc->if_node[IPX_NODE_LEN-dev->addr_len]),
				dev->dev_addr, dev->addr_len);
		} else
			memcpy(intrfc->if_node, idef->ipx_node, IPX_NODE_LEN);
		spin_lock_init(&intrfc->if_sklist_lock);
		atomic_set(&intrfc->refcnt, 1);
		MOD_INC_USE_COUNT;
		ipxitf_hold(intrfc);
		ipxitf_insert(intrfc);
	}


	/* If the network number is known, add a route */
	err = 0;
	if (!intrfc->if_netnum)
		goto out_intrfc;

	err = ipxitf_add_local_route(intrfc);
out_intrfc:
	ipxitf_put(intrfc);
	return err;
out_dev:
	dev_put(dev);
	return err;
}

static int ipxitf_delete(ipx_interface_definition *idef)
{
	struct net_device *dev = NULL;
	unsigned short dlink_type = 0;
	ipx_interface *intrfc;
	int ret = 0;

	spin_lock_bh(&ipx_interfaces_lock);
	if (idef->ipx_special == IPX_INTERNAL) {
		if (ipx_internal_net) {
			__ipxitf_put(ipx_internal_net);
			goto out;
		}
		ret = -ENOENT;
		goto out;
	}

	dlink_type = ipx_map_frame_type(idef->ipx_dlink_type);
	if (!dlink_type) {
		ret = -EPROTONOSUPPORT;
		goto out;
	}

	dev = __dev_get_by_name(idef->ipx_device);
	if (!dev) {
		ret = -ENODEV;
		goto out;
	}

	intrfc = __ipxitf_find_using_phys(dev, dlink_type);
	if (intrfc)
		__ipxitf_put(intrfc);
	else
		ret = -EINVAL;

out:	spin_unlock_bh(&ipx_interfaces_lock);
	return ret;
}

static ipx_interface *ipxitf_auto_create(struct net_device *dev, 
					 unsigned short dlink_type)
{
	struct datalink_proto *datalink = NULL;
	ipx_interface *intrfc;

	switch (htons(dlink_type)) {
		case ETH_P_IPX:
			datalink = pEII_datalink;
			break;

		case ETH_P_802_2:
			datalink = p8022_datalink;
			break;

		case ETH_P_SNAP:
			datalink = pSNAP_datalink;
			break;

		case ETH_P_802_3:
			datalink = p8023_datalink;
			break;

		default:
			return NULL;
	}

	if (!dev)
		return NULL;

	/* Check addresses are suitable */
	if (dev->addr_len > IPX_NODE_LEN)
		return NULL;

	intrfc = kmalloc(sizeof(ipx_interface), GFP_ATOMIC);
	if (intrfc) {
		intrfc->if_dev		= dev;
		intrfc->if_netnum	= 0;
		intrfc->if_dlink_type 	= dlink_type;
		intrfc->if_dlink 	= datalink;
		intrfc->if_sklist 	= NULL;
		intrfc->if_internal 	= 0;
		intrfc->if_sknum 	= IPX_MIN_EPHEMERAL_SOCKET;
		intrfc->if_ipx_offset 	= dev->hard_header_len +
						datalink->header_length;
		memset(intrfc->if_node, 0, IPX_NODE_LEN);
		memcpy((char *)&(intrfc->if_node[IPX_NODE_LEN-dev->addr_len]),
			dev->dev_addr, dev->addr_len);
		spin_lock_init(&intrfc->if_sklist_lock);
		atomic_set(&intrfc->refcnt, 1);
		MOD_INC_USE_COUNT;
		ipxitf_insert(intrfc);
		dev_hold(dev);
	}

	return intrfc;
}

static int ipxitf_ioctl(unsigned int cmd, void *arg)
{
	struct ifreq ifr;
	int err = 0, val;

	switch (cmd) {
		case SIOCSIFADDR: {
			struct sockaddr_ipx *sipx;
			ipx_interface_definition f;

			if (copy_from_user(&ifr, arg, sizeof(ifr)))
				return -EFAULT;

			sipx = (struct sockaddr_ipx *)&ifr.ifr_addr;
			if (sipx->sipx_family != AF_IPX)
				return -EINVAL;

			f.ipx_network = sipx->sipx_network;
			memcpy(f.ipx_device,ifr.ifr_name,sizeof(f.ipx_device));
			memcpy(f.ipx_node, sipx->sipx_node, IPX_NODE_LEN);
			f.ipx_dlink_type = sipx->sipx_type;
			f.ipx_special = sipx->sipx_special;

			if (sipx->sipx_action == IPX_DLTITF)
				return ipxitf_delete(&f);
			else
				return ipxitf_create(&f);
		}

		case SIOCGIFADDR: 
		{
			struct sockaddr_ipx *sipx;
			ipx_interface *ipxif;
			struct net_device *dev;

			if (copy_from_user(&ifr, arg, sizeof(ifr)))
				return -EFAULT;

			sipx = (struct sockaddr_ipx *)&ifr.ifr_addr;
			dev = __dev_get_by_name(ifr.ifr_name);
			if (!dev)
				return -ENODEV;

			ipxif = ipxitf_find_using_phys(dev, ipx_map_frame_type(sipx->sipx_type));
			if (!ipxif)
				return -EADDRNOTAVAIL;

			sipx->sipx_family	= AF_IPX;
			sipx->sipx_network	= ipxif->if_netnum;
			memcpy(sipx->sipx_node, ipxif->if_node,
				sizeof(sipx->sipx_node));
			if (copy_to_user(arg, &ifr, sizeof(ifr)))
				err = -EFAULT;

			ipxitf_put(ipxif);
			return err;
		}

		case SIOCAIPXITFCRT: 
			if (get_user(val, (unsigned char *) arg))
				return -EFAULT;
			ipxcfg_set_auto_create(val);
			break;

		case SIOCAIPXPRISLT: 
			if (get_user(val, (unsigned char *) arg))
				return -EFAULT;
			ipxcfg_set_auto_select(val);
			break;

		default:
			return -EINVAL;
	}

	return 0;
}

/**************************************************************************\
*                                                                          *
* Routing tables for the IPX socket layer.                                 *
*                                                                          *
\**************************************************************************/

static ipx_route *ipxrtr_lookup(__u32 net)
{
	ipx_route *r;

	read_lock_bh(&ipx_routes_lock);
	for (r = ipx_routes; r && r->ir_net != net; r = r->ir_next)
		;
	read_unlock_bh(&ipx_routes_lock);

	return r;
}

/* caller must hold a reference to intrfc */

static int ipxrtr_add_route(__u32 network, ipx_interface *intrfc, unsigned char *node)
{
	ipx_route *rt;

	/* Get a route structure; either existing or create */
	rt = ipxrtr_lookup(network);
	if (!rt) {
		rt = kmalloc(sizeof(ipx_route),GFP_ATOMIC);
		if (!rt)
			return -EAGAIN;

		write_lock_bh(&ipx_routes_lock);
		rt->ir_next	= ipx_routes;
		ipx_routes	= rt;
		write_unlock_bh(&ipx_routes_lock);
	}
	else if (intrfc == ipx_internal_net)
		return -EEXIST;

	rt->ir_net 	= network;
	rt->ir_intrfc 	= intrfc;
	if (!node) {
		memset(rt->ir_router_node, '\0', IPX_NODE_LEN);
		rt->ir_routed = 0;
	} else {
		memcpy(rt->ir_router_node, node, IPX_NODE_LEN);
		rt->ir_routed = 1;
	}

	return 0;
}

static void ipxrtr_del_routes(ipx_interface *intrfc)
{
	ipx_route **r, *tmp;

	write_lock_bh(&ipx_routes_lock);
	for (r = &ipx_routes; (tmp = *r) != NULL;) {
		if (tmp->ir_intrfc == intrfc) {
			*r = tmp->ir_next;
			kfree(tmp);
		} else
			r = &(tmp->ir_next);
	}
	write_unlock_bh(&ipx_routes_lock);
}

static int ipxrtr_create(ipx_route_definition *rd)
{
	ipx_interface *intrfc;
	int ret;

	/* Find the appropriate interface */
	intrfc = ipxitf_find_using_net(rd->ipx_router_network);
	if (!intrfc)
		return -ENETUNREACH;
	ret = ipxrtr_add_route(rd->ipx_network, intrfc, rd->ipx_router_node);
	ipxitf_put(intrfc);
	return ret;
}

static int ipxrtr_delete(long net)
{
	ipx_route **r;
	ipx_route *tmp;
	int err;

	write_lock_bh(&ipx_routes_lock);
	for (r = &ipx_routes; (tmp = *r) != NULL;) {
		if (tmp->ir_net == net) {
			/* Directly connected; can't lose route */
			err = -EPERM;
			if (!tmp->ir_routed)
				goto out;

			*r = tmp->ir_next;
			kfree(tmp);
			err = 0;
			goto out;
		}

		r = &(tmp->ir_next);
	}
	err = -ENOENT;
out:	write_unlock_bh(&ipx_routes_lock);
	return err;
}

/*
 *	Checksum routine for IPX
 */
 
/* Note: We assume ipx_tctrl==0 and htons(length)==ipx_pktsize */
/* This functions should *not* mess with packet contents */

static __u16 ipx_cksum(struct ipxhdr *packet,int length) 
{
	/* 
	 *	NOTE: sum is a net byte order quantity, which optimizes the 
	 *	loop. This only works on big and little endian machines. (I
	 *	don't know of a machine that isn't.)
	 */
	/* start at ipx_dest - We skip the checksum field and start with
	 * ipx_type before the loop, not considering ipx_tctrl in the calc */
	__u16 *p = (__u16 *)&packet->ipx_dest;
	__u32 i = (length >> 1) - 1; /* Number of complete words */
	__u32 sum = packet->ipx_type << sizeof(packet->ipx_tctrl); 

	/* Loop through all complete words except the checksum field,
	 * ipx_type (accounted above) and ipx_tctrl (not used in the cksum) */
	while (--i)
		sum += *p++;

	/* Add on the last part word if it exists */
	if (packet->ipx_pktsize & htons(1))
		sum += ntohs(0xff00) & *p;

	/* Do final fixup */
	sum = (sum & 0xffff) + (sum >> 16);

	/* It's a pity there's no concept of carry in C */
	if (sum >= 0x10000)
		sum++;

	return ~sum;
}

/*
 * Route an outgoing frame from a socket.
 */
static int ipxrtr_route_packet(struct sock *sk, struct sockaddr_ipx *usipx, struct iovec *iov, int len, int noblock)
{
	struct sk_buff *skb;
	ipx_interface *intrfc;
	struct ipxhdr *ipx;
	struct ipx_cb *cb;
	int size;
	int ipx_offset;
	ipx_route *rt = NULL;
	int err;

	/* Find the appropriate interface on which to send packet */
	if (!usipx->sipx_network && ipx_primary_net) {
		usipx->sipx_network = ipx_primary_net->if_netnum;
		intrfc = ipx_primary_net;
	} else {
		rt = ipxrtr_lookup(usipx->sipx_network);
		if (!rt)
			return -ENETUNREACH;

		intrfc = rt->ir_intrfc;
	}

	ipxitf_hold(intrfc);
	ipx_offset = intrfc->if_ipx_offset;
	size = sizeof(struct ipxhdr) + len + ipx_offset;

	skb = sock_alloc_send_skb(sk, size, 0, noblock, &err);
	if (!skb)
		goto out;

	skb_reserve(skb,ipx_offset);
	skb->sk = sk;
	cb = (struct ipx_cb *) skb->cb;

	/* Fill in IPX header */
	ipx = (struct ipxhdr *)skb_put(skb, sizeof(struct ipxhdr));
	ipx->ipx_pktsize= htons(len + sizeof(struct ipxhdr));
	cb->ipx_tctrl = 0;
	ipx->ipx_type 	= usipx->sipx_type;
	skb->h.raw 	= (void *)skb->nh.ipxh = ipx;

	cb->last_hop_index = -1;

#ifdef CONFIG_IPX_INTERN
	cb->ipx_source_net = sk->protinfo.af_ipx.intrfc->if_netnum;
	memcpy(ipx->ipx_source.node, sk->protinfo.af_ipx.node, IPX_NODE_LEN);
#else
	err = ntohs(sk->protinfo.af_ipx.port);
	if (err == 0x453 || err == 0x452) {
		/* RIP/SAP special handling for mars_nwe */
		cb->ipx_source_net = intrfc->if_netnum;
		memcpy(ipx->ipx_source.node, intrfc->if_node, IPX_NODE_LEN);
	} else {
		cb->ipx_source_net = sk->protinfo.af_ipx.intrfc->if_netnum;
		memcpy(ipx->ipx_source.node, sk->protinfo.af_ipx.intrfc->if_node, IPX_NODE_LEN);
	}
#endif	/* CONFIG_IPX_INTERN */

	ipx->ipx_source.sock 	= sk->protinfo.af_ipx.port;
	cb->ipx_dest_net = usipx->sipx_network;
	memcpy(ipx->ipx_dest.node,usipx->sipx_node,IPX_NODE_LEN);
	ipx->ipx_dest.sock	= usipx->sipx_port;

	err = memcpy_fromiovec(skb_put(skb,len),iov,len);
	if (err) {
		kfree_skb(skb);
		goto out;
	}	

	/* Apply checksum. Not allowed on 802.3 links. */
	if (sk->no_check || intrfc->if_dlink_type == IPX_FRAME_8023)
		ipx->ipx_checksum=0xFFFF;
	else
		ipx->ipx_checksum = ipx_cksum(ipx, len + sizeof(struct ipxhdr));

	err = ipxitf_send(intrfc, skb, (rt && rt->ir_routed) ? 
				rt->ir_router_node : ipx->ipx_dest.node);
out:	ipxitf_put(intrfc);
	return err;
}
	
int ipxrtr_route_skb(struct sk_buff *skb)
{
	struct ipxhdr *ipx = skb->nh.ipxh;
	struct ipx_cb *cb = (struct ipx_cb *) skb->cb;
	ipx_route *r = ipxrtr_lookup(cb->ipx_dest_net);

	if (!r) {	/* no known route */
		kfree_skb(skb);
		return 0;
	}

	ipxitf_hold(r->ir_intrfc);
	ipxitf_send(r->ir_intrfc, skb, (r->ir_routed) ?
			r->ir_router_node : ipx->ipx_dest.node);
	ipxitf_put(r->ir_intrfc);

	return 0;
}

/*
 * We use a normal struct rtentry for route handling
 */
static int ipxrtr_ioctl(unsigned int cmd, void *arg)
{
	struct rtentry rt;	/* Use these to behave like 'other' stacks */
	struct sockaddr_ipx *sg,*st;
	int err;

	err = copy_from_user(&rt,arg,sizeof(rt));
	if (err)
		return -EFAULT;

	sg = (struct sockaddr_ipx *)&rt.rt_gateway;
	st = (struct sockaddr_ipx *)&rt.rt_dst;

	if (!(rt.rt_flags & RTF_GATEWAY))
		return -EINVAL;	/* Direct routes are fixed */
	if (sg->sipx_family != AF_IPX)
		return -EINVAL;
	if (st->sipx_family != AF_IPX)
		return -EINVAL;

	switch (cmd) {
		case SIOCDELRT:
			return ipxrtr_delete(st->sipx_network);

		case SIOCADDRT:
		{
			struct ipx_route_definition f;
			f.ipx_network=st->sipx_network;
			f.ipx_router_network=sg->sipx_network;
			memcpy(f.ipx_router_node, sg->sipx_node, IPX_NODE_LEN);
			return ipxrtr_create(&f);
		}
	}

	return -EINVAL;
}

static const char *ipx_frame_name(unsigned short frame)
{
	switch (ntohs(frame)) {
		case ETH_P_IPX:
			return "EtherII";

		case ETH_P_802_2:
			return "802.2";

		case ETH_P_SNAP:
			return "SNAP";

		case ETH_P_802_3:
			return "802.3";

		case ETH_P_TR_802_2:
			return "802.2TR";

		default:
			return "None";
	}
}

static const char *ipx_device_name(ipx_interface *intrfc)
{
	return intrfc->if_internal ? "Internal" :
		intrfc->if_dev ? intrfc->if_dev->name : "Unknown";
}

/* Called from proc fs */
static int ipx_interface_get_info(char *buffer, char **start, off_t offset,
				  int length)
{
	ipx_interface *i;
	off_t begin = 0, pos = 0;
	int len = 0;

	/* Theory.. Keep printing in the same place until we pass offset */

	len += sprintf(buffer,"%-11s%-15s%-9s%-11s%s", "Network",
		"Node_Address", "Primary", "Device", "Frame_Type");
#ifdef IPX_REFCNT_DEBUG
	len += sprintf(buffer + len, "  refcnt");
#endif
	strcat(buffer+len++, "\n");
	spin_lock_bh(&ipx_interfaces_lock);
	for (i = ipx_interfaces; i; i = i->if_next) {
		len += sprintf(buffer+len, "%08lX   ", (long unsigned int)ntohl(i->if_netnum));
		len += sprintf(buffer+len,"%02X%02X%02X%02X%02X%02X   ",
				i->if_node[0], i->if_node[1], i->if_node[2],
				i->if_node[3], i->if_node[4], i->if_node[5]);
		len += sprintf(buffer+len, "%-9s", i == ipx_primary_net ?
			"Yes" : "No");
		len += sprintf(buffer+len, "%-11s", ipx_device_name(i));
		len += sprintf(buffer+len, "%-9s",
			ipx_frame_name(i->if_dlink_type));
#ifdef IPX_REFCNT_DEBUG
		len += sprintf(buffer+len,"%6d",atomic_read(&i->refcnt));
#endif
		strcat(buffer+len++, "\n");
		/* Are we still dumping unwanted data then discard the record */
		pos = begin + len;

		if (pos < offset) {
			len   = 0;	/* Keep dumping into the buffer start */
			begin = pos;
		}
		if (pos > offset + length)	/* We have dumped enough */
			break;
	}
	spin_unlock_bh(&ipx_interfaces_lock);

	/* The data in question runs from begin to begin+len */
	*start = buffer + (offset - begin);	/* Start of wanted data */
	len -= (offset - begin); /* Remove unwanted header data from length */
	if (len > length)
		len = length;	/* Remove unwanted tail data from length */

	return len;
}

static int ipx_get_info(char *buffer, char **start, off_t offset, int length)
{
	struct sock *s;
	ipx_interface *i;
	off_t begin = 0, pos = 0;
	int len = 0;

	/* Theory.. Keep printing in the same place until we pass offset */

#ifdef CONFIG_IPX_INTERN
	len += sprintf(buffer,"%-28s%-28s%-10s%-10s%-7s%s\n", "Local_Address",
#else
	len += sprintf(buffer,"%-15s%-28s%-10s%-10s%-7s%s\n", "Local_Address",
#endif	/* CONFIG_IPX_INTERN */
			"Remote_Address", "Tx_Queue", "Rx_Queue",
			"State", "Uid");

	spin_lock_bh(&ipx_interfaces_lock);
	for (i = ipx_interfaces; i; i = i->if_next) {
		ipxitf_hold(i);
		spin_lock_bh(&i->if_sklist_lock);
		for (s = i->if_sklist; s; s = s->next) {
#ifdef CONFIG_IPX_INTERN
			len += sprintf(buffer+len,
				       "%08lX:%02X%02X%02X%02X%02X%02X:%04X  ",
                                       (unsigned long) htonl(s->protinfo.af_ipx.intrfc->if_netnum),
				       s->protinfo.af_ipx.node[0],
				       s->protinfo.af_ipx.node[1],
				       s->protinfo.af_ipx.node[2],
				       s->protinfo.af_ipx.node[3],
				       s->protinfo.af_ipx.node[4],
				       s->protinfo.af_ipx.node[5],
				       htons(s->protinfo.af_ipx.port));
#else
			len += sprintf(buffer+len,"%08lX:%04X  ",
				       (unsigned long)htonl(i->if_netnum),
				       htons(s->protinfo.af_ipx.port));
#endif	/* CONFIG_IPX_INTERN */

			if (s->state != TCP_ESTABLISHED)
				len += sprintf(buffer+len, "%-28s", "Not_Connected");
			else {
				len += sprintf(buffer+len,
					"%08lX:%02X%02X%02X%02X%02X%02X:%04X  ",
					(unsigned long) htonl(s->protinfo.af_ipx.dest_addr.net),
					s->protinfo.af_ipx.dest_addr.node[0],
					s->protinfo.af_ipx.dest_addr.node[1],
					s->protinfo.af_ipx.dest_addr.node[2],
					s->protinfo.af_ipx.dest_addr.node[3],
					s->protinfo.af_ipx.dest_addr.node[4],
					s->protinfo.af_ipx.dest_addr.node[5],
					htons(s->protinfo.af_ipx.dest_addr.sock));
			}

			len += sprintf(buffer+len,"%08X  %08X  ",
				atomic_read(&s->wmem_alloc),
				atomic_read(&s->rmem_alloc));
			len += sprintf(buffer+len,"%02X     %03d\n",
				s->state, SOCK_INODE(s->socket)->i_uid);

			pos = begin + len;
			if (pos < offset) {
				len   = 0;
				begin = pos;
			}

			if (pos > offset + length)  /* We have dumped enough */
				break;
		}
		spin_unlock_bh(&i->if_sklist_lock);
		ipxitf_put(i);
	}
	spin_unlock_bh(&ipx_interfaces_lock);

	/* The data in question runs from begin to begin+len */
	*start = buffer + (offset-begin);
	len -= (offset - begin);
	if (len > length)
		len = length;

	return len;
}

static int ipx_rt_get_info(char *buffer, char **start, off_t offset, int length)
{
	ipx_route *rt;
	off_t begin = 0, pos = 0;
	int len = 0;

	len += sprintf(buffer,"%-11s%-13s%s\n",
			"Network", "Router_Net", "Router_Node");
	read_lock_bh(&ipx_routes_lock);
	for (rt = ipx_routes; rt; rt = rt->ir_next) {
		len += sprintf(buffer+len,"%08lX   ", (long unsigned int) ntohl(rt->ir_net));
		if (rt->ir_routed) {
			len += sprintf(buffer+len,"%08lX     %02X%02X%02X%02X%02X%02X\n",
				(long unsigned int) ntohl(rt->ir_intrfc->if_netnum),
				rt->ir_router_node[0], rt->ir_router_node[1],
				rt->ir_router_node[2], rt->ir_router_node[3],
				rt->ir_router_node[4], rt->ir_router_node[5]);
		} else {
			len += sprintf(buffer+len, "%-13s%s\n",
					"Directly", "Connected");
		}

		pos = begin + len;
		if (pos < offset) {
			len = 0;
			begin = pos;
		}

		if (pos > offset + length)
			break;
	}
	read_unlock_bh(&ipx_routes_lock);

	*start = buffer + (offset - begin);
	len -= (offset - begin);
	if (len > length)
		len = length;

	return len;
}

/**************************************************************************\
*                                                                          *
* Handling for system calls applied via the various interfaces to an       *
* IPX socket object.                                                       *
*                                                                          *
\**************************************************************************/

static int ipx_setsockopt(struct socket *sock, int level, int optname,
			  char *optval, int optlen)
{
	struct sock *sk = sock->sk;
	int opt;

	if (optlen != sizeof(int))
		return -EINVAL;

	if (get_user(opt, (unsigned int *)optval))
		return -EFAULT;

	switch (level) {
		case SOL_IPX:
			switch (optname) {
				case IPX_TYPE:
					sk->protinfo.af_ipx.type = opt;
					return 0;

				default:
					return -ENOPROTOOPT;
			}
			break;

		default:
			return -ENOPROTOOPT;
	}
}

static int ipx_getsockopt(struct socket *sock, int level, int optname,
	char *optval, int *optlen)
{
	struct sock *sk = sock->sk;
	int val = 0;
	int len;

	switch (level) {
		case SOL_IPX:
			switch (optname) {
				case IPX_TYPE:
					val = sk->protinfo.af_ipx.type;
					break;

				default:
					return -ENOPROTOOPT;
			}
			break;

		default:
			return -ENOPROTOOPT;
	}

	if (get_user(len, optlen))
		return -EFAULT;

	len = min(len, sizeof(int));
	
	if(len < 0)
		return -EINVAL;
		
	if (put_user(len, optlen))
		return -EFAULT;

	if (copy_to_user(optval, &val, len))
		return -EFAULT;

	return 0;
}

static int ipx_create(struct socket *sock, int protocol)
{
	struct sock *sk;

	switch (sock->type) {
		case SOCK_DGRAM:
			sk = sk_alloc(PF_IPX, GFP_KERNEL, 1);
			if (!sk)
                		return -ENOMEM;
                        sock->ops = &ipx_dgram_ops;
                        break;

		case SOCK_SEQPACKET:
			/*
			 * From this point on SPX sockets are handled
			 * by af_spx.c and the methods replaced.
			 */
			if (spx_family_ops)
				return spx_family_ops->create(sock,protocol);
			/* Fall through if SPX is not loaded */
		case SOCK_STREAM:       /* Allow higher levels to piggyback */
		default:
			return -ESOCKTNOSUPPORT;
	}
#ifdef IPX_REFCNT_DEBUG
        atomic_inc(&ipx_sock_nr);
        printk(KERN_DEBUG "IPX socket %p created, now we have %d alive\n", sk,
			atomic_read(&ipx_sock_nr));
#endif
	sock_init_data(sock, sk);
	sk->destruct	= NULL;
	sk->no_check 	= 1;		/* Checksum off by default */

	MOD_INC_USE_COUNT;
	return 0;
}

static int ipx_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	if (!sk)
		return 0;

	if (!sk->dead)
		sk->state_change(sk);

	sk->dead = 1;
	sock->sk = NULL;
	ipx_destroy_socket(sk);

	if (sock->type == SOCK_DGRAM)
		MOD_DEC_USE_COUNT;

	return 0;
}

/* caller must hold a reference to intrfc */

static unsigned short ipx_first_free_socketnum(ipx_interface *intrfc)
{
	unsigned short socketNum = intrfc->if_sknum;

	spin_lock_bh(&intrfc->if_sklist_lock);

	if (socketNum < IPX_MIN_EPHEMERAL_SOCKET)
		socketNum = IPX_MIN_EPHEMERAL_SOCKET;

	while (__ipxitf_find_socket(intrfc, ntohs(socketNum)))
		if (socketNum > IPX_MAX_EPHEMERAL_SOCKET)
			socketNum = IPX_MIN_EPHEMERAL_SOCKET;
		else
			socketNum++;

	spin_unlock_bh(&intrfc->if_sklist_lock);
	intrfc->if_sknum = socketNum;

	return ntohs(socketNum);
}

static int ipx_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk = sock->sk;
	ipx_interface *intrfc;
	struct sockaddr_ipx *addr = (struct sockaddr_ipx *)uaddr;
	int ret;

	if (!sk->zapped)
		return -EINVAL;

	if (addr_len != sizeof(struct sockaddr_ipx))
		return -EINVAL;

	intrfc = ipxitf_find_using_net(addr->sipx_network);
	if (!intrfc)
		return -EADDRNOTAVAIL;

	if (!addr->sipx_port) {
		addr->sipx_port = ipx_first_free_socketnum(intrfc);
		ret = -EINVAL;
		if (!addr->sipx_port)
			goto out;
	}

	/* protect IPX system stuff like routing/sap */
	ret = -EACCES;
	if (ntohs(addr->sipx_port) < IPX_MIN_EPHEMERAL_SOCKET &&
	    !capable(CAP_NET_ADMIN))
		goto out;

	sk->protinfo.af_ipx.port = addr->sipx_port;

#ifdef CONFIG_IPX_INTERN
	if (intrfc == ipx_internal_net) {
		/* The source address is to be set explicitly if the
		 * socket is to be bound on the internal network. If a
		 * node number 0 was specified, the default is used.
		 */

		ret = -EINVAL;
		if (!memcmp(addr->sipx_node,ipx_broadcast_node,IPX_NODE_LEN))
			goto out;
		if (!memcmp(addr->sipx_node, ipx_this_node, IPX_NODE_LEN))
			memcpy(sk->protinfo.af_ipx.node, intrfc->if_node,
			       IPX_NODE_LEN);
		else
			memcpy(sk->protinfo.af_ipx.node, addr->sipx_node,
				IPX_NODE_LEN);

		ret = -EADDRINUSE;
		if (ipxitf_find_internal_socket(intrfc,
						sk->protinfo.af_ipx.node,
						sk->protinfo.af_ipx.port)) {
			SOCK_DEBUG(sk,
				"IPX: bind failed because port %X in use.\n",
				ntohs((int)addr->sipx_port));
			goto out;
		}
	} else {
		/* Source addresses are easy. It must be our
		 * network:node pair for an interface routed to IPX
		 * with the ipx routing ioctl()
		 */

		memcpy(sk->protinfo.af_ipx.node, intrfc->if_node,
			IPX_NODE_LEN);

		ret = -EADDRINUSE;
		if (ipxitf_find_socket(intrfc, addr->sipx_port)) {
			SOCK_DEBUG(sk,
				"IPX: bind failed because port %X in use.\n",
				ntohs((int)addr->sipx_port));
			goto out;
		}
	}

#else	/* !def CONFIG_IPX_INTERN */

	/* Source addresses are easy. It must be our network:node pair for
	   an interface routed to IPX with the ipx routing ioctl() */

	ret = -EADDRINUSE;
	if (ipxitf_find_socket(intrfc, addr->sipx_port)) {
		SOCK_DEBUG(sk, "IPX: bind failed because port %X in use.\n",
				ntohs((int)addr->sipx_port));
		goto out;
	}

#endif	/* CONFIG_IPX_INTERN */

	ipxitf_insert_socket(intrfc, sk);
	sk->zapped = 0;
	SOCK_DEBUG(sk, "IPX: bound socket 0x%04X.\n", ntohs(addr->sipx_port) );

	ret = 0;
out:	ipxitf_put(intrfc);
	return ret;
}

static int ipx_connect(struct socket *sock, struct sockaddr *uaddr,
	int addr_len, int flags)
{
	struct sock *sk = sock->sk;
	struct sockaddr_ipx *addr;

	sk->state	= TCP_CLOSE;
	sock->state 	= SS_UNCONNECTED;

	if (addr_len != sizeof(*addr))
		return -EINVAL;
	addr = (struct sockaddr_ipx *)uaddr;

	/* put the autobinding in */
	if (!sk->protinfo.af_ipx.port) {
		struct sockaddr_ipx uaddr;
		int ret;

		uaddr.sipx_port		= 0;
		uaddr.sipx_network 	= 0;

#ifdef CONFIG_IPX_INTERN
		if (sk->protinfo.af_ipx.intrfc)
			memcpy(uaddr.sipx_node, sk->protinfo.af_ipx.intrfc->if_node,IPX_NODE_LEN);
		else
			return -ENETDOWN;		/* Someone zonked the iface */
#endif	/* CONFIG_IPX_INTERN */

		ret = ipx_bind(sock, (struct sockaddr *)&uaddr,
				sizeof(struct sockaddr_ipx));
		if (ret)
			return ret;
	}

        /* We can either connect to primary network or somewhere
	 * we can route to */
	if (!(!addr->sipx_network && ipx_primary_net) &&
	    !ipxrtr_lookup(addr->sipx_network))
		return -ENETUNREACH;

	sk->protinfo.af_ipx.dest_addr.net  = addr->sipx_network;
	sk->protinfo.af_ipx.dest_addr.sock = addr->sipx_port;
	memcpy(sk->protinfo.af_ipx.dest_addr.node,
		addr->sipx_node,IPX_NODE_LEN);
	sk->protinfo.af_ipx.type = addr->sipx_type;

	if (sock->type == SOCK_DGRAM) {
		sock->state 	= SS_CONNECTED;
		sk->state 	= TCP_ESTABLISHED;
	}

	return 0;
}


static int ipx_getname(struct socket *sock, struct sockaddr *uaddr,
			int *uaddr_len, int peer)
{
	ipx_address *addr;
	struct sockaddr_ipx sipx;
	struct sock *sk = sock->sk;

	*uaddr_len = sizeof(struct sockaddr_ipx);

	if (peer) {
		if (sk->state != TCP_ESTABLISHED)
			return -ENOTCONN;

		addr = &sk->protinfo.af_ipx.dest_addr;
		sipx.sipx_network = addr->net;
		memcpy(sipx.sipx_node,addr->node,IPX_NODE_LEN);
		sipx.sipx_port = addr->sock;
	} else {
		if (sk->protinfo.af_ipx.intrfc) {
			sipx.sipx_network=sk->protinfo.af_ipx.intrfc->if_netnum;
#ifdef CONFIG_IPX_INTERN
			memcpy(sipx.sipx_node, sk->protinfo.af_ipx.node, IPX_NODE_LEN);
#else
			memcpy(sipx.sipx_node, sk->protinfo.af_ipx.intrfc->if_node, IPX_NODE_LEN);
#endif	/* CONFIG_IPX_INTERN */

		} else {
			sipx.sipx_network = 0;
			memset(sipx.sipx_node, '\0', IPX_NODE_LEN);
		}

		sipx.sipx_port = sk->protinfo.af_ipx.port;
	}

	sipx.sipx_family = AF_IPX;
	sipx.sipx_type = sk->protinfo.af_ipx.type;
	memcpy(uaddr,&sipx,sizeof(sipx));

	return 0;
}

int ipx_rcv(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt)
{
	/* NULL here for pt means the packet was looped back */
	ipx_interface *intrfc;
	struct ipxhdr *ipx;
	struct ipx_cb *cb;
	u16 ipx_pktsize;
	int ret;
		
	/* Not ours */	
        if (skb->pkt_type == PACKET_OTHERHOST)
        	goto drop;

	if ((skb = skb_share_check(skb, GFP_ATOMIC)) == NULL)
		goto out;

	ipx = skb->nh.ipxh;
	ipx_pktsize = ntohs(ipx->ipx_pktsize);
	
	/* Too small or invalid header? */
	if (ipx_pktsize < sizeof(struct ipxhdr) || ipx_pktsize > skb->len)
		goto drop;
                        
	if (ipx->ipx_checksum != IPX_NO_CHECKSUM &&
	   ipx->ipx_checksum != ipx_cksum(ipx, ipx_pktsize))
		goto drop;

	cb = (struct ipx_cb *) skb->cb;
	cb->ipx_tctrl = ipx->ipx_tctrl;
	cb->ipx_dest_net = ipx->ipx_dest.net;
	cb->ipx_source_net = ipx->ipx_source.net;

	/* Determine what local ipx endpoint this is */
	intrfc = ipxitf_find_using_phys(dev, pt->type);
	if (!intrfc) {
		if (ipxcfg_auto_create_interfaces &&
		   ntohl(cb->ipx_dest_net)) {
			intrfc = ipxitf_auto_create(dev, pt->type);
			ipxitf_hold(intrfc);
		}

		if (!intrfc)	/* Not one of ours */
			goto drop;
	}

	ret = ipxitf_rcv(intrfc, skb);
	ipxitf_put(intrfc);
	return ret;
drop:	kfree_skb(skb);
out:	return 0;
}

static int ipx_sendmsg(struct socket *sock, struct msghdr *msg, int len,
	struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	struct sockaddr_ipx *usipx = (struct sockaddr_ipx *)msg->msg_name;
	struct sockaddr_ipx local_sipx;
	int retval;
	int flags = msg->msg_flags;

	/* Socket gets bound below anyway */
/*	if (sk->zapped)
		return -EIO; */	/* Socket not bound */
	if (flags & ~MSG_DONTWAIT)
		return -EINVAL;

	if (usipx) {
		if (!sk->protinfo.af_ipx.port) {
			struct sockaddr_ipx uaddr;
			int ret;

			uaddr.sipx_port = 0;
			uaddr.sipx_network = 0L;
#ifdef CONFIG_IPX_INTERN
			if (sk->protinfo.af_ipx.intrfc)
				memcpy(uaddr.sipx_node,
					sk->protinfo.af_ipx.intrfc->if_node,
					IPX_NODE_LEN);
			else
				return -ENETDOWN;               /* Someone zonked the iface */
#endif
			ret = ipx_bind(sock, (struct sockaddr *)&uaddr,
					sizeof(struct sockaddr_ipx));
			if (ret)
				return ret;
		}

		if (msg->msg_namelen < sizeof(*usipx))
			return -EINVAL;
		if (usipx->sipx_family != AF_IPX)
			return -EINVAL;
	} else {
		if (sk->state != TCP_ESTABLISHED)
			return -ENOTCONN;

		usipx=&local_sipx;
		usipx->sipx_family 	= AF_IPX;
		usipx->sipx_type 	= sk->protinfo.af_ipx.type;
		usipx->sipx_port 	= sk->protinfo.af_ipx.dest_addr.sock;
		usipx->sipx_network 	= sk->protinfo.af_ipx.dest_addr.net;
		memcpy(usipx->sipx_node,sk->protinfo.af_ipx.dest_addr.node,IPX_NODE_LEN);
	}

	retval = ipxrtr_route_packet(sk, usipx, msg->msg_iov, len,
				     flags & MSG_DONTWAIT);
	if (retval < 0)
		return retval;

	return len;
}


static int ipx_recvmsg(struct socket *sock, struct msghdr *msg, int size,
		int flags, struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	struct sockaddr_ipx *sipx = (struct sockaddr_ipx *)msg->msg_name;
	struct ipxhdr *ipx = NULL;
	struct sk_buff *skb;
	int copied, err;

	/* put the autobinding in */
	if (!sk->protinfo.af_ipx.port) {
		struct sockaddr_ipx uaddr;
		int ret;

		uaddr.sipx_port		= 0;
		uaddr.sipx_network 	= 0;

#ifdef CONFIG_IPX_INTERN
		if (sk->protinfo.af_ipx.intrfc)
			memcpy(uaddr.sipx_node, sk->protinfo.af_ipx.intrfc->if_node,IPX_NODE_LEN);
		else
			return -ENETDOWN;		/* Someone zonked the iface */
#endif	/* CONFIG_IPX_INTERN */

		ret = ipx_bind(sock, (struct sockaddr *)&uaddr,
				sizeof(struct sockaddr_ipx));
		if (ret)
			return ret;
	}
	
	if (sk->zapped)
		return -ENOTCONN;

	skb = skb_recv_datagram(sk,flags&~MSG_DONTWAIT,flags&MSG_DONTWAIT,&err);
	if (!skb)
		goto out;

	ipx 	= skb->nh.ipxh;
	copied 	= ntohs(ipx->ipx_pktsize) - sizeof(struct ipxhdr);
	if (copied > size) {
		copied=size;
		msg->msg_flags |= MSG_TRUNC;
	}

	err = skb_copy_datagram_iovec(skb, sizeof(struct ipxhdr), msg->msg_iov,
					copied);
	if (err)
		goto out_free;
	sk->stamp = skb->stamp;

	msg->msg_namelen = sizeof(*sipx);

	if (sipx) {
		struct ipx_cb *cb = (struct ipx_cb *) skb->cb;
		sipx->sipx_family	= AF_IPX;
		sipx->sipx_port		= ipx->ipx_source.sock;
		memcpy(sipx->sipx_node,ipx->ipx_source.node,IPX_NODE_LEN);
		sipx->sipx_network	= cb->ipx_source_net;
		sipx->sipx_type 	= ipx->ipx_type;
	}
	err = copied;

out_free:
	skb_free_datagram(sk, skb);
out:	return err;
}


static int ipx_ioctl(struct socket *sock,unsigned int cmd, unsigned long arg)
{
	long amount = 0;
	struct sock *sk = sock->sk;

	switch (cmd) {
		case TIOCOUTQ:
			amount = sk->sndbuf - atomic_read(&sk->wmem_alloc);
			if (amount < 0)
				amount = 0;
			return put_user(amount, (int *)arg);

		case TIOCINQ:
		{
			struct sk_buff *skb = skb_peek(&sk->receive_queue);
			/* These two are safe on a single CPU system as only user tasks fiddle here */
			if (skb)
				amount = skb->len - sizeof(struct ipxhdr);
			return put_user(amount, (int *)arg);
		}

		case SIOCADDRT:
		case SIOCDELRT:
			if (!capable(CAP_NET_ADMIN))
				return -EPERM;
			return ipxrtr_ioctl(cmd,(void *)arg);

		case SIOCSIFADDR:
		case SIOCAIPXITFCRT:
		case SIOCAIPXPRISLT:
			if (!capable(CAP_NET_ADMIN))
				return -EPERM;

		case SIOCGIFADDR:
			return ipxitf_ioctl(cmd,(void *)arg);

		case SIOCIPXCFGDATA:
			return ipxcfg_get_config_data((void *)arg);

		case SIOCIPXNCPCONN:
                {
			/*
			 * This socket wants to take care of the NCP connection
			 * handed to us in arg.
			 */
                	if (!capable(CAP_NET_ADMIN))
                		return -EPERM;
			return get_user(sk->protinfo.af_ipx.ipx_ncp_conn,
					(const unsigned short *)(arg));
                }

		case SIOCGSTAMP:
		{
			int ret = -EINVAL;
			if (sk) {
				if (!sk->stamp.tv_sec)
					return -ENOENT;
				ret = -EFAULT;
				if (!copy_to_user((void *)arg, &sk->stamp,
						sizeof(struct timeval)))
					ret = 0;
			}

			return ret;
		}

		case SIOCGIFDSTADDR:
		case SIOCSIFDSTADDR:
		case SIOCGIFBRDADDR:
		case SIOCSIFBRDADDR:
		case SIOCGIFNETMASK:
		case SIOCSIFNETMASK:
			return -EINVAL;

		default:
			return dev_ioctl(cmd,(void *) arg);
	}

	/*NOT REACHED*/
	return 0;
}

/*
 *      SPX interface support
 */

int ipx_register_spx(struct proto_ops **p, struct net_proto_family *spx)
{
        if (spx_family_ops)
                return -EBUSY;
        cli();
        MOD_INC_USE_COUNT;
        *p = &ipx_dgram_ops;
        spx_family_ops = spx;
        sti();
        return 0;
}

int ipx_unregister_spx(void)
{
        spx_family_ops = NULL;
        MOD_DEC_USE_COUNT;
        return 0;
}

/*
 * Socket family declarations
 */

static struct net_proto_family ipx_family_ops = {
	family:		PF_IPX,
	create:		ipx_create,
};

static struct proto_ops SOCKOPS_WRAPPED(ipx_dgram_ops) = {
	family:		PF_IPX,

	release:	ipx_release,
	bind:		ipx_bind,
	connect:	ipx_connect,
	socketpair:	sock_no_socketpair,
	accept:		sock_no_accept,
	getname:	ipx_getname,
	poll:		datagram_poll,
	ioctl:		ipx_ioctl,
	listen:		sock_no_listen,
	shutdown:	sock_no_shutdown, /* FIXME: have to support shutdown */
	setsockopt:	ipx_setsockopt,
	getsockopt:	ipx_getsockopt,
	sendmsg:	ipx_sendmsg,
	recvmsg:	ipx_recvmsg,
	mmap:		sock_no_mmap,
};

#include <linux/smp_lock.h>
SOCKOPS_WRAP(ipx_dgram, PF_IPX);

static struct packet_type ipx_8023_packet_type =

{
	type:		__constant_htons(ETH_P_802_3),
	func:		ipx_rcv,
	data:		(void *) 1,	/* yap, I understand shared skbs :-) */
};

static struct packet_type ipx_dix_packet_type =
{
	type:		__constant_htons(ETH_P_IPX),
	func:		ipx_rcv,
	data:		(void *) 1,	/* yap, I understand shared skbs :-) */
};

static struct notifier_block ipx_dev_notifier = {
	notifier_call:	ipxitf_device_event,
};


extern struct datalink_proto *make_EII_client(void);
extern struct datalink_proto *make_8023_client(void);
extern void destroy_EII_client(struct datalink_proto *);
extern void destroy_8023_client(struct datalink_proto *);

static unsigned char ipx_8022_type = 0xE0;
static unsigned char ipx_snap_id[5] = { 0x0, 0x0, 0x0, 0x81, 0x37 };

static int __init ipx_init(void)
{
	sock_register(&ipx_family_ops);

	pEII_datalink = make_EII_client();
	dev_add_pack(&ipx_dix_packet_type);

	p8023_datalink = make_8023_client();
	dev_add_pack(&ipx_8023_packet_type);

	p8022_datalink = register_8022_client(ipx_8022_type,ipx_rcv);
	if (!p8022_datalink)
		printk(KERN_CRIT "IPX: Unable to register with 802.2\n");

	pSNAP_datalink = register_snap_client(ipx_snap_id,ipx_rcv);
	if (!pSNAP_datalink)
		printk(KERN_CRIT "IPX: Unable to register with SNAP\n");

	register_netdevice_notifier(&ipx_dev_notifier);
#ifdef CONFIG_PROC_FS
	proc_net_create("ipx", 0, ipx_get_info);
	proc_net_create("ipx_interface", 0, ipx_interface_get_info);
	proc_net_create("ipx_route", 0, ipx_rt_get_info);
#endif
	printk(KERN_INFO "NET4: Linux IPX 0.44 for NET4.0\n");
	printk(KERN_INFO "IPX Portions Copyright (c) 1995 Caldera, Inc.\n");
	printk(KERN_INFO "IPX Portions Copyright (c) 2000 Conectiva, Inc.\n");
	return 0;
}

module_init(ipx_init);

/* Higher layers need this info to prep tx pkts */
int ipx_if_offset(unsigned long ipx_net_number)
{
	ipx_route *rt = ipxrtr_lookup(ipx_net_number);

	return rt ? rt->ir_intrfc->if_ipx_offset : -ENETUNREACH;
}

/* Export symbols for higher layers */
EXPORT_SYMBOL(ipxrtr_route_skb);
EXPORT_SYMBOL(ipx_if_offset);
EXPORT_SYMBOL(ipx_remove_socket);
EXPORT_SYMBOL(ipx_register_spx);
EXPORT_SYMBOL(ipx_unregister_spx);

/* Note on MOD_{INC,DEC}_USE_COUNT:
 *
 * Use counts are incremented/decremented when
 * sockets are created/deleted.
 *
 * Routes are always associated with an interface, and
 * allocs/frees will remain properly accounted for by
 * their associated interfaces.
 *
 * Ergo, before the ipx module can be removed, all IPX
 * sockets be closed from user space.
 */

#ifdef MODULE
static void ipx_proto_finito(void)
{
	/* no need to worry about having anything on the ipx_interfaces
	 * list, when a interface is created we increment the module
	 * usage count, so the module will only be unloaded when there
	 * are no more interfaces */

	proc_net_remove("ipx_route");
	proc_net_remove("ipx_interface");
	proc_net_remove("ipx");

	unregister_netdevice_notifier(&ipx_dev_notifier);

	unregister_snap_client(ipx_snap_id);
	pSNAP_datalink = NULL;

	unregister_8022_client(ipx_8022_type);
	p8022_datalink = NULL;

	dev_remove_pack(&ipx_8023_packet_type);
	destroy_8023_client(p8023_datalink);
	p8023_datalink = NULL;

	dev_remove_pack(&ipx_dix_packet_type);
	destroy_EII_client(pEII_datalink);
	pEII_datalink = NULL;

	sock_unregister(ipx_family_ops.family);
}

module_exit(ipx_proto_finito);
#endif /* MODULE */
#endif /* CONFIG_IPX || CONFIG_IPX_MODULE */
