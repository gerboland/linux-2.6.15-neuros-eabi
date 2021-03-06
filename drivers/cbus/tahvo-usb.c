/**
 * drivers/cbus/tahvo-usb.c
 *
 * Tahvo USB transeiver
 *
 * Copyright (C) 2005 Nokia Corporation
 *
 * Written by Juha Yrj�l� <juha.yrjola@nokia.com>,
 *	      Tony Lindgren <tony@atomide.com>, and
 *	      Timo Ter�s <timo.teras@nokia.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/usb_ch9.h>
#include <linux/usb_gadget.h>
#include <linux/usb.h>
#include <linux/usb_otg.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <linux/kobject_uevent.h>

#include <asm/irq.h>
#include <asm/semaphore.h>
#include <asm/arch/usb.h>
#include <asm/hardware/clock.h>

#include "cbus.h"
#include "tahvo.h"

#define DRIVER_NAME     "tahvo-usb"

#define USBR_SLAVE_CONTROL	(1 << 8)
#define USBR_VPPVIO_SW		(1 << 7)
#define USBR_SPEED		(1 << 6)
#define USBR_REGOUT		(1 << 5)
#define USBR_MASTER_SW2		(1 << 4)
#define USBR_MASTER_SW1		(1 << 3)
#define USBR_SLAVE_SW		(1 << 2)
#define USBR_NSUSPEND		(1 << 1)
#define USBR_SEMODE		(1 << 0)

/* bits in OTG_CTRL_REG */

/* Bits that are controlled by OMAP OTG and are read-only */
#define OTG_CTRL_OMAP_MASK	(OTG_PULLDOWN|OTG_PULLUP|OTG_DRV_VBUS|\
				OTG_PD_VBUS|OTG_PU_VBUS|OTG_PU_ID)
/* Bits that are controlled by transceiver */
#define OTG_CTRL_XCVR_MASK	(OTG_ASESSVLD|OTG_BSESSEND|\
				OTG_BSESSVLD|OTG_VBUSVLD|OTG_ID)
/* Bits that are controlled by system */
#define OTG_CTRL_SYS_MASK	(OTG_A_BUSREQ|OTG_A_SETB_HNPEN|OTG_B_BUSREQ|\
				OTG_B_HNPEN|OTG_BUSDROP)

#if defined(CONFIG_USB_OHCI_HCD) && !defined(CONFIG_USB_OTG)
#error tahvo-otg.c does not work with OCHI yet!
#endif

#define TAHVO_MODE_HOST		0
#define TAHVO_MODE_PERIPHERAL	1

#ifdef CONFIG_USB_OTG
#define TAHVO_MODE(tu)		(tu)->tahvo_mode
#elif defined(CONFIG_USB_GADGET_OMAP)
#define TAHVO_MODE(tu)		TAHVO_MODE_PERIPHERAL
#else
#define TAHVO_MODE(tu)		TAHVO_MODE_HOST
#endif

extern int ohci_omap_host_enable(struct usb_bus *host, int enable);

struct tahvo_usb {
	struct platform_device *pt_dev;
	struct otg_transceiver otg;
	int vbus_state;
	struct work_struct irq_work;
	struct semaphore serialize;
#ifdef CONFIG_USB_OTG
	int tahvo_mode;
#endif
};
static struct platform_device tahvo_usb_device;

/*
 * ---------------------------------------------------------------------------
 * OTG related functions
 *
 * These shoud be separated into omap-otg.c driver module, as they are used
 * by various transceivers. These functions are needed in the UDC-only case 
 * as well. These functions are copied from GPL isp1301_omap.c
 * ---------------------------------------------------------------------------
 */
static struct platform_device *tahvo_otg_dev;

static irqreturn_t omap_otg_irq(int irq, void *arg, struct pt_regs *regs)
{
	struct platform_device *otg_dev = (struct platform_device *) arg;
	struct tahvo_usb *tu = (struct tahvo_usb *) otg_dev->dev.driver_data;
	u16 otg_irq;

	otg_irq = OTG_IRQ_SRC_REG;
	if (otg_irq & OPRT_CHG) {
		OTG_IRQ_SRC_REG = OPRT_CHG;
	} else if (otg_irq & B_SRP_TMROUT) {
		OTG_IRQ_SRC_REG = B_SRP_TMROUT;
	} else if (otg_irq & B_HNP_FAIL) {
		OTG_IRQ_SRC_REG = B_HNP_FAIL;
	} else if (otg_irq & A_SRP_DETECT) {
		OTG_IRQ_SRC_REG = A_SRP_DETECT;
	} else if (otg_irq & A_REQ_TMROUT) {
		OTG_IRQ_SRC_REG = A_REQ_TMROUT;
	} else if (otg_irq & A_VBUS_ERR) {
		OTG_IRQ_SRC_REG = A_VBUS_ERR;
	} else if (otg_irq & DRIVER_SWITCH) {

		if ((!(OTG_CTRL_REG & OTG_DRIVER_SEL)) &&
		   tu->otg.host && tu->otg.state == OTG_STATE_A_HOST) {
			/* role is host */
			usb_bus_start_enum(tu->otg.host,
					   tu->otg.host->otg_port);
		}

		OTG_IRQ_SRC_REG = DRIVER_SWITCH;
	} else
		return IRQ_NONE;

	return IRQ_HANDLED;

}

static int omap_otg_init(void)
{

#ifdef CONFIG_USB_OTG
	if (!tahvo_otg_dev) {
		printk("tahvo-usb: no tahvo_otg_dev\n");
		return -ENODEV;
	}
#endif

	/* some of these values are board-specific... */
	OTG_SYSCON_2_REG |= OTG_EN
		/* for B-device: */
		| SRP_GPDATA		/* 9msec Bdev D+ pulse */
		| SRP_GPDVBUS		/* discharge after VBUS pulse */
		// | (3 << 24)		/* 2msec VBUS pulse */
		/* for A-device: */
		| (0 << 20)		/* 200ms nominal A_WAIT_VRISE timer */
		| SRP_DPW		/* detect 167+ns SRP pulses */
		| SRP_DATA | SRP_VBUS;	/* accept both kinds of SRP pulse */

	OTG_IRQ_EN_REG = DRIVER_SWITCH | OPRT_CHG
			| B_SRP_TMROUT | B_HNP_FAIL
			| A_VBUS_ERR | A_SRP_DETECT | A_REQ_TMROUT;
	OTG_SYSCON_2_REG |= OTG_EN;

	return 0;
}

static int omap_otg_probe(struct device *dev)
{
	int ret;

	tahvo_otg_dev = to_platform_device(dev);
	ret = omap_otg_init();
	if (ret != 0) {
		printk(KERN_ERR "tahvo-usb: omap_otg_init failed\n");
		return ret;
	}

	return request_irq(tahvo_otg_dev->resource[1].start,
			   omap_otg_irq, SA_INTERRUPT, DRIVER_NAME,
			   &tahvo_usb_device);
}

static int omap_otg_remove(struct device *dev)
{
	tahvo_otg_dev = NULL;
	free_irq(tahvo_otg_dev->resource[1].start, &tahvo_usb_device);
	return 0;
}

struct device_driver omap_otg_driver = {
	.name		= "omap_otg",
	.bus		= &platform_bus_type,
	.probe		= omap_otg_probe,
	.remove		= omap_otg_remove,
};

/*
 * ---------------------------------------------------------------------------
 * Tahvo related functions
 * These are Nokia proprietary code, except for the OTG register settings,
 * which are copied from isp1301.c
 * ---------------------------------------------------------------------------
 */
static ssize_t vbus_state_show(struct device *device, char *buf)
{
	struct tahvo_usb *tu = (struct tahvo_usb*) device->driver_data;
	return sprintf(buf, "%d\n", tu->vbus_state);
}
static DEVICE_ATTR(vbus_state, 0444, vbus_state_show, NULL);

int vbus_active = 0;

static void check_vbus_state(struct tahvo_usb *tu)
{
	int reg, prev_state;

	reg = tahvo_read_reg(TAHVO_REG_IDSR);
	if (reg & 0x01) {
		vbus_active = 1;
		switch (tu->otg.state) {
		case OTG_STATE_B_IDLE:
			/* Enable the gadget driver */
			if (tu->otg.gadget)
				usb_gadget_vbus_connect(tu->otg.gadget);
			/* Set B-session valid and not B-sessio ended to indicate
			 * Vbus to be ok. */
			OTG_CTRL_REG = (OTG_CTRL_REG & ~OTG_BSESSEND) | OTG_BSESSVLD;

			tu->otg.state = OTG_STATE_B_PERIPHERAL;
			break;
		case OTG_STATE_A_IDLE:
			/* Session is now valid assuming the USB hub is driving Vbus */
			tu->otg.state = OTG_STATE_A_HOST;
			if (tu->otg.host)
				ohci_omap_host_enable(tu->otg.host, 1);
			break;
		default:
			break;
		}
		printk("USB cable connected\n");
	} else {
		vbus_active = 0;
		switch (tu->otg.state) {
		case OTG_STATE_B_PERIPHERAL:
			OTG_CTRL_REG = (OTG_CTRL_REG & ~OTG_BSESSVLD) | OTG_BSESSEND;
			if (tu->otg.gadget)
				usb_gadget_vbus_disconnect(tu->otg.gadget);
			tu->otg.state = OTG_STATE_B_IDLE;
			break;
		case OTG_STATE_A_HOST:
			tu->otg.state = OTG_STATE_A_IDLE;
			if (tu->otg.host)
				ohci_omap_host_enable(tu->otg.host, 0);

			break;
		default:
			break;
		}
		printk("USB cable disconnected\n");
	}

	prev_state = tu->vbus_state;
	tu->vbus_state = reg & 0x01;
	if (prev_state != tu->vbus_state) {
		kobject_uevent_atomic(&tu->pt_dev->dev.kobj,
				      KOBJ_CHANGE,
				      &dev_attr_vbus_state.attr);
	}
}

static void tahvo_usb_become_host(struct tahvo_usb *tu)
{
	/* Clear system and transceiver controlled bits
	 * also mark the A-session is always valid */
	omap_otg_init();
	OTG_CTRL_REG = (OTG_CTRL_REG & ~(OTG_CTRL_XCVR_MASK|OTG_CTRL_SYS_MASK))
		| OTG_ASESSVLD;

	/* Power up the transceiver in USB host mode */
	tahvo_write_reg(TAHVO_REG_USBR, USBR_REGOUT | USBR_NSUSPEND |
			USBR_MASTER_SW2 | USBR_MASTER_SW1);
	tu->otg.state = OTG_STATE_A_IDLE;

	check_vbus_state(tu);
}

static void tahvo_usb_stop_host(struct tahvo_usb *tu)
{
	if (tu->otg.host)
		ohci_omap_host_enable(tu->otg.host, 0);
	tu->otg.state = OTG_STATE_A_IDLE;
}

static void tahvo_usb_become_peripheral(struct tahvo_usb *tu)
{
	/* Clear system and transceiver controlled bits
	 * and enable ID to mark peripheral mode and
	 * BSESSEND to mark no Vbus */
	omap_otg_init();
	OTG_CTRL_REG = (OTG_CTRL_REG & ~(OTG_CTRL_XCVR_MASK|OTG_CTRL_SYS_MASK))
		| OTG_ID | OTG_BSESSEND;

	/* Power up transceiver and set it in USB perhiperal mode */
	tahvo_write_reg(TAHVO_REG_USBR, USBR_SLAVE_CONTROL | USBR_REGOUT | USBR_NSUSPEND | USBR_SLAVE_SW);
	tu->otg.state = OTG_STATE_B_IDLE;

	check_vbus_state(tu);
}

static void tahvo_usb_stop_peripheral(struct tahvo_usb *tu)
{
	OTG_CTRL_REG = (OTG_CTRL_REG & ~OTG_BSESSVLD) | OTG_BSESSEND;
	if (tu->otg.gadget)
		usb_gadget_vbus_disconnect(tu->otg.gadget);
	tu->otg.state = OTG_STATE_B_IDLE;

}

static void tahvo_usb_power_off(struct tahvo_usb *tu)
{
	int id;

	/* Disable gadget controller if any */
	if (tu->otg.gadget)
		usb_gadget_vbus_disconnect(tu->otg.gadget);

	if (tu->otg.host)
		ohci_omap_host_enable(tu->otg.host, 0);

	/* Disable OTG and interrupts */
	if (TAHVO_MODE(tu) == TAHVO_MODE_PERIPHERAL)
		id = OTG_ID;
        else    id = 0;
	OTG_CTRL_REG = (OTG_CTRL_REG & ~(OTG_CTRL_XCVR_MASK|OTG_CTRL_SYS_MASK)) | id;
	OTG_IRQ_EN_REG = 0;
#if 0
	OTG_SYSCON_2_REG &= ~OTG_EN;
#endif

	/* Power off transceiver */
	tahvo_write_reg(TAHVO_REG_USBR, 0);
	tu->otg.state = OTG_STATE_UNDEFINED;
}


static int tahvo_usb_set_power(struct otg_transceiver *dev, unsigned mA)
{
	if (dev->state == OTG_STATE_B_PERIPHERAL) {
		/* REVISIT: Can Tahvo charge battery from VBUS? */
	}
	return 0;
}

static int tahvo_usb_set_suspend(struct otg_transceiver *dev, int suspend)
{
	u16 w;

	w = tahvo_read_reg(TAHVO_REG_USBR);
	if (suspend)
		w &= ~USBR_NSUSPEND;
	else
		w |= USBR_NSUSPEND;
	tahvo_write_reg(TAHVO_REG_USBR, w);

	return 0;
}

static int tahvo_usb_start_srp(struct otg_transceiver *dev)
{
	struct tahvo_usb *tu = container_of(dev, struct tahvo_usb, otg);
	u32 otg_ctrl;

	if (!dev || tu->otg.state != OTG_STATE_B_IDLE)
		return -ENODEV;

	otg_ctrl = OTG_CTRL_REG;
	if (!(otg_ctrl & OTG_BSESSEND))
		return -EINVAL;

	otg_ctrl |= OTG_B_BUSREQ;
	otg_ctrl &= ~OTG_A_BUSREQ & OTG_CTRL_SYS_MASK;
	OTG_CTRL_REG = otg_ctrl;
	tu->otg.state = OTG_STATE_B_SRP_INIT;

	pr_debug("otg: SRP, %s ... %06x\n", state_name(tu), OTG_CTRL_REG);

	return 0;
}

static int tahvo_usb_start_hnp(struct otg_transceiver *dev)
{
#ifdef CONFIG_USB_OTG
	/* REVISIT: Add this for OTG */
#endif
	return -EINVAL;
}

static int tahvo_usb_set_host(struct otg_transceiver *otg, struct usb_bus *host)
{
	struct tahvo_usb *tu = container_of(otg, struct tahvo_usb, otg);

	if (!otg)
		return -ENODEV;

#if defined(CONFIG_USB_OTG) || !defined(CONFIG_USB_GADGET_OMAP)

	down(&tu->serialize);

	if (!host) {
		if (TAHVO_MODE(tu) == TAHVO_MODE_HOST)
			tahvo_usb_power_off(tu);
		tu->otg.host = 0;
		up(&tu->serialize);
		return 0;
	}

	if (TAHVO_MODE(tu) == TAHVO_MODE_HOST) {
		tu->otg.host = 0;
		tahvo_usb_become_host(tu);
	} else
		ohci_omap_host_enable(host, 0);

	tu->otg.host = host;

	up(&tu->serialize);
#else
	/* No host mode configured, so do not allow host controlled to be set */
	return -EINVAL;
#endif

	return 0;
}

static int tahvo_usb_set_peripheral(struct otg_transceiver *otg, struct usb_gadget *gadget)
{
	struct tahvo_usb *tu = container_of(otg, struct tahvo_usb, otg);

	if (!otg)
		return -ENODEV;

#if defined(CONFIG_USB_OTG) || defined(CONFIG_USB_GADGET_OMAP)

	down(&tu->serialize);

	if (!gadget) {
		if (TAHVO_MODE(tu) == TAHVO_MODE_PERIPHERAL)
			tahvo_usb_power_off(tu);
		tu->otg.gadget = 0;
		up(&tu->serialize);
		return 0;
	}

	tu->otg.gadget = gadget;
	if (TAHVO_MODE(tu) == TAHVO_MODE_PERIPHERAL)
		tahvo_usb_become_peripheral(tu);

	up(&tu->serialize);
#else
	/* No gadget mode configured, so do not allow host controlled to be set */
	return -EINVAL;
#endif

	return 0;
}

static void tahvo_usb_irq_work(void *data)
{
	struct tahvo_usb *tu = (struct tahvo_usb *)data;

	down(&tu->serialize);
	check_vbus_state(tu);
	up(&tu->serialize);
}

static void tahvo_usb_vbus_interrupt(unsigned long arg)
{
	struct tahvo_usb *tu = (struct tahvo_usb *) arg;

	tahvo_ack_irq(TAHVO_INT_VBUSON);
	/* Seems we need this to acknowledge the interrupt */
	tahvo_read_reg(TAHVO_REG_IDSR);
	schedule_work(&tu->irq_work);
}

#ifdef CONFIG_USB_OTG
static ssize_t otg_mode_show(struct device *device, char *buf)
{
	struct tahvo_usb *tu = (struct tahvo_usb*) device->driver_data;
	switch (tu->tahvo_mode) {
	case TAHVO_MODE_HOST:
		return sprintf(buf, "host\n");
	case TAHVO_MODE_PERIPHERAL:
		return sprintf(buf, "peripheral\n");
	}
	return sprintf(buf, "unknown\n");
}

static ssize_t otg_mode_store(struct device *device, const char *buf, size_t count)
{
	struct tahvo_usb *tu = (struct tahvo_usb*) device->driver_data;
	int r;

	r = strlen(buf);
	down(&tu->serialize);
	if (strncmp(buf, "host", 4) == 0) {
		if (tu->tahvo_mode == TAHVO_MODE_PERIPHERAL)
			tahvo_usb_stop_peripheral(tu);
		tu->tahvo_mode = TAHVO_MODE_HOST;
		if (tu->otg.host) {
			printk(KERN_INFO "Selected HOST mode: host controller present.\n");
			tahvo_usb_become_host(tu);
		} else {
			printk(KERN_INFO "Selected HOST mode: no host controller, powering off.\n");
			tahvo_usb_power_off(tu);
		}
	} else if (strncmp(buf, "peripheral", 10) == 0) {
		if (tu->tahvo_mode == TAHVO_MODE_HOST)
			tahvo_usb_stop_host(tu);
		tu->tahvo_mode = TAHVO_MODE_PERIPHERAL;
		if (tu->otg.gadget) {
			printk(KERN_INFO "Selected PERIPHERAL mode: gadget driver present.\n");
			tahvo_usb_become_peripheral(tu);
		} else {
			printk(KERN_INFO "Selected PERIPHERAL mode: no gadget driver, powering off.\n");
			tahvo_usb_power_off(tu);
		}
	} else
		r = -EINVAL;

	up(&tu->serialize);
	return r;
}

static DEVICE_ATTR(otg_mode, 0644, otg_mode_show, otg_mode_store);
#endif

static int tahvo_usb_probe(struct device *dev)
{
	struct tahvo_usb *tu;
	int ret;

	/* Create driver data */
	tu = kmalloc(sizeof(*tu), GFP_KERNEL);
	if (!tu)
		return -ENOMEM;
	memset(tu, 0, sizeof(*tu));
	tu->pt_dev = container_of(dev, struct platform_device, dev);
#ifdef CONFIG_USB_OTG
	/* Default mode */
#ifdef CONFIG_CBUS_TAHVO_USB_HOST_BY_DEFAULT
	tu->tahvo_mode = TAHVO_MODE_HOST;
#else
	tu->tahvo_mode = TAHVO_MODE_PERIPHERAL;
#endif
#endif

	INIT_WORK(&tu->irq_work, tahvo_usb_irq_work, tu);
	init_MUTEX(&tu->serialize);

	/* Set initial state, so that we generate kevents only on
	 * state changes */
	tu->vbus_state = tahvo_read_reg(TAHVO_REG_IDSR) & 0x01;

	/* We cannot enable interrupt until omap_udc is initialized */
	ret = tahvo_request_irq(TAHVO_INT_VBUSON, tahvo_usb_vbus_interrupt,
				(unsigned long) tu, "vbus_interrupt");
	if (ret != 0) {
		kfree(tu);
		printk(KERN_ERR "Could not register Tahvo interrupt for VBUS\n");
		return ret;
	}

	/* Attributes */
	device_create_file(dev, &dev_attr_vbus_state);
#ifdef CONFIG_USB_OTG
	device_create_file(dev, &dev_attr_otg_mode);
#endif

	/* Create OTG interface */
	tahvo_usb_power_off(tu);
	tu->otg.state = OTG_STATE_UNDEFINED;
	tu->otg.label = DRIVER_NAME;
	tu->otg.set_host = tahvo_usb_set_host;
	tu->otg.set_peripheral = tahvo_usb_set_peripheral;
	tu->otg.set_power = tahvo_usb_set_power;
	tu->otg.set_suspend = tahvo_usb_set_suspend;
	tu->otg.start_srp = tahvo_usb_start_srp;
	tu->otg.start_hnp = tahvo_usb_start_hnp;

	ret = otg_set_transceiver(&tu->otg);
	if (ret < 0) {
		printk(KERN_ERR "Cannot register USB transceiver\n");
		kfree(tu);
		tahvo_free_irq(TAHVO_INT_VBUSON);
		return ret;
	}

	dev->driver_data = tu;

	/* Act upon current vbus state once at startup. A vbus state irq may or
	 * may not be generated in addition to this. */
	schedule_work(&tu->irq_work);
	return 0;
}

static int tahvo_usb_remove(struct device *dev)
{
	tahvo_free_irq(TAHVO_INT_VBUSON);
	flush_scheduled_work();
	otg_set_transceiver(0);
	device_remove_file(dev, &dev_attr_vbus_state);
#ifdef CONFIG_USB_OTG
	device_remove_file(dev, &dev_attr_otg_mode);
#endif
	return 0;
}

static struct device_driver tahvo_usb_driver = {
	.name		= "tahvo-usb",
	.bus		= &platform_bus_type,
	.probe		= tahvo_usb_probe,
	.remove		= tahvo_usb_remove,
};

static struct platform_device tahvo_usb_device = {
	.name		= "tahvo-usb",
	.id		= -1,
};

static int __init tahvo_usb_init(void)
{
	int ret = 0;

	printk(KERN_INFO "Tahvo USB transceiver driver initializing\n");
	ret = driver_register(&tahvo_usb_driver);
	if (ret)
		return ret;
	ret = platform_device_register(&tahvo_usb_device);
	if (ret < 0) {
		driver_unregister(&tahvo_usb_driver);
		return ret;
	}
	ret = driver_register(&omap_otg_driver);
	if (ret) {
		platform_device_unregister(&tahvo_usb_device);
		driver_unregister(&tahvo_usb_driver);
		return ret;
	}
	return 0;
}

subsys_initcall(tahvo_usb_init);

static void __exit tahvo_usb_exit(void)
{
	driver_unregister(&omap_otg_driver);
	platform_device_unregister(&tahvo_usb_device);
	driver_unregister(&tahvo_usb_driver);
}
module_exit(tahvo_usb_exit);

MODULE_DESCRIPTION("Tahvo USB OTG Transceiver Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Juha Yrj�l�, Tony Lindgren, and Timo Ter�s");
