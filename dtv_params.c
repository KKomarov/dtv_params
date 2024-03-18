#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/firmware.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/version.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/platform_device.h>
#include <linux/timer.h>

#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt "\n"

static struct timer_list s_gpio_check_timer;
static int s_gpio_check_timer_inited = 0;
static struct pinctrl *s_pinctrl = NULL, *s_pinctrl_ao = NULL;

typedef struct
{
    char *name;
    int mode;
    int pin;
    int enabled;
    int value;
} PinState;

// mode 0-output, 1-input
PinState pin_map[] = {
    {
        .name = "ant_overload_1",
        .mode = 1,
    },
    {
        .name = "ant_power_1",
        .mode = 0,
    },
    {
        .name = "ant_overload_2",
        .mode = 1,
    },
    {
        .name = "ant_power_2",
        .mode = 0,
    },
    {
        .name = "demod_reset_1",
        .mode = 0,
        .value = 1,
    },
    {
        .name = "demod_reset_2",
        .mode = 0,
        .value = 1,
    },
    {
        .name = "tuner_power_enable_1",
        .mode = 0,
    },
    {
        .name = "tuner_power_enable_2",
        .mode = 0,
    },
    {
        .name = "user_defined_1",
        .mode = 0,
    },
    {
        .name = "user_defined_2",
        .mode = 0,
    },
    {
        .name = "power_led",
        .mode = 0,
    },
    {
        .name = "standy_led",
        .mode = 0,
    },
};
#define DEF_PIN(i_, name_) static PinState* name_ = &pin_map[i_];
DEF_PIN(0, ant_overload_1_pin);
DEF_PIN(1, ant_power_1_pin);
DEF_PIN(2, ant_overload_2_pin);
DEF_PIN(3, ant_power_2_pin);
DEF_PIN(4, demod_reset_1_pin);
DEF_PIN(5, demod_reset_2_pin);
DEF_PIN(6, tuner_power_enable_1_pin);
DEF_PIN(7, tuner_power_enable_2_pin);
DEF_PIN(8, user_defined_1_pin);
DEF_PIN(9, user_defined_2_pin);
DEF_PIN(10, power_led_pin);
DEF_PIN(11, standby_led_pin);

void update_value(PinState *pin_state)
{
    gpiod_set_raw_value(gpio_to_desc(pin_state->pin), pin_state->value);
}

void set_value(PinState *pin_state, int val)
{
    pin_state->value = val;
    gpiod_set_raw_value(gpio_to_desc(pin_state->pin), val);
}

int read_value(PinState *pin_state)
{
    int val = gpiod_get_raw_value(gpio_to_desc(pin_state->pin));
    pin_state->value = val;
    return val;
}

void ant_overload_check(int ant_num)
{
    PinState *ant_sense = &pin_map[ant_num * 2];
    PinState *ant_power = &pin_map[ant_num * 2 + 1];
    if (!(ant_sense->enabled && ant_power->enabled))
        return;

    int ant_overload_val = read_value(ant_sense);
    if (!ant_overload_val)
    {
        // overload detected -> switch off ant_power
        ant_sense->value = 1;
        set_value(ant_power, 0);
        return;
    }

    static last_power_val[2];
    if (last_power_val[i] != ant_power->value)
    {
        update_value(ant_power);
        last_power_val[i] = ant_power->value;
    }
    return;
}

void gpio_check_timer_sr(unsigned long arg)
{
    ant_overload_check(0);
    ant_overload_check(1);
    return mod_timer(&s_gpio_check_timer, jiffies + 125);
}

static ssize_t read_from_pin(PinState *pin_state, struct class_attribute *attr, char *buf)
{
    if (pin_state->enabled)
    {
        return sprintf(buf, "%d\n", read_value(pin_state));
    }
    return sprintf(buf, "error : %s not configured...", attr->attr.name);
}

static ssize_t save_to_pin(PinState *pin_state, struct class_attribute *attr, const char *buf, size_t size)
{
    if (!pin_state->enabled)
        return size;
    int val = *buf == '0' ? 0 : 1;
    set_value(pin_state, val);
    return size;
}

static ssize_t save_to_pin_or_var(PinState *pin_state, struct class_attribute *attr, const char *buf, size_t size)
{
    if (!pin_state->enabled)
        return size;
    int val = *buf == '0' ? 0 : 1;
    if (s_gpio_check_timer_inited == 1)
        pin_state->value = val;
    else
        set_value(pin_state, val);
    return size;
}

static ssize_t read_from_var(PinState *pin_state, struct class_attribute *attr, char *buf)
{
    if (pin_state->enabled)
    {
        return sprintf(buf, "%d\n", pin_state->value);
    }
    return sprintf(buf, "error : %s not configured...", attr->attr.name);
}

static ssize_t save_to_var(PinState *pin_state, struct class_attribute *attr, const char *buf, size_t size)
{
    if (ant_overload_1_enabled)
        s_hasAntOverload = (int)(*buf - '0');
    return size;
}

#define STORE_HEADER(_name, _handler)                                                                             \
    static ssize_t _name##_store(struct class *class, struct class_attribute *attr, const char *buf, size_t size) \
    {                                                                                                             \
        return _handler(_name, attr, buf, size);                                                                  \
    }

#define SHOW_HEADER(_name, _handler)                                                          \
    static ssize_t _name##_show(struct class *class, struct class_attribute *attr, char *buf) \
    {                                                                                         \
        return _handler(_name, attr, buf);                                                    \
    }

#define OUTPUT_PIN(_name)             \
    SHOW_HEADER(_name, read_from_pin) \
    STORE_HEADER(_name, save_to_pin)

#define INPUT_PIN(_name)              \
    SHOW_HEADER(_name, read_from_var) \
    STORE_HEADER(_name, save_to_var)

#define POWER_PIN(_name)              \
    SHOW_HEADER(_name, read_from_pin) \
    STORE_HEADER(_name, save_to_pin_or_var)

INPUT_PIN(ant_overload_1_pin);
POWER_PIN(ant_power_1_pin);
INPUT_PIN(ant_overload_2_pin);
POWER_PIN(ant_power_2_pin);
OUTPUT_PIN(demod_reset_1_pin);
OUTPUT_PIN(demod_reset_2_pin);
OUTPUT_PIN(tuner_power_enable_1_pin);
OUTPUT_PIN(tuner_power_enable_2_pin);
OUTPUT_PIN(user_defined_1_pin);
OUTPUT_PIN(user_defined_2_pin);
OUTPUT_PIN(power_led_pin);
OUTPUT_PIN(standby_led_pin);

#define __ATTR_664(_name) __ATTR(_name, (S_IRUGO | S_IWUSR | S_IWGRP), _name##_show, _name##_store)

static struct class_attribute dtv_params_class_attrs[] = {
    __ATTR_664(ant_overload_1_pin),
    __ATTR_664(ant_power_1_pin),
    __ATTR_664(ant_overload_2_pin),
    __ATTR_664(ant_power_2_pin),
    __ATTR_664(demod_reset_1_pin),
    __ATTR_664(demod_reset_2_pin),
    __ATTR_664(tuner_power_enable_1_pin),
    __ATTR_664(tuner_power_enable_2_pin),
    __ATTR_664(user_defined_1_pin),
    __ATTR_664(user_defined_2_pin),
    __ATTR_664(power_led_pin),
    __ATTR_664(standby_led_pin),
    __ATTR_NULL,
};

static struct class dtv_params_class = {
    .name = "dtv-params",
    .class_attrs = dtv_params_class_attrs,
};

struct pinctrl * init_gpio(struct device *dev, const char *name)
{
    struct pinctrl * p_handle = devm_pinctrl_get(dev);
    struct pinctrl * p_res = p_handle;
    if (p_handle <= 0xFFFFF000)
    {
        struct pinctrl_state * state_handle = pinctrl_lookup_state(p_handle, name);
        struct pinctrl_state * prev_state_handle = p_res;
        if (state_handle > 0xFFFFF000)
        {
            p_res = state_handle;
            devm_pinctrl_put(prev_state_handle);
        }
        else
        {
            auto sel_state = pinctrl_select_state(p_res);
            if (sel_state < 0)
            {
                auto tmp = p_res;
                p_res = sel_state;
                devm_pinctrl_put(v30);
            }
        }
    }
    return p_res;
}

static int dtv_params_probe(struct platform_device *pdev)
{
    pr_info("probe dtv params driver : start");
    auto of_node = pdev->dev.of_node;
    if (!of_node)
    {
        pr_err("no dt entry, exiting...");
        return 0;
    }

    for (int i = 0; i < sizeof(pin_map); ++i)
    {
        PinState *cur = &pin_map[i];
        char name[32];
        int flags;
        snprintf(name, sizeof(name), "%s-gpio", cur->name);
        cur->pin = of_get_named_gpio_flags(of_node, name, 0, &flags);
        if (cur->pin < 0x200)
        {
            cur->enabled = 1;
            if (cur->mode)
                gpiod_direction_output_raw(gpio_to_desc(cur->pin), cur->value);
            else
                gpiod_direction_input(gpio_to_desc(cur->pin));
        }
    }

    s_pinctrl = init_gpio(&pdev->dev, "default");
    pr_info("set pinctrl : %p", s_pinctrl);
    s_pinctrl_ao = init_gpio(&pdev->dev, "default_ao");
    pr_info("set pinctrl_ao : %p", s_pinctrl);

    if (class_register(&dtv_params_class) < 0)
    {
        pr_err("register class error");
        return 0;
    }

    for (int i = 0; i < 2; ++i)
    {
        PinState *ant_sense = &pin_map[i * 2];
        PinState *ant_power = &pin_map[i * 2 + 1];
        if (ant_sense->enabled && ant_power->enabled)
            s_gpio_check_timer_inited = 1;
    }

    if (s_gpio_check_timer_inited)
    {
        init_timer(&s_gpio_check_timer);
        s_gpio_check_timer.function = gpio_check_timer_sr;
        s_gpio_check_timer.data = 0;
        mod_timer(&s_gpio_check_timer, jiffies + 125);
    }
    pr_info("probe dtv params driver : end");
    return 0;
}

static int dtv_params_remove(struct platform_device *pdev)
{
    if (s_gpio_check_timer_inited == 1)
    {
        s_gpio_check_timer_inited = 0;
        del_timer(&s_gpio_check_timer);
    }
    if (s_pinctrl)
    {
        devm_pinctrl_put(s_pinctrl);
        s_pinctrl = NULL;
    }
    if (s_pinctrl_ao)
    {
        devm_pinctrl_put(s_pinctrl_ao);
        s_pinctrl_ao = NULL;
    }
    return 0;
}

static int dtv_shutdown(struct platform_device *pdev)
{
    return 0;
}

static int dtv_suspend(struct platform_device *pdev, pm_message_t state)
{
    return 0;
}

static int dtv_resume(struct platform_device *pdev)
{
    return 0;
}

static const struct of_device_id dtv_params_dt_match[] = {
    {
        .compatible = "dtv-params",
    },
    {},
};

static struct platform_driver dtv_params_driver = {
    .probe = dtv_params_probe,
    .remove = dtv_params_remove,
    .shutdown = dtv_shutdown,
    .suspend = dtv_suspend,
    .resume = dtv_resume,
    .driver = {
        .name = "dtv-params",
        .of_match_table = dtv_params_dt_match,
    }};

int dtv_params_init(void)
{
    pr_info("init");
    return platform_driver_register(&dtv_params_driver);
}

void dtv_params_exit(void)
{
    pr_info("exit");
    platform_driver_unregister(&dtv_params_driver);
}

module_init(dtv_params_init);
module_exit(dtv_params_exit);
MODULE_AUTHOR("KKomarov");
MODULE_DESCRIPTION("DTV params driver");
MODULE_LICENSE("GPL");
