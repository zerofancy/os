#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/pci.h>

#include <asm/init.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>

#include "pmac_pic.h"
#include "open_pic.h"

/* pmac */struct pmac_irq_hw {
        unsigned int    flag;
        unsigned int    enable;
        unsigned int    ack;
        unsigned int    level;
};

/* XXX these addresses should be obtained from the device tree */
static volatile struct pmac_irq_hw *pmac_irq_hw[4] = {
        (struct pmac_irq_hw *) 0xf3000020,
        (struct pmac_irq_hw *) 0xf3000010,
        (struct pmac_irq_hw *) 0xf4000020,
        (struct pmac_irq_hw *) 0xf4000010,
};

static int max_irqs;
static int max_real_irqs;
static int pmac_has_openpic;

spinlock_t pmac_pic_lock = SPIN_LOCK_UNLOCKED;


#define GATWICK_IRQ_POOL_SIZE        10
static struct interrupt_info gatwick_int_pool[GATWICK_IRQ_POOL_SIZE];

/*
 * Mark an irq as "lost".  This is only used on the pmac
 * since it can lose interrupts (see pmac_set_irq_mask).
 * -- Cort
 */
void __pmac __no_use_set_lost(unsigned long irq_nr)
{
	if (!test_and_set_bit(irq_nr, ppc_lost_interrupts))
		atomic_inc(&ppc_n_lost_interrupts);
}

static void __pmac pmac_mask_and_ack_irq(unsigned int irq_nr)
{
        unsigned long bit = 1UL << (irq_nr & 0x1f);
        int i = irq_nr >> 5;
        unsigned long flags;

        if ((unsigned)irq_nr >= max_irqs)
                return;

        clear_bit(irq_nr, ppc_cached_irq_mask);
        if (test_and_clear_bit(irq_nr, ppc_lost_interrupts))
                atomic_dec(&ppc_n_lost_interrupts);
	spin_lock_irqsave(&pmac_pic_lock, flags);
        out_le32(&pmac_irq_hw[i]->ack, bit);
        out_le32(&pmac_irq_hw[i]->enable, ppc_cached_irq_mask[i]);
        out_le32(&pmac_irq_hw[i]->ack, bit);
        do {
                /* make sure ack gets to controller before we enable
                   interrupts */
                mb();
        } while(in_le32(&pmac_irq_hw[i]->flag) & bit);
	spin_unlock_irqrestore(&pmac_pic_lock, flags);
}

static void __pmac pmac_set_irq_mask(unsigned int irq_nr)
{
        unsigned long bit = 1UL << (irq_nr & 0x1f);
        int i = irq_nr >> 5;
        unsigned long flags;

        if ((unsigned)irq_nr >= max_irqs)
                return;

	spin_lock_irqsave(&pmac_pic_lock, flags);
        /* enable unmasked interrupts */
        out_le32(&pmac_irq_hw[i]->enable, ppc_cached_irq_mask[i]);

        do {
                /* make sure mask gets to controller before we
                   return to user */
                mb();
        } while((in_le32(&pmac_irq_hw[i]->enable) & bit)
                != (ppc_cached_irq_mask[i] & bit));

        /*
         * Unfortunately, setting the bit in the enable register
         * when the device interrupt is already on *doesn't* set
         * the bit in the flag register or request another interrupt.
         */
        if ((bit & ppc_cached_irq_mask[i])
            && (ld_le32(&pmac_irq_hw[i]->level) & bit)
            && !(ld_le32(&pmac_irq_hw[i]->flag) & bit))
		__set_lost((ulong)irq_nr);
	spin_unlock_irqrestore(&pmac_pic_lock, flags);
}

static void __pmac pmac_mask_irq(unsigned int irq_nr)
{
        clear_bit(irq_nr, ppc_cached_irq_mask);
        pmac_set_irq_mask(irq_nr);
        mb();
}

static void __pmac pmac_unmask_irq(unsigned int irq_nr)
{
        set_bit(irq_nr, ppc_cached_irq_mask);
        pmac_set_irq_mask(irq_nr);
}

static void __pmac pmac_end_irq(unsigned int irq_nr)
{
	if (!(irq_desc[irq_nr].status & (IRQ_DISABLED|IRQ_INPROGRESS))) {
        	set_bit(irq_nr, ppc_cached_irq_mask);
	        pmac_set_irq_mask(irq_nr);
	}
}


struct hw_interrupt_type pmac_pic = {
        " PMAC-PIC ",
        NULL,
        NULL,
        pmac_unmask_irq,
        pmac_mask_irq,
        pmac_mask_and_ack_irq,
        pmac_end_irq,
        NULL
};

struct hw_interrupt_type gatwick_pic = {
	" GATWICK  ",
	NULL,
	NULL,
	pmac_unmask_irq,
	pmac_mask_irq,
	pmac_mask_and_ack_irq,
	pmac_end_irq,
	NULL
};

static void gatwick_action(int cpl, void *dev_id, struct pt_regs *regs)
{
	int irq, bits;
	
	for (irq = max_irqs; (irq -= 32) >= max_real_irqs; ) {
		int i = irq >> 5;
		bits = ld_le32(&pmac_irq_hw[i]->flag)
			| ppc_lost_interrupts[i];
		if (bits == 0)
			continue;
		irq += __ilog2(bits);
		break;
	}
	/* The previous version of this code allowed for this case, we
	 * don't.  Put this here to check for it.
	 * -- Cort
	 */
	if ( irq_desc[irq].handler != &gatwick_pic )
		printk("gatwick irq not from gatwick pic\n");
	else
		ppc_irq_dispatch_handler( regs, irq );
}

int
pmac_get_irq(struct pt_regs *regs)
{
	int irq;
	unsigned long bits = 0;

#ifdef CONFIG_SMP
	void psurge_smp_message_recv(struct pt_regs *);
	
       	/* IPI's are a hack on the powersurge -- Cort */
       	if ( smp_processor_id() != 0 ) {
		psurge_smp_message_recv(regs);
		return -2;	/* ignore, already handled */
        }
#endif /* CONFIG_SMP */
	for (irq = max_real_irqs; (irq -= 32) >= 0; ) {
		int i = irq >> 5;
		bits = ld_le32(&pmac_irq_hw[i]->flag)
			| ppc_lost_interrupts[i];
		if (bits == 0)
			continue;
		irq += __ilog2(bits);
		break;
	}

	return irq;
}

/* This routine will fix some missing interrupt values in the device tree
 * on the gatwick mac-io controller used by some PowerBooks
 */
static void __init
pmac_fix_gatwick_interrupts(struct device_node *gw, int irq_base)
{
	struct device_node *node;
	int count;
	
	memset(gatwick_int_pool, 0, sizeof(gatwick_int_pool));
	node = gw->child;
	count = 0;
	while(node)
	{
		/* Fix SCC */
		if (strcasecmp(node->name, "escc") == 0)
			if (node->child) {
				if (node->child->n_intrs < 3) {
					node->child->intrs = &gatwick_int_pool[count];
					count += 3;
				}
				node->child->n_intrs = 3;				
				node->child->intrs[0].line = 15+irq_base;
				node->child->intrs[1].line =  4+irq_base;
				node->child->intrs[2].line =  5+irq_base;
				printk(KERN_INFO "irq: fixed SCC on second controller (%d,%d,%d)\n",
					node->child->intrs[0].line,
					node->child->intrs[1].line,
					node->child->intrs[2].line);
			}
		/* Fix media-bay & left SWIM */
		if (strcasecmp(node->name, "media-bay") == 0) {
			struct device_node* ya_node;

			if (node->n_intrs == 0)
				node->intrs = &gatwick_int_pool[count++];
			node->n_intrs = 1;
			node->intrs[0].line = 29+irq_base;
			printk(KERN_INFO "irq: fixed media-bay on second controller (%d)\n",
					node->intrs[0].line);
			
			ya_node = node->child;
			while(ya_node)
			{
				if (strcasecmp(ya_node->name, "floppy") == 0) {
					if (ya_node->n_intrs < 2) {
						ya_node->intrs = &gatwick_int_pool[count];
						count += 2;
					}
					ya_node->n_intrs = 2;
					ya_node->intrs[0].line = 19+irq_base;
					ya_node->intrs[1].line =  1+irq_base;
					printk(KERN_INFO "irq: fixed floppy on second controller (%d,%d)\n",
						ya_node->intrs[0].line, ya_node->intrs[1].line);
				} 
				if (strcasecmp(ya_node->name, "ata4") == 0) {
					if (ya_node->n_intrs < 2) {
						ya_node->intrs = &gatwick_int_pool[count];
						count += 2;
					}
					ya_node->n_intrs = 2;
					ya_node->intrs[0].line = 14+irq_base;
					ya_node->intrs[1].line =  3+irq_base;
					printk(KERN_INFO "irq: fixed ide on second controller (%d,%d)\n",
						ya_node->intrs[0].line, ya_node->intrs[1].line);
				} 
				ya_node = ya_node->sibling;
			}
		}
		node = node->sibling;
	}
	if (count > 10) {
		printk("WARNING !! Gatwick interrupt pool overflow\n");
		printk("  GATWICK_IRQ_POOL_SIZE = %d\n", GATWICK_IRQ_POOL_SIZE);
		printk("              requested = %d\n", count);
	}
}

/*
 * The PowerBook 3400/2400/3500 can have a combo ethernet/modem
 * card which includes an ohare chip that acts as a second interrupt
 * controller.  If we find this second ohare, set it up and fix the
 * interrupt value in the device tree for the ethernet chip.
 */
static void __init enable_second_ohare(void)
{
	unsigned char bus, devfn;
	unsigned short cmd;
        unsigned long addr;
	int second_irq;
	struct device_node *irqctrler = find_devices("pci106b,7");
	struct device_node *ether;

	if (irqctrler == NULL || irqctrler->n_addrs <= 0)
		return;
	addr = (unsigned long) ioremap(irqctrler->addrs[0].address, 0x40);
	pmac_irq_hw[1] = (volatile struct pmac_irq_hw *)(addr + 0x20);
	max_irqs = 64;
	if (pci_device_from_OF_node(irqctrler, &bus, &devfn) == 0) {
		struct pci_controller* hose = pci_find_hose_for_OF_device(irqctrler);
		if (!hose)
		    printk(KERN_ERR "Can't find PCI hose for OHare2 !\n");
		else {
		    early_read_config_word(hose, bus, devfn, PCI_COMMAND, &cmd);
		    cmd |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
	  	    cmd &= ~PCI_COMMAND_IO;
		    early_write_config_word(hose, bus, devfn, PCI_COMMAND, cmd);
		}
	}

	second_irq = irqctrler->intrs[0].line;
	printk(KERN_INFO "irq: secondary controller on irq %d\n", second_irq);
	request_irq(second_irq, gatwick_action, SA_INTERRUPT,
		    "interrupt cascade", 0 );

	/* Fix interrupt for the modem/ethernet combo controller. The number
	   in the device tree (27) is bogus (correct for the ethernet-only
	   board but not the combo ethernet/modem board).
	   The real interrupt is 28 on the second controller -> 28+32 = 60.
	*/
	ether = find_devices("pci1011,14");
	if (ether && ether->n_intrs > 0) {
		ether->intrs[0].line = 60;
		printk(KERN_INFO "irq: Fixed ethernet IRQ to %d\n",
		       ether->intrs[0].line);
	}
}

void __init
pmac_pic_init(void)
{
        int i;
        struct device_node *irqctrler;
        unsigned long addr;
	int second_irq = -999;

	/* We first try to detect Apple's new Core99 chipset, since mac-io
	 * is quite different on those machines and contains an IBM MPIC2.
	 */
	irqctrler = find_type_devices("open-pic");
	if (irqctrler != NULL)
	{
		printk("PowerMac using OpenPIC irq controller\n");
		if (irqctrler->n_addrs > 0)
		{
			int nmi_irq = -1;
			unsigned char senses[NR_IRQS];
#ifdef CONFIG_XMON
			struct device_node* pswitch;

			pswitch = find_devices("programmer-switch");
			if (pswitch && pswitch->n_intrs)
				nmi_irq = pswitch->intrs[0].line;
#endif /* CONFIG_XMON */
			prom_get_irq_senses(senses, 0, NR_IRQS);
			OpenPIC_InitSenses = senses;
			OpenPIC_NumInitSenses = NR_IRQS;
			ppc_md.get_irq = openpic_get_irq;
			OpenPIC_Addr = ioremap(irqctrler->addrs[0].address,
					       irqctrler->addrs[0].size);
			openpic_init(1, 0, 0, nmi_irq);
			pmac_has_openpic = 1;
#ifdef CONFIG_XMON
			if (nmi_irq >= 0)
				request_irq(nmi_irq, xmon_irq, 0,
					    "NMI - XMON", 0);
#endif	/* CONFIG_XMON */
			return;
		}
		irqctrler = NULL;
	}

	int_control.int_set_lost = __no_use_set_lost;
	/*
	 * G3 powermacs and 1999 G3 PowerBooks have 64 interrupts,
	 * 1998 G3 Series PowerBooks have 128, 
	 * other powermacs have 32.
	 * The combo ethernet/modem card for the Powerstar powerbooks
	 * (2400/3400/3500, ohare based) has a second ohare chip
	 * effectively making a total of 64.
	 */
	max_irqs = max_real_irqs = 32;
	irqctrler = find_devices("mac-io");
	if (irqctrler)
	{
		max_real_irqs = 64;
		if (irqctrler->next)
			max_irqs = 128;
		else
			max_irqs = 64;
	}
	for ( i = 0; i < max_real_irqs ; i++ )
		irq_desc[i].handler = &pmac_pic;

	/* get addresses of first controller */
	if (irqctrler) {
		if  (irqctrler->n_addrs > 0) {
			addr = (unsigned long) 
				ioremap(irqctrler->addrs[0].address, 0x40);
			for (i = 0; i < 2; ++i)
				pmac_irq_hw[i] = (volatile struct pmac_irq_hw*)
					(addr + (2 - i) * 0x10);
		}
		
		/* get addresses of second controller */
		irqctrler = irqctrler->next;
		if (irqctrler && irqctrler->n_addrs > 0) {
			addr = (unsigned long) 
				ioremap(irqctrler->addrs[0].address, 0x40);
			for (i = 2; i < 4; ++i)
				pmac_irq_hw[i] = (volatile struct pmac_irq_hw*)
					(addr + (4 - i) * 0x10);
		}
	} else {
		/* older powermacs have a GC (grand central) or ohare at
		   f3000000, with interrupt control registers at f3000020. */
		addr = (unsigned long) ioremap(0xf3000000, 0x40);
		pmac_irq_hw[0] = (volatile struct pmac_irq_hw *) (addr + 0x20);
	}

	/* PowerBooks 3400 and 3500 can have a second controller in a second
	   ohare chip, on the combo ethernet/modem card */
	if (machine_is_compatible("AAPL,3400/2400")
	     || machine_is_compatible("AAPL,3500"))
		enable_second_ohare();

	/* disable all interrupts in all controllers */
	for (i = 0; i * 32 < max_irqs; ++i)
		out_le32(&pmac_irq_hw[i]->enable, 0);
	
	/* get interrupt line of secondary interrupt controller */
	if (irqctrler) {
		second_irq = irqctrler->intrs[0].line;
		printk(KERN_INFO "irq: secondary controller on irq %d\n",
			(int)second_irq);
		if (device_is_compatible(irqctrler, "gatwick"))
			pmac_fix_gatwick_interrupts(irqctrler, max_real_irqs);
		for ( i = max_real_irqs ; i < max_irqs ; i++ )
			irq_desc[i].handler = &gatwick_pic;
		request_irq( second_irq, gatwick_action, SA_INTERRUPT,
			     "gatwick cascade", 0 );
	}
	printk("System has %d possible interrupts\n", max_irqs);
	if (max_irqs != max_real_irqs)
		printk(KERN_DEBUG "%d interrupts on main controller\n",
			max_real_irqs);

#ifdef CONFIG_XMON
	request_irq(20, xmon_irq, 0, "NMI - XMON", 0);
#endif	/* CONFIG_XMON */
}

#ifdef CONFIG_PMAC_PBOOK
/*
 * These procedures are used in implementing sleep on the powerbooks.
 * sleep_save_intrs() saves the states of all interrupt enables
 * and disables all interrupts except for the nominated one.
 * sleep_restore_intrs() restores the states of all interrupt enables.
 */
unsigned int sleep_save_mask[2];

void
sleep_save_intrs(int viaint)
{
	sleep_save_mask[0] = ppc_cached_irq_mask[0];
	sleep_save_mask[1] = ppc_cached_irq_mask[1];
	ppc_cached_irq_mask[0] = 0;
	ppc_cached_irq_mask[1] = 0;
	set_bit(viaint, ppc_cached_irq_mask);
	out_le32(&pmac_irq_hw[0]->enable, ppc_cached_irq_mask[0]);
	if (max_real_irqs > 32)
		out_le32(&pmac_irq_hw[1]->enable, ppc_cached_irq_mask[1]);
	(void)in_le32(&pmac_irq_hw[0]->flag);
	/* make sure mask gets to controller before we return to caller */
	mb();
        (void)in_le32(&pmac_irq_hw[0]->enable);
}

void
sleep_restore_intrs(void)
{
	int i;

	out_le32(&pmac_irq_hw[0]->enable, 0);
	if (max_real_irqs > 32)
		out_le32(&pmac_irq_hw[1]->enable, 0);
	mb();
	for (i = 0; i < max_real_irqs; ++i)
		if (test_bit(i, sleep_save_mask))
			pmac_unmask_irq(i);
}
#endif /* CONFIG_PMAC_PBOOK */
