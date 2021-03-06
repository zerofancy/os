/*
 *  cmbatt.c - Control Method Battery driver
 *
 *  Copyright (C) 2000 Andrew Grover
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
/*
 * Changes:
 * Brendan Burns <bburns@wso.williams.edu> 2000-11-15
 * - added proc battery interface
 * - parse returned data from _BST and _BIF
 * Andy Grover <andrew.grover@intel.com> 2000-12-8
 * - improved proc interface
 * Pavel Machek <pavel@suse.cz> 2001-1-31
 * - fixed oops on NULLs in return from _BIF
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include "acpi.h"
#include "driver.h"

#define _COMPONENT	OS_DEPENDENT
	MODULE_NAME	("cmbatt")

/* ACPI-specific defines */
#define ACPI_CMBATT_HID		"PNP0C0A"
#define ACPI_BATT_PRESENT	0x10
#define ACPI_BATT_UNKNOWN	0xFFFFFFFF

/* driver-specific defines */
#define MAX_CM_BATTERIES	0x8
#define MAX_BATT_STRLEN		0x20

#define Xstrncpy(a, b, c) if (b) strncpy(a,b,c); else strncpy(a, "unknown", c)

struct cmbatt_info
{
	u32  power_unit;
	u32  design_capacity;
	u32  last_full_capacity;
	u32  battery_technology;
	u32  design_voltage;
	u32  design_capacity_warning;
	u32  design_capacity_low;
	u32  battery_capacity_granularity_1;
	u32  battery_capacity_granularity_2;

	char model_number[MAX_BATT_STRLEN];
	char serial_number[MAX_BATT_STRLEN];
	char battery_type[MAX_BATT_STRLEN];
	char oem_info[MAX_BATT_STRLEN];
};

struct cmbatt_context
{
	u32			is_present;
	ACPI_HANDLE		handle;
	char			UID[9];
	char			*power_unit;
	struct cmbatt_info	info;
};

struct cmbatt_status
{
	u32			state;
	u32			present_rate;
	u32			remaining_capacity;
	u32			present_voltage;
};

static u32 batt_count = 0;

static struct cmbatt_context batt_list[MAX_CM_BATTERIES];

static ACPI_STATUS
acpi_get_battery_status(ACPI_HANDLE handle, struct cmbatt_status *result)
{
	ACPI_OBJECT       *obj;
	ACPI_OBJECT       *objs;
	ACPI_BUFFER       buf;

	buf.length = 0;
	buf.pointer = NULL;		

	/* determine buffer length needed */
	if (acpi_evaluate_object(handle, "_BST", NULL, &buf) != AE_BUFFER_OVERFLOW) {
		printk(KERN_ERR "Cmbatt: Could not get battery status struct length\n");
		return AE_NOT_FOUND;
	}

	buf.pointer = kmalloc(buf.length, GFP_KERNEL);
	if (!buf.pointer)
		return AE_NO_MEMORY;

	/* get the data */
	if (!ACPI_SUCCESS(acpi_evaluate_object(handle, "_BST", NULL, &buf))) {
		printk(KERN_ERR "Cmbatt: Could not get battery status\n");
		kfree (buf.pointer);
		return AE_NOT_FOUND;
	}

	obj = (ACPI_OBJECT *) buf.pointer;
	objs = obj->package.elements;

	result->state = objs[0].integer.value;
	result->present_rate = objs[1].integer.value;
	result->remaining_capacity = objs[2].integer.value;
	result->present_voltage = objs[3].integer.value;

	kfree(buf.pointer);

	return AE_OK;
}

static ACPI_STATUS
acpi_get_battery_info(ACPI_HANDLE handle, struct cmbatt_info *result)
{
	ACPI_OBJECT       *obj;
	ACPI_OBJECT       *objs;
	ACPI_BUFFER       buf;

	buf.length = 0;
	buf.pointer = NULL;

	/* determine the length of the data */
	if (acpi_evaluate_object(handle, "_BIF", NULL, &buf) != AE_BUFFER_OVERFLOW) {
		printk(KERN_ERR "Cmbatt: Could not get battery info struct length\n");
		return AE_NOT_FOUND;
	}

	buf.pointer = kmalloc(buf.length, GFP_KERNEL);
	if (!buf.pointer)
		return AE_NO_MEMORY;

	/* get the data */
	if (!ACPI_SUCCESS(acpi_evaluate_object(handle, "_BIF", NULL, &buf))) {
		printk(KERN_ERR "Cmbatt: Could not get battery info\n");
		kfree (buf.pointer);
		return AE_NOT_FOUND;
	}
	
	obj = (ACPI_OBJECT *) buf.pointer;
	objs = obj->package.elements;
	
	result->power_unit=objs[0].integer.value;
	result->design_capacity=objs[1].integer.value;
	result->last_full_capacity=objs[2].integer.value;
	result->battery_technology=objs[3].integer.value;
	result->design_voltage=objs[4].integer.value;
	result->design_capacity_warning=objs[5].integer.value;
	result->design_capacity_low=objs[6].integer.value;
	result->battery_capacity_granularity_1=objs[7].integer.value;
	result->battery_capacity_granularity_2=objs[8].integer.value;

 	/* BUG: trailing NULL issue */
	Xstrncpy(result->model_number, objs[9].string.pointer, MAX_BATT_STRLEN-1);
	Xstrncpy(result->serial_number, objs[10].string.pointer, MAX_BATT_STRLEN-1);
	Xstrncpy(result->battery_type, objs[11].string.pointer, MAX_BATT_STRLEN-1);
	Xstrncpy(result->oem_info, objs[12].string.pointer, MAX_BATT_STRLEN-1);
	
	kfree(buf.pointer);

	return AE_OK;
}

/*
 * We found a device with the correct HID
 */
static ACPI_STATUS
acpi_found_cmbatt(ACPI_HANDLE handle, u32 level, void *ctx, void **value)
{
	ACPI_DEVICE_INFO	info;

	if (batt_count >= MAX_CM_BATTERIES) {
		printk(KERN_ERR "Cmbatt: MAX_CM_BATTERIES exceeded\n");
		return AE_OK;
	}

	if (!ACPI_SUCCESS(acpi_get_object_info(handle, &info))) {
		printk(KERN_ERR "Cmbatt: Could not get battery object info\n");
		return (AE_OK);
	}

	if (info.valid & ACPI_VALID_UID) {
		strncpy(batt_list[batt_count].UID, info.unique_id, 9);
	}
	else if (batt_count > 1) {
		printk(KERN_WARNING "Cmbatt: No UID but more than 1 battery\n");
	}
	
	if (!(info.valid & ACPI_VALID_STA)) {
		printk(KERN_ERR "Cmbatt: Battery _STA invalid\n");
		return AE_OK;
	}

	if (!(info.current_status & ACPI_BATT_PRESENT)) {
		printk(KERN_INFO "Cmbatt: Battery socket %d empty\n", batt_count);
		batt_list[batt_count].is_present = FALSE;
	}
	else {
		printk(KERN_INFO "Cmbatt: Battery socket %d occupied\n", batt_count);
		batt_list[batt_count].is_present = TRUE;
		if (acpi_get_battery_info(handle, &batt_list[batt_count].info) != AE_OK) {
			printk(KERN_ERR "acpi_get_battery_info failed\n");
			return AE_OK;
		}

		batt_list[batt_count].power_unit = (batt_list[batt_count].info.power_unit) ? "mA" : "mW";
	}
	
	batt_list[batt_count].handle = handle;

	batt_count++;

	return AE_OK;
}

static int
proc_read_batt_info(char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	struct cmbatt_info *info;
	u32 batt_num = (u32) data;
	char *p = page;
	int len;

	info = &batt_list[batt_num].info;

	/* don't get info more than once for a single proc read */
	if (off != 0)
		goto end;

	if (!batt_list[batt_num].is_present) {
		p += sprintf(p, "battery %d not present\n", batt_num);
		goto end;
	}

	if (info->last_full_capacity != ACPI_BATT_UNKNOWN)
		p += sprintf(p, "Last Full Capacity: %d %sh\n", 
		     info->last_full_capacity, batt_list[batt_num].power_unit);

	if (info->design_capacity != ACPI_BATT_UNKNOWN)
		p += sprintf(p, "Design Capacity:    %d %sh\n", 
		     info->design_capacity, batt_list[batt_num].power_unit);
	
	if (info->battery_technology)
		p += sprintf(p, "Battery Technology: Secondary\n");
	else
		p += sprintf(p, "Battery Technology: Primary\n");
	
	if (info->design_voltage != ACPI_BATT_UNKNOWN)
		p += sprintf(p, "Design Voltage:     %d mV\n", 
		     info->design_voltage);
	
	p += sprintf(p, "Design Capacity Warning:    %d %sh\n",
		info->design_capacity_warning, batt_list[batt_num].power_unit);
	p += sprintf(p, "Design Capacity Low:        %d %sh\n",
		info->design_capacity_low, batt_list[batt_num].power_unit);
	p += sprintf(p, "Battery Capacity Granularity 1: %d\n",
		info->battery_capacity_granularity_1);
	p += sprintf(p, "Battery Capacity Granularity 2: %d\n",
		info->battery_capacity_granularity_2);
	p += sprintf(p, "Model number; %s\nSerial number: %s\nBattery type: %s\nOEM info: %s\n",
		info->model_number,info->serial_number,
		info->battery_type,info->oem_info);
end:
	len = (p - page);
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;
	return len;
}

static int
proc_read_batt_status(char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	struct cmbatt_status status;
	u32 batt_num = (u32) data;
	char *p = page;
	int len;

	/* don't get status more than once for a single proc read */
	if (off != 0)
		goto end;

	if (!batt_list[batt_num].is_present) {
		p += sprintf(p, "battery %d not present\n", batt_num);
		goto end;
	}

	if (acpi_get_battery_status(batt_list[batt_num].handle, &status) != AE_OK) {
		printk(KERN_ERR "Cmbatt: acpi_get_battery_status failed\n");
		goto end;
	}

	p += sprintf(p, "Battery discharging:        %s\n",
		     (status.state & 0x1)?"yes":"no");
	p += sprintf(p, "Battery charging:           %s\n",
		     (status.state & 0x2)?"yes":"no");
	p += sprintf(p, "Battery critically low:     %s\n",
		     (status.state & 0x4)?"yes":"no");

	if (status.present_rate != ACPI_BATT_UNKNOWN)
		p += sprintf(p, "Battery rate:               %d %s\n",
			status.present_rate, batt_list[batt_num].power_unit);

	if (status.remaining_capacity != ACPI_BATT_UNKNOWN)
		p += sprintf(p, "Battery capacity:           %d %sh\n",
			status.remaining_capacity, batt_list[batt_num].power_unit);

	if (status.present_voltage != ACPI_BATT_UNKNOWN)
		p += sprintf(p, "Battery voltage:            %d mV\n",
			status.present_voltage);

end:

	len = (p - page);
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;
	return len;
}



int
acpi_cmbatt_init(void)
{
	int i;

	acpi_get_devices(ACPI_CMBATT_HID, 
			acpi_found_cmbatt,
			NULL,
			NULL);

	for (i = 0; i < batt_count; i++) {

		char batt_name[20];

		sprintf(batt_name, "power/batt%d_info", i);
		create_proc_read_entry(batt_name, 0, NULL,
			proc_read_batt_info, (void *) i);

		sprintf(batt_name, "power/batt%d_status", i);
		create_proc_read_entry(batt_name, 0, NULL,
			proc_read_batt_status, (void *) i);

	}

	return 0;
}

int
acpi_cmbatt_terminate(void)
{
	int i;

	for (i = 0; i < batt_count; i++) {

		char batt_name[20];

		sprintf(batt_name, "power/batt%d_info", i);
		remove_proc_entry(batt_name, NULL);

		sprintf(batt_name, "power/batt%d_status", i);
		remove_proc_entry(batt_name, NULL);
	}

	return 0;
}
