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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt "\n"

static timer_list s_gpio_check_timer;
static int s_gpio_check_timer_inited = 0, s_pinctrl = 0, s_pinctrl_ao = 0;

int demod_reset_1_pin;

int ant_overload_check(int ant_num)
{
    // какая тут логика. если на пине перегрузки 0, значит перегруз, вырубаем питание на антенну и не включаем пока кто нибудь не сбросит флаг через запись в файл
    // если перегруза нет, обновляем питание на антенне в соответсвии с переменной
    if (!*(&demod_reset_1_gpio + ant_num + 26)) // ant_overload_1_enabled
        return ant_num;
    if (!*(&demod_reset_1_gpio + ant_num + 6)) // power_1_pin_enabled
        return ant_num;

    v1 = 4 * ant_num;
    v2 = &demod_reset_1_gpio + ant_num;
    if (v2[28]) // s_hasAntOverload
        return ant_num;
    v3 = ant_num;
    if (!v2[45]) // enableCheckAntOverload
        goto LABEL_6;
    auto v7 = gpio_to_desc(*(&demod_reset_1_gpio + ant_num + 24)); // ant_overload_1_gpio
    auto ant_overload_val = gpiod_get_raw_value(v7);
    if (!ant_overload_val)
    {
        v8 = *(&demod_reset_1_gpio + v3 + 4); // ant_power_1_gpio
        v9 = (char *)&dtv_params_driver + v1;
        *((_DWORD *)v9 + 23) = 0; // odd offset, should be 24 or 26 (if 0 then stored_val)
        *((_DWORD *)v9 + 25) = 0; // odd_offset2 (if 0 then ant_overload_2_gpio)
        v10 = gpio_to_desc(v8);
        ant_num = gpiod_set_raw_value(v10, 0);
        v2[45] = 0; // enableCheckAntOverload = 0
        v2[28] = 1; // s_hasAntOverload = 1
        return ant_num;
    }
    if (v2[28]) // s_hasAntOverload
        return ant_overload_val;

LABEL_6:
    v4 = (char *)&dtv_params_driver + v1;
    v5 = *(_DWORD *)((char *)&dtv_params_driver + v1 + 92);       // odd_offset 23 stored_val
    if (*(_DWORD *)((char *)&dtv_params_driver + v1 + 100) == v5) // odd_offset2 25 ant_overload_2_gpio
        return ant_num;
    *((_DWORD *)v4 + 25) = v5;                          // odd_offset2 = v5  // ant_overload_2_gpio
    v6 = gpio_to_desc(*(&demod_reset_1_gpio + v3 + 4)); // ant_power_1_gpio
    ant_num = gpiod_set_raw_value(v6, v5);
    *(int *)((char *)&demod_reset_1_gpio + v1 + 180) = *((_DWORD *)v4 + 25) != 0; // enableCheckAntOverload[] = odd_offset2[] != 0
    return ant_num;
}

unsigned long gpio_check_timer_sr()
{
    ant_overload_check(0);
    ant_overload_check(1);
    return mod_timer(s_gpio_check_timer, jiffies + 125);
}

static ssize_t stb_show_demod_reset_1_pin(struct class *class, struct class_attribute *attr, char *buf)
{
    if (demod_reset_1_enabled)
    {
        auto pin_desc = gpio_to_desc(demod_reset_1_gpio);
        auto raw_value = gpiod_get_raw_value(pin_desc);
        return sprintf(buf, "%d\n", raw_value);
    }
    return sprintf(buf, "error : demod reset 1 pin not config..\n");
}

static ssize_t stb_store_demod_reset_1_pin(struct class *class, struct class_attribute *attr, const char *buf, size_t size)
{
    if (!demod_reset_1_enabled)
        return size;
    int val = *buf == '0' ? 0 : 1;
    auto raw_val = gpio_to_desc(demod_reset_1_gpio);
    gpiod_set_raw_value(raw_val, val);
    return size;
}

static ssize_t stb_store_ant_power_1_pin(struct class *class, struct class_attribute *attr, const char *buf, size_t size)
{
    if (!power_1_pin_enabled)
        return size;
    if (*buf == '0')
    {
        if (s_gpio_check_timer_inited == 1)
        {
            s_antPowerOutput = 0;
        }
        else
        {
            v7 = gpio_to_desc(power_1_pin_gpio);
            gpiod_set_raw_value(v7, 0);
        }
        return size;
    }

    if (s_gpio_check_timer_inited != 1)
    {
        v5 = gpio_to_desc(power_1_pin_gpio);
        gpiod_set_raw_value(v5, 1);
        return size;
    }
    s_antPowerOutput = 1;
    return size;
}

static ssize_t stb_show_ant_overload_1_pin(struct class *class, struct class_attribute *attr, char *buf)
{
    if (ant_overload_1_enabled)
    {
        return sprintf(buf, "%d\n", s_hasAntOverload);
    }
    return sprintf(buf, "error : ant overload 1 pin not config..\n");
}

static ssize_t stb_store_ant_overload_1_pin(struct class *class, struct class_attribute *attr, const char *buf, size_t size)
{
    if (ant_overload_1_enabled)
        s_hasAntOverload = (int)(*buf - '0');
    return size;
}

static struct class_attribute dtv_params_class_attrs[] = {
    __ATTR(demod_reset_1_pin, S_IRUGO | S_IWUSR | S_IWGRP, stb_show_demod_reset_1_pin, stb_store_demod_reset_1_pin),

    __ATTR_NULL,
};

static struct class dtv_params_class = {
    .name = "dtv-params",
    .class_attrs = dtv_params_class_attrs,
};

auto init_gpio(struct device *dev, const char *name)
{
    auto p_handle = devm_pinctrl_get(p_dev);
    auto p_res = p_handle;
    if (p_handle <= 0xFFFFF000)
    {
        auto state_handle = pinctrl_lookup_state(p_handle, name);
        auto prev_state_handle = p_res;
        if (state_handle > 0xFFFFF000)
        {
            p_res = state_handle;
            devm_pinctrl_put(prev_state_handle);
        }
        else
        {
            auto v11 = pinctrl_select_state(p_res);
            if (v11 < 0)
            {
                auto tmp = p_res;
                p_res = v11;
                devm_pinctrl_put(v30);
            }
        }
    }
    return p_res;
}

struct PinState {
    char* name;
    int mode;
    int pin;
    int state;
};

PinState pin_map[] = {
    {
        .name="demod_reset_1",
        .mode=0,
    },
};

static int dtv_params_probe(struct platform_device *pdev)
{
    printk("dtv_params: probe dtv params driver : start\n");
    auto of_node = pdev->dev.of_node;
    if (!of_node)
    {
        pr_err("no dt entry, exiting...");
        return 0;
    }
    auto demod_reset_1_gpio = of_get_named_gpio_flags(of_node, "demod_reset_1-gpio", 0);
    auto ant_power_1_gpio = of_get_named_gpio_flags(of_node, "ant_power_1-gpio", 0);
    auto dword_C18124B0 = of_get_named_gpio_flags(of_node, "tuner_power_enable_1-gpio", 0);
    auto dword_C1812494 = of_get_named_gpio_flags(of_node, "demod_reset_2-gpio", 0);
    auto dword_C18124A4 = of_get_named_gpio_flags(of_node, "ant_power_2-gpio", 0);
    auto dword_C18124B4 = of_get_named_gpio_flags(of_node, "tuner_power_enable_2-gpio", 0);
    auto dword_C18124C0 = of_get_named_gpio_flags(of_node, "user_defined_1-gpio", 0);
    auto dword_C18124C4 = of_get_named_gpio_flags(of_node, "user_defined_2-gpio", 0);
    auto dword_C18124D0 = of_get_named_gpio_flags(of_node, "power_led-gpio", 0);
    auto dword_C18124E0 = of_get_named_gpio_flags(of_node, "standy_led-gpio", 0);
    auto ant_overload_1_gpio = of_get_named_gpio_flags(of_node, "ant_overload_1-gpio", 0);
    auto ant_overload_2_gpio = of_get_named_gpio_flags(of_node, "ant_overload_2-gpio", 0);
    if ((unsigned int)demod_reset_1_gpio < 0x200)
    {
        dword_C1812498 = 1;
        v19 = gpio_to_desc(demod_reset_1_gpio);
        gpiod_direction_output_raw(v19, 1);
    }
    if ((unsigned int)ant_power_1_gpio < 0x200)
    {
        ant_power_1_gpio_enabled = 1;
        v18 = gpio_to_desc(ant_power_1_gpio);
        gpiod_direction_output_raw(v18, 0);
    }
    if ((unsigned int)dword_C18124B0 < 0x200)
    {
        dword_C18124B8 = 1;
        v17 = gpio_to_desc(dword_C18124B0);
        gpiod_direction_output_raw(v17, 0);
    }
    if ((unsigned int)dword_C1812494 < 0x200)
    {
        dword_C181249C = 1;
        v21 = gpio_to_desc(dword_C1812494);
        gpiod_direction_output_raw(v21, 1);
    }
    if ((unsigned int)dword_C18124A4 < 0x200)
    {
        dword_C18124AC = 1;
        v20 = gpio_to_desc(dword_C18124A4);
        gpiod_direction_output_raw(v20, 0);
    }
    if ((unsigned int)dword_C18124B4 < 0x200)
    {
        dword_C18124BC = 1;
        v22 = gpio_to_desc(dword_C18124B4);
        gpiod_direction_output_raw(v22, 0);
    }
    if ((unsigned int)dword_C18124C0 < 0x200)
    {
        dword_C18124C8 = 1;
        if ((v33 & 1) != 0)
        {
            printk(&byte_C12B020C);
            v4 = gpio_to_desc(dword_C18124C0);
            gpiod_direction_input(v4);
        }
        else
        {
            printk(&byte_C12B0230);
            v28 = gpio_to_desc(dword_C18124C0);
            gpiod_direction_output_raw(v28, v33 & 1);
        }
    }
    if ((unsigned int)dword_C18124C4 < 0x200)
    {
        dword_C18124CC = 1;
        if ((v34 & 1) != 0)
        {
            printk(&byte_C12B0254);
            v5 = gpio_to_desc(dword_C18124C4);
            gpiod_direction_input(v5);
        }
        else
        {
            printk(&byte_C12B0278);
            v27 = gpio_to_desc(dword_C18124C4);
            gpiod_direction_output_raw(v27, v34 & 1);
        }
    }
    if ((unsigned int)dword_C18124D0 < 0x200)
    {
        dword_C18124D8 = 1;
        v24 = gpio_to_desc(dword_C18124D0);
        gpiod_direction_output_raw(v24, (v31 & 1) == 0);
    }
    if ((unsigned int)dword_C18124E0 < 0x200)
    {
        dword_C18124E8 = 1;
        v23 = gpio_to_desc(dword_C18124E0);
        gpiod_direction_output_raw(v23, (v32 & 1) == 0);
    }
    if ((unsigned int)ant_overload_1_gpio < 0x200)
    {
        ant_overload_1_enabled = 1;
        v26 = gpio_to_desc(ant_overload_1_gpio);
        gpiod_direction_input(v26);
    }
    if ((unsigned int)ant_overload_2_gpio < 0x200)
    {
        ant_overload_2_enabled = 1;
        v25 = gpio_to_desc(ant_overload_2_gpio);
        gpiod_direction_input(v25);
    }

    s_pinctrl = init_gpio(&pdev->dev, "default");
    pr_info("set pinctrl : %p", s_pinctrl);
    s_pinctrl_ao = init_gpio(&pdev->dev, "default_ao");
    pr_info("set pinctrl_ao : %p", s_pinctrl);

    // TODO replace with macros
    if (_class_register(&dtv_params_class, &_key_33631) < 0)
    {
        printk("dtv_params: register class error\n");
        return 0;
    }

    if (!ant_overload_1_enabled || !power_1_pin_enabled)
    {
        if (!ant_overload_2_enabled || !power_2_pin_enabled)
            goto LABEL_2;
        s_gpio_check_timer_inited = 1;
        if (power_1_pin_enabled)
            s_antPowerOutput = 0;
        goto LABEL_47;
    }
    s_antPowerOutput = 0;
    s_gpio_check_timer_inited = 1;
    if (power_2_pin_enabled)
    LABEL_47:
        s_antPower2Output = 0;

    init_timer_key(&s_gpio_check_timer, 0, 0);
    s_gpio_check_timer.function = gpio_check_timer_sr;
    s_gpio_check_timer.data = 0;
    mod_timer(&s_gpio_check_timer, jiffies + 125);
LABEL_2:
    printk("dtv_params: probe dtv params driver : end\n");
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
        s_pinctrl = 0;
    }
    if (!s_pinctrl_ao)
        return 0;
    devm_pinctrl_put(s_pinctrl_ao);
    s_pinctrl_ao = 0;
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

int dtv_params_init()
{
    printk("dtv params init\n");
    return _platform_driver_register(&dtv_params_driver, 0);
}

int dtv_params_exit()
{
    printk("dtv params exit\n");
    return platform_driver_unregister(&dtv_params_driver);
}

module_init(dtv_params_init);
module_exit(dtv_params_exit);
MODULE_AUTHOR("KKomarov");
MODULE_DESCRIPTION("DTV params driver");
MODULE_LICENSE("GPL");
