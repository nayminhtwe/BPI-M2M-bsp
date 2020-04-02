/*
 * A V4L2 driver for HM2131 cameras.
 *
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>

#include "camera.h"
#include "../isp_cfg/SENSOR_H/hm2131_default_f35.h"

MODULE_AUTHOR("lwj");
MODULE_DESCRIPTION("A low-level driver for HM2131 sensors");
MODULE_LICENSE("GPL");

/*for internel driver debug*/
#define DEV_DBG_EN 1
#if (DEV_DBG_EN == 1)
#define vfe_dev_dbg(x, arg...) printk(KERN_DEBUG "[HM2131]" x, ##arg)
#else
#define vfe_dev_dbg(x, arg...)
#endif
#define vfe_dev_err(x, arg...) printk(KERN_ERR "[HM2131]" x, ##arg)
#define vfe_dev_print(x, arg...) printk(KERN_INFO "[HM2131]" x, ##arg)

#define LOG_ERR_RET(x)\
	{\
		int ret;\
		ret = x;\
		if (ret < 0) {\
			vfe_dev_err("error at %s\n", __func__);\
			return ret;\
		} \
	}

#define MCLK (24 * 1000 * 1000)
#define VREF_POL V4L2_MBUS_VSYNC_ACTIVE_HIGH
#define HREF_POL V4L2_MBUS_HSYNC_ACTIVE_HIGH
#define CLK_POL V4L2_MBUS_PCLK_SAMPLE_RISING
#define V4L2_IDENT_SENSOR 0x2131
int hm2131_sensor_vts;

#define CSI_STBY_ON 0
#define CSI_STBY_OFF 1
#define CSI_RST_ON 0
#define CSI_RST_OFF 1
#define CSI_PWR_ON 1
#define CSI_PWR_OFF 0
#define CSI_AF_PWR_ON 1
#define CSI_AF_PWR_OFF 0
#define regval_list reg_list_a16_d8

#define REG_TERM 0xfffe
#define VAL_TERM 0xfe
#define REG_DLY 0xffff

/*
 * Our nominal (default) frame rate.
 */

#define SENSOR_FRAME_RATE 15

/*
 * The HM2131 sits on i2c with ID 0x6c
 */
#define I2C_ADDR 0x48
#define SENSOR_NAME "hm2131"
/* static struct delayed_work sensor_s_ae_ratio_work; */
static struct v4l2_subdev *glb_sd;

/*
 * Information we maintain about a known sensor.
 */
struct sensor_format_struct;	/* coming later */

struct cfg_array {		/* coming later */
	struct regval_list *regs;
	int size;
};

static inline struct sensor_info *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct sensor_info, sd);
}

/*
 * The default register settings
 *
 */

static struct regval_list sensor_default_regs[] = { };

static struct regval_list sensor_quxga_regs[] = { };

static struct regval_list sensor_1080p_regs_soft_reset[] = {
	{0x0103, 0x00}
};
static struct regval_list sensor_1080p_regs[] = {
	{0x0304, 0x2A}, {0x0305, 0x0C}, {0x0307, 0x55},
	{0x0303, 0x04}, {0x0309, 0x00}, {0x030A, 0x0A}, {0x030D, 0x02},
	{0x030F, 0x14}, {0x5268, 0x01}, {0x5264, 0x24}, {0x5265, 0x92},
	{0x5266, 0x23}, {0x5267, 0x07}, {0x5269, 0x02}, {0x0100, 0x02},
	{0x0100, 0x02}, {0x0111, 0x01}, {0x0112, 0x0A}, {0x0113, 0x0A},
	{0x4B20, 0xCE}, {0x4B18, 0x12}, {0x4B02, 0x05}, {0x4B43, 0x07},
	{0x4B05, 0x1C}, {0x4B0E, 0x00}, {0x4B0F, 0x0D}, {0x4B06, 0x06},
	{0x4B39, 0x0B}, {0x4B42, 0x03}, {0x4B03, 0x0C}, {0x4B04, 0x07},
	{0x4B3A, 0x0B}, {0x4B51, 0x80}, {0x4B52, 0x09}, {0x4B52, 0xC9},
	{0x4B57, 0x07}, {0x4B68, 0x6B}, {0x0350, 0x27}, {0x5030, 0x10},
	{0x5032, 0x02}, {0x5033, 0xD1}, {0x5034, 0x01}, {0x5035, 0x67},
	{0x5229, 0x90}, {0x5061, 0x00}, {0x5062, 0x94}, {0x50F5, 0x06},
	{0x5230, 0x00}, {0x526C, 0x00}, {0x5254, 0x08}, {0x522B, 0x00},
	{0x4144, 0x08}, {0x4148, 0x03}, {0x4024, 0x40}, {0x4B66, 0x00},
	{0x0340, 0x04}, {0x0341, 0xD6}, {0x0342, 0x04}, {0x0343, 0x78},
	{0x034C, 0x07}, {0x034D, 0x88}, {0x034E, 0x04}, {0x034F, 0x40},
	{0x0101, 0x00}, {0x4020, 0x10},
#ifdef HII_GAIN
	{0x50DD, 0x01}, {0x0350, 0x37},
#else
	{0x50DD, 0x00}, {0x0350, 0x27},
#endif
	{0x4131, 0x01}, {0x4132, 0x20}, {0x5011, 0x00}, {0x5015, 0x00},
	{0x501D, 0x1C}, {0x501E, 0x00}, {0x501F, 0x20}, {0x50D5, 0xF0},
	{0x50D7, 0x12}, {0x50BB, 0x14}, {0x5040, 0x07}, {0x50B7, 0x00},
	{0x50B8, 0x35}, {0x50B9, 0xFF}, {0x50BA, 0xFF}, {0x5200, 0x26},
	{0x5201, 0x00}, {0x5202, 0x00}, {0x5203, 0x00}, {0x5217, 0x01},
	{0x5234, 0x01}, {0x526B, 0x03}, {0x4C00, 0x00}, {0x4B31, 0x02},
	{0x4B3B, 0x02}, {0x4B44, 0x0C}, {0x4B45, 0x01}, {0x50A1, 0x00},
	{0x50AA, 0x2D}, {0x50AC, 0x11}, {0x50AB, 0x04}, {0x50A0, 0xB0},
	{0x50A2, 0x12}, {0x50AF, 0x00}, {0x5208, 0x55}, {0x5209, 0x0F},
	{0x520D, 0x40}, {0x5214, 0x18}, {0x5215, 0x0B}, {0x5216, 0x00},
	{0x521A, 0x10}, {0x521B, 0x24}, {0x5232, 0x04}, {0x5233, 0x03},
	{0x5106, 0xF0}, {0x510E, 0xF0}, {0x51C0, 0x80}, {0x51C4, 0x80},
	{0x51C8, 0x80}, {0x51CC, 0x80}, {0x51D0, 0x80}, {0x51D4, 0x8F},
	{0x51D8, 0x8F}, {0x51DC, 0x8F}, {0x51C1, 0x03}, {0x51C5, 0x13},
	{0x51C9, 0x17}, {0x51CD, 0x17}, {0x51D1, 0x17}, {0x51D5, 0x1F},
	{0x51D9, 0x1F}, {0x51DD, 0x1F}, {0x51C2, 0x4B}, {0x51C6, 0x4B},
	{0x51CA, 0x4B}, {0x51CE, 0x4B}, {0x51D2, 0x4B}, {0x51D6, 0x3B},
	{0x51DA, 0x3B}, {0x51DE, 0x39}, {0x51C3, 0x10}, {0x51C7, 0x10},
	{0x51CB, 0x10}, {0x51CF, 0x08}, {0x51D3, 0x08}, {0x51D7, 0x08},
	{0x51DB, 0x08}, {0x51DF, 0x00}, {0x5264, 0x23}, {0x5265, 0x07},
	{0x5266, 0x24}, {0x5267, 0x92}, {0x5268, 0x01}, {0xBAA2, 0xC0},
	{0xBAA2, 0x40}, {0xBA90, 0x01}, {0xBA93, 0x02}, {0x3110, 0x0B},
	{0x373E, 0x8A}, {0x373F, 0x8A}, {0x3713, 0x00}, {0x3717, 0x00},
	{0x5043, 0x01}, {0x0202, 0x03}, {0x0203, 0xff}, {0x0205, 0xe0},
	{0x0000, 0x00}, {0x0104, 0x01}, {0x0104, 0x00}, {0x0100, 0x03},
};

static struct regval_list sensor_sxga_regs[] = { };

/*
 * Here we'll try to encapsulate the changes for just the output
 * video format.
 *
 */

static struct regval_list sensor_fmt_raw[] = {

};

/*
 * Low-level register I/O.
 *
 */

/*
 * On most platforms, we'd rather do straight i2c I/O.
 */
static int sensor_read(struct v4l2_subdev *sd, unsigned short reg,
					 unsigned char *value)
{
	int ret = 0;
	int cnt = 0;

	ret = cci_read_a16_d8(sd, reg, value);
	while (ret != 0 && cnt < 2) {
		ret = cci_read_a16_d8(sd, reg, value);
		cnt++;
	}
	if (cnt > 0)
		vfe_dev_dbg("sensor read retry=%d\n", cnt);

	return ret;
}

static int sensor_write(struct v4l2_subdev *sd, unsigned short reg,
			unsigned char value)
{
	int ret = 0;
	int cnt = 0;
	ret = cci_write_a16_d8(sd, reg, value);
	while (ret != 0 && cnt < 2) {
		ret = cci_write_a16_d8(sd, reg, value);
		cnt++;
	}
	if (cnt > 0)
		vfe_dev_dbg("sensor write retry=%d\n", cnt);

	return ret;
}

/*
 * Write a list of register settings;
 */
static int sensor_write_array(struct v4l2_subdev *sd, struct regval_list *regs,
						int array_size)
{
	int i = 0;

	if (!regs)
		return -EINVAL;

	while (i < array_size) {
		if (regs->addr == REG_DLY)
			msleep(regs->data);
		else
			LOG_ERR_RET(sensor_write(sd, regs->addr, regs->data))

		i++;
		regs++;
	}
	return 0;
}

/*
 * Code for dealing with controls.
 * fill with different sensor module
 * different sensor module has different settings here
 * if not support the follow function ,retrun -EINVAL
 */

/* **********begin of********** */

static int sensor_g_hflip(struct v4l2_subdev *sd, __s32 *value)
{
	struct sensor_info *info = to_state(sd);
	unsigned char rdval;

	LOG_ERR_RET(sensor_read(sd, 0x3821, &rdval))

	rdval &= (1<<1);
	rdval >>= 1;

	*value = rdval;

	info->hflip = *value;
	return 0;
}

static int sensor_s_hflip(struct v4l2_subdev *sd, int value)
{
	struct sensor_info *info = to_state(sd);
	unsigned char rdval;

	if (info->hflip == value)
		return 0;

	LOG_ERR_RET(sensor_read(sd, 0x3821, &rdval))

				switch (value) {
				case 0:
						rdval &= 0xf9;
						break;
				case 1:
						rdval |= 0x06;
						break;
				default:
						return -EINVAL;
				}

				LOG_ERR_RET(sensor_write(sd, 0x3821, rdval))

				mdelay(10);
				info->hflip = value;
				return 0;
}

static int sensor_g_vflip(struct v4l2_subdev *sd, __s32 *value)
{
				struct sensor_info *info = to_state(sd);
				unsigned char rdval;

				LOG_ERR_RET(sensor_read(sd, 0x3820, &rdval))

				rdval &= (1<<1);
				*value = rdval;
				rdval >>= 1;

				info->vflip = *value;
				return 0;
}

static int sensor_s_vflip(struct v4l2_subdev *sd, int value)
{
				struct sensor_info *info = to_state(sd);
				unsigned char rdval;

				if (info->vflip == value)
					return 0;

				LOG_ERR_RET(sensor_read(sd, 0x3820, &rdval))

				switch (value) {

				case 0:
						rdval &= 0xf9;
						break;
				case 1:
						rdval |= 0x06;
						break;
				default:
						return -EINVAL;
				}
	LOG_ERR_RET(sensor_write(sd, 0x3820, rdval))

	mdelay(10);
	info->vflip = value;
	return 0;
}


static int sensor_g_exp(struct v4l2_subdev *sd, __s32 *value)
{
	struct sensor_info *info = to_state(sd);

	*value = info->exp;
	vfe_dev_dbg("sensor_get_exposure = %d\n", info->exp);
	return 0;
}

static int sensor_s_exp(struct v4l2_subdev *sd, unsigned int exp_val)
{
	unsigned char explow, expmid;
	struct sensor_info *info = to_state(sd);

	vfe_dev_dbg("sensor_set_exposure = %d\n", exp_val >> 4);
	if (exp_val > 0xfffff)
		exp_val = 0xfffff;

	expmid = (unsigned char)((0x0ff000 & exp_val) >> 12);
	explow = (unsigned char)((0x000ff0 & exp_val) >> 4);

	sensor_write(sd, 0x0104, 0x01);
	sensor_write(sd, 0x0202, expmid);
	sensor_write(sd, 0x0203, explow);
	sensor_write(sd, 0x0104, 0x00);

	info->exp = exp_val;
	return 0;
}

static int sensor_g_gain(struct v4l2_subdev *sd, __s32 *value)
{
	struct sensor_info *info = to_state(sd);

	*value = info->gain;
	vfe_dev_dbg("sensor_get_gain = %d\n", info->gain);
	return 0;
}

static int sensor_s_gain(struct v4l2_subdev *sd, int gain_val)
{
	struct sensor_info *info = to_state(sd);
	unsigned char gainlow = 0;

	if (gain_val < 1 * 16)
		gain_val = 16;
	if (gain_val > 64 * 16 - 1)
		gain_val = 64 * 16 - 1;
	vfe_dev_dbg("sensor_set_gain = %d\n", gain_val);
	/* if (info->gain == gain_val)
		 return 0; */

	gainlow = (unsigned char)(gain_val - 16);

	sensor_write(sd, 0x0104, 0x01);
	sensor_write(sd, 0x0205, gainlow);
	sensor_write(sd, 0x0104, 0x00);

	printk("2131 sensor_set_gain = %d, 0x%x, Done!\n", gain_val, gainlow);
	info->gain = gain_val;

	return 0;
}

static int sensor_s_exp_gain(struct v4l2_subdev *sd,
					 struct sensor_exp_gain *exp_gain)
{
	int exp_val, gain_val, frame_length, shutter;
	unsigned char explow = 0, expmid = 0;
	unsigned char gainlow = 0;
	struct sensor_info *info = to_state(sd);

	exp_val = exp_gain->exp_val;
	gain_val = exp_gain->gain_val;

	if ((info->exp == exp_val) && (info->gain == gain_val))
		return 0;

	if (gain_val < 1 * 16)
		gain_val = 16;
	if (gain_val > 64 * 16 - 1)
		gain_val = 64 * 16 - 1;

	if (exp_val > 0xfffff)
		exp_val = 0xfffff;

	shutter = exp_val / 16;
	if (shutter > hm2131_sensor_vts - 4)
		frame_length = shutter + 4;
	else
		frame_length = hm2131_sensor_vts;

	gainlow = (unsigned char)(gain_val - 16);
	expmid = (unsigned char)((0x0ff000 & exp_val) >> 12);
	explow = (unsigned char)((0x000ff0 & exp_val) >> 4);

	sensor_write(sd, 0x0104, 0x01);
	sensor_write(sd, 0x0202, expmid);
	sensor_write(sd, 0x0203, explow);
	sensor_write(sd, 0x0205, gainlow);
	sensor_write(sd, 0x0104, 0x00);

#if 0
	unsigned char tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;
	sensor_read(sd, 0x0205, &tmp0);
	sensor_read(sd, 0x008D, &tmp1);
	sensor_read(sd, 0x008F, &tmp2);
	sensor_read(sd, 0x0091, &tmp3);
	sensor_read(sd, 0x0093, &tmp4);
	sensor_read(sd, 0x0081, &tmp5);
#endif

	info->exp = exp_val;
	info->gain = gain_val;
	return 0;
}

static int sensor_s_sw_stby(struct v4l2_subdev *sd, int on_off)
{
	int ret;
	unsigned char rdval;

	ret = sensor_read(sd, 0x0100, &rdval);
	if (ret != 0)
		return ret;

	if (on_off == CSI_STBY_ON)
		ret = sensor_write(sd, 0x0100, rdval & 0xfe);
	else
		ret = sensor_write(sd, 0x0100, rdval | 0x01);
	return ret;
}

/*
 * Stuff that knows about the sensor.
 */

static int sensor_power(struct v4l2_subdev *sd, int on)
{
	int ret;
	unsigned long t_val;

	ret = 0;
	switch (on) {
	case CSI_SUBDEV_STBY_ON:
		vfe_dev_dbg("CSI_SUBDEV_STBY_ON!\n");
		ret = sensor_s_sw_stby(sd, CSI_STBY_ON);
		if (ret < 0)
			vfe_dev_err("soft stby falied!\n");
		usleep_range(10000, 12000);
		cci_lock(sd);
		vfe_gpio_write(sd, RESET, CSI_RST_ON);
		cci_unlock(sd);
		vfe_set_mclk(sd, OFF);
		break;
	case CSI_SUBDEV_STBY_OFF:
		vfe_dev_dbg("CSI_SUBDEV_STBY_OFF!\n");
		cci_lock(sd);
		vfe_set_mclk_freq(sd, MCLK);
		vfe_set_mclk(sd, ON);
		usleep_range(10000, 12000);
		vfe_gpio_write(sd, RESET, CSI_RST_OFF);
		usleep_range(10000, 12000);
		cci_unlock(sd);
		ret = sensor_s_sw_stby(sd, CSI_STBY_OFF);
		if (ret < 0)
			vfe_dev_err("soft stby off falied!\n");
		usleep_range(10000, 12000);
		break;
	case CSI_SUBDEV_PWR_ON:
		vfe_dev_dbg("CSI_SUBDEV_PWR_ON!\n");
		cci_lock(sd);
		vfe_gpio_set_status(sd, PWDN, 1);
		vfe_gpio_set_status(sd, RESET, 1);
		vfe_gpio_write(sd, PWDN, CSI_STBY_ON);
		vfe_gpio_write(sd, RESET, CSI_RST_ON);
		usleep_range(1000, 1200);
#if 0
		writel(0x80008000, 0xf1c20134);
		t_val = readl(0xf1c20890);
		t_val &= 0xffffff8f;
		t_val |= 0x20;
		writel(t_val, 0xf1c20890);
		usleep_range(10000, 12000);
#else
		vfe_set_mclk_freq(sd, MCLK);
		vfe_set_mclk(sd, ON);
#endif
		vfe_gpio_write(sd, POWER_EN, CSI_PWR_ON);
		vfe_set_pmu_channel(sd, IOVDD, ON);
		vfe_set_pmu_channel(sd, AVDD, ON);
		vfe_set_pmu_channel(sd, DVDD, ON);
		vfe_set_pmu_channel(sd, AFVDD, ON);

		vfe_gpio_write(sd, PWDN, CSI_STBY_OFF);
		usleep_range(10000, 12000);
		vfe_gpio_write(sd, RESET, CSI_RST_OFF);
		usleep_range(30000, 31000);

		cci_unlock(sd);

		break;
	case CSI_SUBDEV_PWR_OFF:
		vfe_dev_dbg("CSI_SUBDEV_PWR_OFF!\n");
		cci_lock(sd);
#if 0
		t_val = readl(0xf1c20890);
		t_val &= 0xffffff8f;
		t_val |= 0x70;
		writel(t_val, 0xf1c20890);
		writel(0x80000000, 0xf1c20134);
#else
		vfe_set_mclk(sd, OFF);
#endif
		vfe_gpio_write(sd, POWER_EN, CSI_PWR_OFF);
		vfe_set_pmu_channel(sd, AFVDD, OFF);
		vfe_set_pmu_channel(sd, DVDD, OFF);
		vfe_set_pmu_channel(sd, AVDD, OFF);
		vfe_set_pmu_channel(sd, IOVDD, OFF);

		usleep_range(10000, 12000);
		vfe_gpio_write(sd, POWER_EN, CSI_PWR_OFF);
		vfe_gpio_write(sd, RESET, CSI_RST_ON);
		vfe_gpio_write(sd, PWDN, CSI_STBY_ON);

		vfe_gpio_set_status(sd, RESET, 0);
		vfe_gpio_set_status(sd, PWDN, 0);

		cci_unlock(sd);

		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sensor_reset(struct v4l2_subdev *sd, u32 val)
{
	switch (val) {
	case 0:
		vfe_dev_dbg("CSI_RST_OFF!\n");
		vfe_gpio_write(sd, RESET, CSI_RST_OFF);
		usleep_range(10000, 12000);
		break;
	case 1:
		vfe_dev_dbg("CSI_RST_ON!\n");
		vfe_gpio_write(sd, RESET, CSI_RST_ON);
		usleep_range(10000, 12000);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sensor_detect(struct v4l2_subdev *sd)
{
	unsigned char rdval;

	LOG_ERR_RET(sensor_read(sd, 0x0000, &rdval))
	if (rdval != 0x21)
		return -ENODEV;

	LOG_ERR_RET(sensor_read(sd, 0x0001, &rdval))
	if (rdval != 0x31)
		return -ENODEV;

	return 0;
}

static int sensor_init(struct v4l2_subdev *sd, u32 val)
{
	int ret;
	struct sensor_info *info = to_state(sd);

	vfe_dev_dbg("sensor_init\n");

	/*Make sure it is a target sensor */
	ret = sensor_detect(sd);
	if (ret) {
		vfe_dev_err("chip found is not an target chip.\n");
		return ret;
	}

	vfe_get_standby_mode(sd, &info->stby_mode);

	if ((info->stby_mode == HW_STBY || info->stby_mode == SW_STBY) &&
			info->init_first_flag == 0) {
		vfe_dev_print("stby_mode and init_first_flag = 0\n");
		return 0;
	}

	info->focus_status = 0;
	info->low_speed = 0;
	info->width = HD1080_WIDTH;
	info->height = HD1080_HEIGHT;
	info->hflip = 0;
	info->vflip = 0;
	info->gain = 0;

	info->tpf.numerator = 1;
	info->tpf.denominator = 30;	/* 30fps */

	sensor_write_array(sd, sensor_1080p_regs_soft_reset,
				 ARRAY_SIZE(sensor_1080p_regs_soft_reset));
	msleep(100);
	ret =	sensor_write_array(sd, sensor_1080p_regs,
					 ARRAY_SIZE(sensor_1080p_regs));
	if (ret < 0) {
		vfe_dev_err("write sensor_default_regs error\n");
		return ret;
	}

	if (info->stby_mode == 0)
		info->init_first_flag = 0;

	info->preview_first_flag = 1;

	return 0;
}

static long sensor_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	int ret = 0;
	struct sensor_info *info = to_state(sd);
	switch (cmd) {
	case GET_CURRENT_WIN_CFG:
		if (info->current_wins != NULL) {
			memcpy(arg, info->current_wins,
				sizeof(struct sensor_win_size));
			ret = 0;
		} else {
			vfe_dev_err("empty wins!\n");
			ret = -1;
		}
		break;
	case SET_FPS:
		ret = 0;
		break;
	case ISP_SET_EXP_GAIN:
		ret = sensor_s_exp_gain(sd, (struct sensor_exp_gain *)arg);
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

/*
 * Store information about the video data format.
 */
static struct sensor_format_struct {
	__u8 *desc;
	enum v4l2_mbus_pixelcode mbus_code;
	struct regval_list *regs;
	int regs_size;
	int bpp;		/* Bytes per pixel */
} sensor_formats[] = {
	{
		.desc = "Raw RGB Bayer",
		.mbus_code = V4L2_MBUS_FMT_SBGGR10_10X1,
		.regs = sensor_fmt_raw,
		.regs_size = ARRAY_SIZE(sensor_fmt_raw),
		.bpp = 1
	},
};
#define N_FMTS ARRAY_SIZE(sensor_formats)

/*
 * Then there is the issue of window sizes.  Try to capture the info here.
 */

static struct sensor_win_size sensor_win_sizes[] = {
#if 0
	/* quxga: 3264*2448 */
	{
	 .width = QUXGA_WIDTH,
	 .height = QUXGA_HEIGHT,
	 .hoffset = 0,
	 .voffset = 0,
	 .hts = 3578,
	 .vts = 2534,
	 .pclk = 136 * 1000 * 1000,
	 .mipi_bps = 720 * 1000 * 1000,
	 .fps_fixed = 2,
	 .bin_factor = 1,
	 .intg_min = 16,
	 .intg_max = 2534 << 4,
	 .gain_min = 1 << 4,
	 .gain_max = 10 << 4,
	 .regs = sensor_quxga_regs,
	 .regs_size = ARRAY_SIZE(sensor_quxga_regs),
	 .set_size = NULL,
	 },
#endif
#if 1
	/* 1080P */
	{
	 .width = HD1080_WIDTH,
	 .height = HD1080_HEIGHT,
	 .hoffset = 0,
	 .voffset = 0,
	 .hts = 1144,
	 .vts = 1238,
	 .pclk = 42500000,
	 .mipi_bps = 720 * 1000 * 1000,
	 .fps_fixed = 30,
	 .bin_factor = 1,
	 .intg_min = 1 << 4,
	 .intg_max = (1238 - 4) << 4,
	 .gain_min = 1 << 4,
	 .gain_max = 15 << 4,
	 .regs = sensor_1080p_regs,
	 .regs_size = ARRAY_SIZE(sensor_1080p_regs),
	 .set_size = NULL,
	.isp_cfg = &hm2131_default_f35,
	 },
#endif
#if 0

	/* SXGA */
	{
	 .width = SXGA_WIDTH,
	 .height = SXGA_HEIGHT,
	 .hoffset = 176,
	 .voffset = 132,
	 .hts = 3573,
	 .vts = 1306,
	 .pclk = 140 * 1000 * 1000,
	 .mipi_bps = 720 * 1000 * 1000,
	 .fps_fixed = 1,
	 .bin_factor = 1,
	 .intg_min = 1,
	 .intg_max = 1306 << 4,
	 .gain_min = 1 << 4,
	 .gain_max = 12 << 4,
	 .regs = sensor_sxga_regs,
	 .regs_size = ARRAY_SIZE(sensor_sxga_regs),
	 .set_size = NULL,
	 },

	/* 720p */
	{
	 .width = HD720_WIDTH,
	 .height = HD720_HEIGHT,
	 .hoffset = 176,
	 .voffset = 252,
	 .hts = 3573,
	 .vts = 1306,
	 .pclk = 140 * 1000 * 1000,
	 .mipi_bps = 720 * 1000 * 1000,
	 .fps_fixed = 1,
	 .bin_factor = 1,
	 .intg_min = 16,
	 .intg_max = 1306 << 4,
	 .gain_min = 1 << 4,
	 .gain_max = 12 << 4,
	 .regs = sensor_sxga_regs,
	 .regs_size = ARRAY_SIZE(sensor_sxga_regs),
	 .set_size = NULL,
	 },
#endif
};

#define N_WIN_SIZES (ARRAY_SIZE(sensor_win_sizes))

static int sensor_enum_fmt(struct v4l2_subdev *sd, unsigned index,
				 enum v4l2_mbus_pixelcode *code)
{
	if (index >= N_FMTS)
		return -EINVAL;

	*code = sensor_formats[index].mbus_code;
	return 0;
}

static int sensor_enum_size(struct v4l2_subdev *sd,
					struct v4l2_frmsizeenum *fsize)
{
	if (fsize->index > N_WIN_SIZES - 1)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = sensor_win_sizes[fsize->index].width;
	fsize->discrete.height = sensor_win_sizes[fsize->index].height;

	return 0;
}

static int sensor_try_fmt_internal(struct v4l2_subdev *sd,
					 struct v4l2_mbus_framefmt *fmt,
					 struct sensor_format_struct **ret_fmt,
					 struct sensor_win_size **ret_wsize)
{
	int index;
	struct sensor_win_size *wsize;
	struct sensor_info *info = to_state(sd);

	for (index = 0; index < N_FMTS; index++)
		if (sensor_formats[index].mbus_code == fmt->code)
			break;

	if (index >= N_FMTS)
		return -EINVAL;

	if (ret_fmt != NULL)
		*ret_fmt = sensor_formats + index;

	/*
	 * Fields: the sensor devices claim to be progressive.
	 */

	fmt->field = V4L2_FIELD_NONE;

	/*
	 * Round requested image size down to the nearest
	 * we support, but not below the smallest.
	 */
	for (wsize = sensor_win_sizes; wsize < sensor_win_sizes + N_WIN_SIZES;
									wsize++)
		if (fmt->width >= wsize->width && fmt->height >= wsize->height)
			break;

	if (wsize >= sensor_win_sizes + N_WIN_SIZES)
		wsize--;	/* Take the smallest one */
	if (ret_wsize != NULL)
		*ret_wsize = wsize;

	info->current_wins = wsize;

	/*
	 * Note the size we'll actually handle.
	 */
	fmt->width = wsize->width;
	fmt->height = wsize->height;

	return 0;
}

static int sensor_try_fmt(struct v4l2_subdev *sd,
				struct v4l2_mbus_framefmt *fmt)
{
	return sensor_try_fmt_internal(sd, fmt, NULL, NULL);
}

static int sensor_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *cfg)
{
	cfg->type = V4L2_MBUS_CSI2;
	cfg->flags = 0 | V4L2_MBUS_CSI2_2_LANE | V4L2_MBUS_CSI2_CHANNEL_0;

	return 0;
}

/*
 * Set a format.
 */
static int sensor_s_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *fmt)
{
	int ret;

	struct sensor_format_struct *sensor_fmt;
	struct sensor_win_size *wsize;
	struct sensor_info *info = to_state(sd);

	vfe_dev_dbg("sensor_s_fmt\n");

	ret = sensor_try_fmt_internal(sd, fmt, &sensor_fmt, &wsize);
	if (ret)
		return ret;

	if (info->capture_mode == V4L2_MODE_VIDEO) {
		/*video */
		;
	} else if (info->capture_mode == V4L2_MODE_IMAGE) {
		/*image */
		;
	}

	sensor_write_array(sd, sensor_fmt->regs, sensor_fmt->regs_size);

	ret = 0;
	if (wsize->regs)
		LOG_ERR_RET(sensor_write_array
			(sd, wsize->regs, wsize->regs_size))

	if (wsize->set_size)
		LOG_ERR_RET(wsize->set_size(sd))

	info->fmt = sensor_fmt;
	info->width = wsize->width;
	info->height = wsize->height;
	hm2131_sensor_vts = wsize->vts;

	vfe_dev_print("s_fmt = %x, width = %d, height = %d\n",
			sensor_fmt->mbus_code, wsize->width, wsize->height);

	if (info->capture_mode == V4L2_MODE_VIDEO) {
		/*video */
		;
	} else {
		/*capture image */
	}

	vfe_dev_print("s_fmt end\n");
	return 0;
}

/*
 * Implement G/S_PARM.  There is a "high quality" mode we could try
 * to do someday; for now, we just do the frame rate tweak.
 */
static int sensor_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	struct v4l2_captureparm *cp = &parms->parm.capture;
	struct sensor_info *info = to_state(sd);

	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	memset(cp, 0, sizeof(struct v4l2_captureparm));
	cp->capability = V4L2_CAP_TIMEPERFRAME;
	cp->capturemode = info->capture_mode;

	return 0;
}

static int sensor_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	struct v4l2_captureparm *cp = &parms->parm.capture;
	struct sensor_info *info = to_state(sd);

	vfe_dev_dbg("sensor_s_parm\n");

	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (info->tpf.numerator == 0)
		return -EINVAL;

	info->capture_mode = cp->capturemode;

	return 0;
}

static int sensor_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc)
{
	/* Fill in min, max, step and default value for these controls. */
	/* see include/linux/videodev2.h for details */

	switch (qc->id) {
	case V4L2_CID_GAIN:
		return v4l2_ctrl_query_fill(qc, 1 * 16, 32 * 16, 1, 16);
	case V4L2_CID_EXPOSURE:
		return v4l2_ctrl_query_fill(qc, 0, 65535 * 16, 1, 0);
	}
	return -EINVAL;
}

static int sensor_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		return sensor_g_gain(sd, &ctrl->value);
	case V4L2_CID_EXPOSURE:
		return sensor_g_exp(sd, &ctrl->value);
	}
	return -EINVAL;
}

static int sensor_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct v4l2_queryctrl qc;
	int ret;

	qc.id = ctrl->id;
	ret = sensor_queryctrl(sd, &qc);
	if (ret < 0)
		return ret;

	if (ctrl->value < qc.minimum || ctrl->value > qc.maximum)
		return -ERANGE;

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		return sensor_s_gain(sd, ctrl->value);
	case V4L2_CID_EXPOSURE:
		return sensor_s_exp(sd, ctrl->value);
	}
	return -EINVAL;
}

static int sensor_g_chip_ident(struct v4l2_subdev *sd,
				struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_SENSOR, 0);
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops sensor_core_ops = {
	.g_chip_ident = sensor_g_chip_ident,
	.g_ctrl = sensor_g_ctrl,
	.s_ctrl = sensor_s_ctrl,
	.queryctrl = sensor_queryctrl,
	.reset = sensor_reset,
	.init = sensor_init,
	.s_power = sensor_power,
	.ioctl = sensor_ioctl,
};

static const struct v4l2_subdev_video_ops sensor_video_ops = {
	.enum_mbus_fmt = sensor_enum_fmt,
	.enum_framesizes = sensor_enum_size,
	.try_mbus_fmt = sensor_try_fmt,
	.s_mbus_fmt = sensor_s_fmt,
	.s_parm = sensor_s_parm,
	.g_parm = sensor_g_parm,
	.g_mbus_config = sensor_g_mbus_config,
};

static const struct v4l2_subdev_ops sensor_ops = {
	.core = &sensor_core_ops,
	.video = &sensor_video_ops,
};

/* ----------------------------------------------------------------------- */
static struct cci_driver cci_drv = {
	.name = SENSOR_NAME,
};

static int sensor_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct sensor_info *info;

	info = kzalloc(sizeof(struct sensor_info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;
	sd = &info->sd;
	glb_sd = sd;
	cci_dev_probe_helper(sd, client, &sensor_ops, &cci_drv);

	info->fmt = &sensor_formats[0];
	info->af_first_flag = 1;
	info->init_first_flag = 1;

	return 0;
}

static int sensor_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd;

	sd = cci_dev_remove_helper(client, &cci_drv);
	kfree(to_state(sd));
	return 0;
}

static const struct i2c_device_id sensor_id[] = { {SENSOR_NAME, 0}, {} };

MODULE_DEVICE_TABLE(i2c, sensor_id);

static struct i2c_driver sensor_driver = {
	.driver = {
			.owner = THIS_MODULE,
			.name = SENSOR_NAME,
			 },
	.probe = sensor_probe,
	.remove = sensor_remove,
	.id_table = sensor_id,
};

static __init int init_sensor(void)
{
	return cci_dev_init_helper(&sensor_driver);
}

static __exit void exit_sensor(void)
{
	cci_dev_exit_helper(&sensor_driver);
}

module_init(init_sensor);
module_exit(exit_sensor);
