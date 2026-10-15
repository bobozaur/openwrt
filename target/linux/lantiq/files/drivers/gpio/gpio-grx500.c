// SPDX-License-Identifier: GPL-2.0-only
/* GRX500 GPIO bank and per-pin GPIO mux selection. Register layout follows
 * the GPL Lantiq/Intel BSP. Atomic set/clear registers preserve other owners.
 * Interrupt and alternate-function pinctrl support are separate work.
 */
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>

#define GPIO_OUT	0x00
#define GPIO_IN	0x04
#define GPIO_DIR	0x08
#define GPIO_SET	0x40
#define GPIO_CLEAR	0x44
#define GPIO_DIR_SET	0x48
#define GPIO_DIR_CLEAR	0x4c
#define PAD_PULL_UP	0x80
#define PAD_PULL_DOWN	0x84
#define PAD_SLEW		0x88
#define PAD_DRIVE_LOW	0x8c
#define PAD_DRIVE_HIGH	0x90
#define PAD_OPEN_DRAIN	0x94
#define RCU_IFMUX_CFG	0x80120
#define RCU_BBSPI_ENABLE	BIT(18)

struct grx500_gpio {
	struct gpio_chip chip;
	void __iomem *port;
	void __iomem *pad;
	spinlock_t lock;
};

static void grx500_pad_update(struct grx500_gpio *gpio, u32 reg, u32 mask, u32 value)
{
	__raw_writel((__raw_readl(gpio->pad + reg) & ~mask) | value, gpio->pad + reg);
	wmb();
}

static int grx500_gpio_request(struct gpio_chip *chip, unsigned int pin)
{
	struct grx500_gpio *gpio = gpiochip_get_data(chip);
	unsigned long flags;
	u32 drive = pin < 16 ? PAD_DRIVE_LOW : PAD_DRIVE_HIGH;

	/* Restore the claimed pin's GPIO output defaults. Firmware can leave
	 * GPIOs used for MDIO in open-drain mode, preventing PERST release.
	 * Bias remains under the descriptor's pull flags.
	 */
	spin_lock_irqsave(&gpio->lock, flags);
	grx500_pad_update(gpio, pin * 4, 7, 0);
	grx500_pad_update(gpio, PAD_OPEN_DRAIN, BIT(pin), 0);
	grx500_pad_update(gpio, PAD_SLEW, BIT(pin), 0);
	grx500_pad_update(gpio, drive, 3U << (2 * (pin % 16)), 0);
	spin_unlock_irqrestore(&gpio->lock, flags);
	return 0;
}

static int grx500_gpio_set_config(struct gpio_chip *chip, unsigned int pin,
				 unsigned long config)
{
	struct grx500_gpio *gpio = gpiochip_get_data(chip);
	enum pin_config_param param = pinconf_to_config_param(config);
	unsigned long flags;
	u32 bit = BIT(pin);
	int ret = 0;

	spin_lock_irqsave(&gpio->lock, flags);
	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN:
		grx500_pad_update(gpio, PAD_PULL_UP, bit,
				  param == PIN_CONFIG_BIAS_PULL_UP ? bit : 0);
		grx500_pad_update(gpio, PAD_PULL_DOWN, bit,
				  param == PIN_CONFIG_BIAS_PULL_DOWN ? bit : 0);
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		grx500_pad_update(gpio, PAD_OPEN_DRAIN, bit,
				  param == PIN_CONFIG_DRIVE_OPEN_DRAIN ? bit : 0);
		break;
	default:
		ret = -ENOTSUPP;
	}
	spin_unlock_irqrestore(&gpio->lock, flags);
	return ret;
}

static int grx500_gpio_get_direction(struct gpio_chip *chip, unsigned int pin)
{
	struct grx500_gpio *gpio = gpiochip_get_data(chip);

	return (__raw_readl(gpio->port + GPIO_DIR) & BIT(pin)) ?
		GPIO_LINE_DIRECTION_OUT : GPIO_LINE_DIRECTION_IN;
}

static int grx500_gpio_set(struct gpio_chip *chip, unsigned int pin, int value)
{
	struct grx500_gpio *gpio = gpiochip_get_data(chip);

	__raw_writel(BIT(pin), gpio->port + (value ? GPIO_SET : GPIO_CLEAR));
	wmb();
	return 0;
}

static int grx500_gpio_direction_input(struct gpio_chip *chip, unsigned int pin)
{
	struct grx500_gpio *gpio = gpiochip_get_data(chip);

	__raw_writel(BIT(pin), gpio->port + GPIO_DIR_CLEAR);
	wmb();
	return 0;
}

static int grx500_gpio_direction_output(struct gpio_chip *chip, unsigned int pin,
				      int value)
{
	struct grx500_gpio *gpio = gpiochip_get_data(chip);

	grx500_gpio_set(chip, pin, value);
	__raw_writel(BIT(pin), gpio->port + GPIO_DIR_SET);
	wmb();
	return 0;
}

static int grx500_gpio_get(struct gpio_chip *chip, unsigned int pin)
{
	struct grx500_gpio *gpio = gpiochip_get_data(chip);
	unsigned int reg = grx500_gpio_get_direction(chip, pin) ==
		GPIO_LINE_DIRECTION_OUT ? GPIO_OUT : GPIO_IN;

	return !!(__raw_readl(gpio->port + reg) & BIT(pin));
}

static int grx500_gpio_setup_bbspi(struct device *dev)
{
	struct device_node *node = dev->of_node;
	struct regmap *rcu;
	u32 enable;
	int ret;

	if (!of_property_present(node, "lantiq,bbspi-en"))
		return 0;
	if (of_property_count_u32_elems(node, "lantiq,bbspi-en") != 1)
		return dev_err_probe(dev, -EINVAL,
				     "lantiq,bbspi-en requires one value, 0 or 1\n");
	ret = of_property_read_u32(node, "lantiq,bbspi-en", &enable);
	if (ret || enable > 1)
		return dev_err_probe(dev, ret ?: -EINVAL,
				     "lantiq,bbspi-en must be 0 or 1\n");
	rcu = syscon_regmap_lookup_by_phandle(node, "lantiq,rcu");
	if (IS_ERR(rcu))
		return dev_err_probe(dev, PTR_ERR(rcu), "RCU syscon unavailable\n");
	/* Apply the board's top-level pad routing before any GPIO is requested.
	 * The shared native-endian map also owns PCIe fields in this register.
	 */
	ret = regmap_update_bits(rcu, RCU_IFMUX_CFG, RCU_BBSPI_ENABLE,
				 enable ? RCU_BBSPI_ENABLE : 0);
	if (ret)
		return dev_err_probe(dev, ret, "BBSPI pad routing setup failed\n");
	return 0;
}

static int grx500_gpio_probe(struct platform_device *pdev)
{
	struct grx500_gpio *gpio;
	struct gpio_chip *chip;
	int ret;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;
	spin_lock_init(&gpio->lock);
	gpio->port = devm_platform_ioremap_resource_byname(pdev, "gpio");
	if (IS_ERR(gpio->port))
		return PTR_ERR(gpio->port);
	gpio->pad = devm_platform_ioremap_resource_byname(pdev, "pad");
	if (IS_ERR(gpio->pad))
		return PTR_ERR(gpio->pad);
	ret = grx500_gpio_setup_bbspi(&pdev->dev);
	if (ret)
		return ret;
	chip = &gpio->chip;
	chip->label = dev_name(&pdev->dev);
	chip->parent = &pdev->dev;
	chip->owner = THIS_MODULE;
	chip->base = -1;
	chip->ngpio = 32;
	chip->request = grx500_gpio_request;
	chip->get_direction = grx500_gpio_get_direction;
	chip->direction_input = grx500_gpio_direction_input;
	chip->direction_output = grx500_gpio_direction_output;
	chip->get = grx500_gpio_get;
	chip->set = grx500_gpio_set;
	chip->set_config = grx500_gpio_set_config;
	return devm_gpiochip_add_data(&pdev->dev, chip, gpio);
}

static const struct of_device_id grx500_gpio_match[] = {
	{ .compatible = "lantiq,grx500-gpio" },
	{ }
};
MODULE_DEVICE_TABLE(of, grx500_gpio_match);

static struct platform_driver grx500_gpio_driver = {
	.probe = grx500_gpio_probe,
	.driver = {
		.name = "grx500-gpio",
		.of_match_table = grx500_gpio_match,
	},
};
module_platform_driver(grx500_gpio_driver);
MODULE_LICENSE("GPL");
