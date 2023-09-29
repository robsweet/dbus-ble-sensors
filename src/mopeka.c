#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include <velib/utils/ve_item_utils.h>
#include <velib/vecan/products.h>
#include <velib/utils/ve_logger.h>

#include "ble-dbus.h"
#include "mopeka.h"
#include "task.h"

#define HW_ID_PRO                3     // Pro Check LPG bottom-up
#define HW_ID_PRO_200            4     // Pro-200, top-down
#define HW_ID_PRO_H2O            5     // Pro Check H2O, bottom-up
#define HW_ID_PRO_PLUS_BLE       8     // PRO+ bottom-up, Boosted BLE sensor
#define HW_ID_PRO_PLUS_CELL      9     // PRO+ bottom-up, Bluetooth + Cellular Sensor
#define HW_ID_TOPDOWN_BLE        10    // TD-40 or TD-200, Top-down, Boosted BLE sensor
#define HW_ID_TOPDOWN_CELL       11    // TD-40 or TD-200, Top-down, Bluetooth + Cellular Sensor
#define HW_ID_UNIVERSAL          12    // Pro Check Universal Sensor, bottom-up

#define FLUID_TYPE_FRESH_WATER   1
#define FLUID_TYPE_WASTE_WATER   2
#define FLUID_TYPE_LIVE_WELL     3
#define FLUID_TYPE_OIL           4
#define FLUID_TYPE_BLACK_WATER   5
#define FLUID_TYPE_GASOLINE      6
#define FLUID_TYPE_DIESEL        7
#define FLUID_TYPE_LPG           8
#define FLUID_TYPE_LNG           9
#define FLUID_TYPE_HYDRAULIC_OIL 10
#define FLUID_TYPE_RAW_WATER     11

static struct VeSettingProperties capacity_props = {
	.type			= VE_FLOAT,
	.def.value.Float	= 0.2,
	.min.value.Float	= 0,
	.max.value.Float	= 1000,
};

static struct VeSettingProperties fluid_type_props = {
	.type			= VE_SN32,
	.def.value.SN32		= 0,
	.min.value.SN32		= 0,
	.max.value.SN32		= INT32_MAX - 3,
};

static struct VeSettingProperties empty_props = {
	.type			= VE_FLOAT,
	.def.value.Float	= 0,
	.min.value.Float	= 0,
	.max.value.Float	= 150,
};

static struct VeSettingProperties full_props = {
	.type			= VE_FLOAT,
	.def.value.Float	= 20,
	.min.value.Float	= 0,
	.max.value.Float	= 150,
};

static const struct dev_setting mopeka_settings[] = {
	{
		.name	= "Capacity",
		.props	= &capacity_props,
	},
	{
		.name	= "FluidType",
		.props	= &fluid_type_props,
	},
	{
		.name	= "RawValueEmpty",
		.props	= &empty_props,
	},
	{
		.name	= "RawValueFull",
		.props	= &full_props,
	},
};

static struct VeSettingProperties butane_props = {
	.type			= VE_SN32,
	.def.value.SN32		= 0,
	.min.value.SN32		= 0,
	.max.value.SN32		= 100,
};

static const struct dev_setting mopeka_lpg_settings[] = {
	{
		.name	= "ButaneRatio",
		.props	= &butane_props,
	},
};

static int mopeka_init(struct VeItem *root, void *data)
{
	int hwid = (int)data;
	VeVariant v;

	ble_dbus_set_str(root, "RawUnit", "cm");
	ble_dbus_set_item(root, "Remaining",
			  veVariantInvalidType(&v, VE_FLOAT), &veUnitm3);

	// Add butane slider only for bottom up sensors that might support propane/butane
	if (hwid != HW_ID_PRO_H2O && hwid != HW_ID_TOPDOWN_BLE && hwid != HW_ID_TOPDOWN_CELL && hwid != HW_ID_PRO_200) {
		ble_dbus_add_settings(root, mopeka_lpg_settings,
					  array_size(mopeka_lpg_settings));
	}

	return 0;
}

static const struct dev_info mopeka_sensor = {
	.product_id	= VE_PROD_ID_MOPEKA_SENSOR,
	.dev_instance	= 20,
	.dev_prefix	= "mopeka_",
	.role		= "tank",
	.num_settings	= array_size(mopeka_settings),
	.settings	= mopeka_settings,
	.init		= mopeka_init,
};

static const float mopeka_coefs_h2o[] = {
	0.600592, 0.003124, -0.00001368,
};

static const float mopeka_coefs_lpg[] = {
	0.573045, -0.002822, -0.00000535,
};

static const float mopeka_coefs_gasoline[] = {
	0.7373417462, -0.001978229885, 0.00000202162,
};

static const float mopeka_coefs_butane[] = {
	0.03615, 0.000815,
};

static const float mopeka_coefs_air[] = {
	0.153096, 0.000327, -0.000000294,
};

static float mopeka_scale_butane(struct VeItem *root, int temp)
{
	float r = veItemValueInt(root, "ButaneRatio") / 100.0;

	return mopeka_coefs_butane[0] * r + mopeka_coefs_butane[1] * r * temp;
}

static int mopeka_xlate_level(struct VeItem *root, VeVariant *val, uint64_t rv)
{
	const float *coefs;
	float scale = 0;
	float level;
	int hwid;
	int temp;
	int fluid_type;
	int tank_level_ext;

	fluid_type = veItemValueInt(root, "FluidType");
	hwid = veItemValueInt(root, "HardwareID");
	temp = veItemValueInt(root, "Temperature");
	temp += 40;

	// Check for presence of extension bit on certain hardware/firmware -
	// it will always be 0 on old firmware/hardware where raw value saturates
	// at 16383.  When extension bit is set, the raw_value resolution changes
	// to 4us with 16384us offet.  Thus legacy sensors and firmware still 
	// have 0 to 16383us range with 1us, and new versions add the range 16384us 
	// to 81916us with 4us resolution
	tank_level_ext = veItemValueInt(root, "TankLevelExtension");
	if (tank_level_ext) {
		rv = 16384 + rv * 4;
	}

	switch (hwid) {
	case HW_ID_PRO:
		scale = mopeka_scale_butane(root, temp);
		coefs = mopeka_coefs_lpg;
		break;
	case HW_ID_PRO_H2O:
		coefs = mopeka_coefs_h2o;
		break;
	case HW_ID_PRO_PLUS_BLE:
	case HW_ID_PRO_PLUS_CELL:
	case HW_ID_UNIVERSAL:
		switch (fluid_type) {
		case FLUID_TYPE_FRESH_WATER:
		case FLUID_TYPE_WASTE_WATER:
		case FLUID_TYPE_LIVE_WELL:
		case FLUID_TYPE_BLACK_WATER:
		case FLUID_TYPE_RAW_WATER:
			// printf("Using H2O coefficients for level calculation\n");
			coefs = mopeka_coefs_h2o;
			break;
		case FLUID_TYPE_LPG:
			// printf("Using LPG coefficients for level calculation\n");
			scale = mopeka_scale_butane(root, temp);
			coefs = mopeka_coefs_lpg;
			break;
		case FLUID_TYPE_GASOLINE:
		case FLUID_TYPE_DIESEL:
			coefs = mopeka_coefs_gasoline;
			break;
		case FLUID_TYPE_LNG:
		case FLUID_TYPE_OIL:
		case FLUID_TYPE_HYDRAULIC_OIL:
		default:
			// printf("No coefficients for fluid type\n");
			return -1;
		}
		break;
	case HW_ID_PRO_200:
	case HW_ID_TOPDOWN_BLE:
	case HW_ID_TOPDOWN_CELL:
		// printf("Using AIR coefficients for level calculation\n");
		coefs = mopeka_coefs_air;
		break;
	default:
		return -1;
	}

	scale += coefs[0] + coefs[1] * temp + coefs[2] * temp * temp;
	level = rv * scale;
	veVariantFloat(val, level / 10);

	return 0;
}

static const struct reg_info mopeka_adv[] = {
	{
		.type	= VE_UN8,
		.offset	= 0,
		.mask   = 0x7f,
		.name	= "HardwareID",
		.format	= &veUnitNone,
	},
	{
		.type	= VE_UN8,
		.offset	= 0,
		.shift	= 7,
		.mask   = 1,
		.name	= "TankLevelExtension",
		.format = &veUnitNone,
	},
	{
		.type	= VE_UN8,
		.offset	= 1,
		.mask   = 0x7f,
		.scale  = 32,
		.name   = "BatteryVoltage",
		.format = &veUnitVolt2Dec,
	},
	{
		.type	= VE_UN8,
		.offset	= 2,
		.mask   = 0x7f,
		.scale	= 1,
		.bias	= -40,
		.name	= "Temperature",
		.format	= &veUnitCelsius1Dec,
	},
	{
		.type	= VE_UN8,
		.offset	= 2,
		.shift	= 7,
		.mask   = 1,
		.name	= "SyncButton",
		.format = &veUnitNone,
	},
	{
		.type	= VE_UN16,
		.offset	= 3,
		.mask   = 0x3fff,
		.xlate	= mopeka_xlate_level,
		.name	= "RawValue",
		.format	= &veUnitcm,
	},
	{
		.type	= VE_UN16,
		.offset	= 3,
		.shift	= 14,
		.mask   = 3,
		.name	= "Quality",
		.format	= &veUnitNone,
	},
	{
		.type	= VE_SN8,
		.offset	= 8,
		.scale	= 1024,
		.name	= "AccelX",
		.format	= &veUnitG2Dec,
	},
	{
		.type	= VE_SN8,
		.offset	= 9,
		.scale	= 1024,
		.name	= "AccelY",
		.format	= &veUnitG2Dec,
	},
};

static void mopeka_update_level(struct VeItem *root)
{
	struct VeItem *item;
	int hwid;
	float capacity;
	int height;
	int empty;
	int full;
	float level;
	float remain;
	VeVariant v;

	hwid = veItemValueInt(root, "HardwareID");
	item = veItemByUid(root, "Capacity");
	if (!item)
		return;

	veItemLocalValue(item, &v);
	veVariantToFloat(&v);
	capacity = v.value.Float;

	height = veItemValueInt(root, "RawValue");
	empty = veItemValueInt(root, "RawValueEmpty");
	full = veItemValueInt(root, "RawValueFull");

	if (hwid == HW_ID_TOPDOWN_BLE || hwid == HW_ID_TOPDOWN_CELL || hwid == HW_ID_PRO_200) {
		if (empty < full) {
			veItemInvalidate(veItemByUid(root, "Level"));
			veItemInvalidate(veItemByUid(root, "Remaining"));
			ble_dbus_set_int(root, "Status", 4);
			return;
		}

		level = (float)(empty - full - height) / (empty - full);
	} else {
		if (empty >= full) {
			veItemInvalidate(veItemByUid(root, "Level"));
			veItemInvalidate(veItemByUid(root, "Remaining"));
			ble_dbus_set_int(root, "Status", 4);
			return;
		}

		level = (float)(height - empty) / (full - empty);
	}

	if (level < 0)
		level = 0;
	if (level > 1)
		level = 1;

	remain = level * capacity;

	ble_dbus_set_int(root, "Level", lrintf(100 * level));

	item = veItemByUid(root, "Remaining");
	veItemOwnerSet(item, veVariantFloat(&v, remain));
}

int mopeka_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len)
{
	struct VeItem *root;
	const uint8_t *uid = buf + 5;
	const char *type;
	char name[24];
	char dev[16];
	int hwid;

	if (len != 10)
		return -1;

	if (uid[0] != addr->b[2] ||
		uid[1] != addr->b[1] ||
		uid[2] != addr->b[0])
		return -1;

	hwid = buf[0];

	switch (hwid) {
	case HW_ID_PRO:
		type = "Pro";
		break;
	case HW_ID_PRO_H2O:
		type = "Pro H2O";
		break;
	case HW_ID_PRO_200:
		type = "Pro 200";
		break;
	case HW_ID_PRO_PLUS_BLE:
		type = "Pro+ BLE";
		break;
	case HW_ID_PRO_PLUS_CELL:
		type = "Pro+ Cell";
		break;
	case HW_ID_TOPDOWN_BLE:
		type = "Topdown BLE";
		break;
	case HW_ID_TOPDOWN_CELL:
		type = "Topdown Cell";
		break;
	case HW_ID_UNIVERSAL  :
		type = "Universal";
		break;
	default:
		return -1;
	}

	snprintf(dev, sizeof(dev), "%02x%02x%02x%02x%02x%02x",
		 addr->b[5], addr->b[4], addr->b[3],
		 addr->b[2], addr->b[1], addr->b[0]);

	root = ble_dbus_create(dev, &mopeka_sensor, (void *)hwid);
	if (!root)
		return -1;

	snprintf(name, sizeof(name), "Mopeka %s %02X:%02X:%02X",
		 type, uid[0], uid[1], uid[2]);
	ble_dbus_set_name(root, name);

	if (!ble_dbus_is_enabled(root))
		return 0;

	ble_dbus_set_regs(root, mopeka_adv, array_size(mopeka_adv), buf, len);

	mopeka_update_level(root);
	ble_dbus_update(root);

	return 0;
}

