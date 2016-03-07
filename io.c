#include "io_config.h"
#include "io_shared.h"
#include "io_gpio.h"
#include "io_aux.h"
#include "io.h"
#include "config.h"

io_info_t io_info =
{
	{
		/* io_id_gpio = 0 */
		0x00,
		0,
		16,
		{
			.input_digital = 1,
			.counter = 1,
			.output_digital = 1,
			.input_analog = 0,
			.output_analog = 1,
			.i2c = 1,
			.pullup = 1,
		},
		"Internal GPIO",
		io_gpio_init,
		io_gpio_periodic,
		io_gpio_init_pin_mode,
		io_gpio_get_pin_info,
		io_gpio_read_pin,
		io_gpio_write_pin,
	},
	{
		/* io_id_aux = 1 */
		0x01,
		0,
		2,
		{
			.input_digital = 1,
			.counter = 0,
			.output_digital = 1,
			.input_analog = 1,
			.output_analog = 0,
			.i2c = 0,
			.pullup = 0,
		},
		"Auxilliary GPIO (RTC+ADC)",
		io_aux_init,
		io_aux_periodic,
		io_aux_init_pin_mode,
		io_aux_get_pin_info,
		io_aux_read_pin,
		io_aux_write_pin,
	}
};

static io_data_t io_data;

typedef struct
{
	io_pin_mode_t	mode;
	const char		*name;
} io_mode_trait_t;

static io_mode_trait_t io_mode_traits[io_pin_size] =
{
	{ io_pin_disabled,			"disabled"			},
	{ io_pin_input_digital,		"inputd"			},
	{ io_pin_counter,			"counter"			},
	{ io_pin_output_digital,	"outputd"			},
	{ io_pin_timer,				"timer"				},
	{ io_pin_input_analog,		"inputa"			},
	{ io_pin_output_analog,		"outputa"			},
	{ io_pin_i2c,				"i2c"				},
};

irom static io_pin_mode_t io_mode_from_string(const string_t *src)
{
	io_pin_mode_t mode;
	const io_mode_trait_t *entry;

	for(mode = io_pin_disabled; mode < io_pin_size; mode++)
	{
		entry = &io_mode_traits[mode];

		if(string_match(src, entry->name))
			return(entry->mode);
	}

	return(io_pin_error);
}

irom static void io_string_from_mode(string_t *name, io_pin_mode_t mode)
{
	if(mode >= io_pin_error)
		string_cat(name, "error");
	else
		string_format(name, "%s", io_mode_traits[mode].name);
}

irom static io_i2c_t io_i2c_pin_from_string(const string_t *pin)
{
	if(string_match(pin, "sda"))
		return(io_i2c_sda);
	else if(string_match(pin, "scl"))
		return(io_i2c_scl);
	else
		return(io_i2c_error);
}

irom static void io_string_from_i2c_type(string_t *name, io_i2c_t type)
{
	switch(type)
	{
		case(io_i2c_sda): { string_cat(name, "sda"); break; }
		case(io_i2c_scl): { string_cat(name, "scl"); break; }
		default: { string_cat(name, "error"); break; }
	}
}

irom static bool pin_flag_from_string(const string_t *flag, io_config_pin_entry_t *pin_config, int value)
{
	if(string_match(flag, "autostart"))
		pin_config->flags.autostart = value;
	else if(string_match(flag, "repeat"))
		pin_config->flags.repeat = value;
	else if(string_match(flag, "pullup"))
		pin_config->flags.pullup = value;
	else if(string_match(flag, "reset-on-read"))
		pin_config->flags.reset_on_read = value;
	else
		return(false);

	return(true);
}

irom static void pin_string_from_flags(string_t *flags, const io_config_pin_entry_t *pin_config)
{
	bool none = true;

	if(pin_config->flags.autostart)
	{
		none = false;
		string_cat(flags, " autostart");
	}

	if(pin_config->flags.repeat)
	{
		none = false;
		string_cat(flags, " repeat");
	}

	if(pin_config->flags.pullup)
	{
		none = false;
		string_cat(flags, " pullup");
	}

	if(pin_config->flags.reset_on_read)
	{
		none = false;
		string_cat(flags, " reset-on-read");
	}

	if(none)
		string_cat(flags, " <none>");
}

irom static io_error_t io_read_pin_x(string_t *errormsg, const io_info_entry_t *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, int *value)
{
	io_error_t error;

	switch(pin_config->mode)
	{
		case(io_pin_disabled):
		case(io_pin_error):
		{
			if(errormsg)
				string_cat(errormsg, "cannot read from this pin");

			return(io_error);
		}

		case(io_pin_input_digital):
		case(io_pin_counter):
		case(io_pin_output_digital):
		case(io_pin_timer):
		case(io_pin_input_analog):
		case(io_pin_output_analog):
		case(io_pin_i2c):
		{
			if((error = info->read_pin_fn(errormsg, info, pin_data, pin_config, pin, value)) != io_ok)
				return(error);

			break;
		}
	}

	return(io_ok);
}

irom static io_error_t io_write_pin_x(string_t *errormsg, const io_info_entry_t *info, io_data_pin_entry_t *pin_data, io_config_pin_entry_t *pin_config, int pin, int value)
{
	io_error_t error;

	switch(pin_config->mode)
	{
		case(io_pin_disabled):
		case(io_pin_input_digital):
		case(io_pin_input_analog):
		case(io_pin_i2c):
		case(io_pin_error):
		{
			if(errormsg)
				string_cat(errormsg, "cannot write to this pin");

			return(io_error);
		}

		case(io_pin_counter):
		case(io_pin_output_digital):
		{
			if((error = info->write_pin_fn(errormsg, info, pin_data, pin_config, pin, value)) != io_ok)
				return(error);

			break;
		}

		case(io_pin_timer):
		{
			if(value)
			{
				value = pin_config->direction == io_dir_up ? 0 : 1;

				if((error = info->write_pin_fn(errormsg, info, pin_data, pin_config, pin, value)) != io_ok)
					return(error);

				pin_data->delay = pin_config->delay;
				pin_data->direction = pin_config->direction;
			}
			else
			{
				value = pin_config->direction == io_dir_up ? 1 : 0;

				if((error = info->write_pin_fn(errormsg, info, pin_data, pin_config, pin, value)) != io_ok)
					return(error);

				pin_data->delay = 0;
				pin_data->direction = io_dir_none;
			}

			break;
		}

		case(io_pin_output_analog):
		{
			if(value >= 0)
			{
				if((error = info->write_pin_fn((string_t *)0, info, pin_data, pin_config, pin, value)) != io_ok)
					return(error);

				pin_data->delay = 0;
				pin_data->direction = io_dir_none;
			}
			else
			{
				value = pin_config->shared.output_analog.lower_bound;

				if((error = info->write_pin_fn((string_t *)0, info, pin_data, pin_config, pin, value)) != io_ok)
					return(error);

				pin_data->delay = pin_config->delay;
				pin_data->direction = io_dir_up;
			}

			break;
		}
	}

	return(io_ok);
}

irom io_error_t io_read_pin(string_t *error_msg, int io, int pin, int *value)
{
	const io_info_entry_t *info;
	io_data_entry_t *data;
	io_config_pin_entry_t *pin_config;
	io_data_pin_entry_t *pin_data;
	io_error_t error;

	if(io >= io_id_size)
	{
		if(error_msg)
			string_cat(error_msg, "io out of range\n");
		return(io_error);
	}

	info = &io_info[io];
	data = &io_data[io];

	if(pin >= info->pins)
	{
		if(error_msg)
			string_cat(error_msg, "pin out of range\n");
		return(io_error);
	}

	pin_config = &config.io_config[io][pin];
	pin_data = &data->pin[pin];

	if(((error = io_read_pin_x(error_msg, info, pin_data, pin_config, pin, value)) != io_ok) && error_msg)
		string_cat(error_msg, "\n");
	else
		if((pin_config->mode == io_pin_counter) && (pin_config->flags.reset_on_read))
			error = io_write_pin_x(error_msg, info, pin_data, pin_config, pin, 0);

	return(error);
}

irom io_error_t io_write_pin(string_t *error, int io, int pin, int value)
{
	const io_info_entry_t *info;
	io_data_entry_t *data;
	io_config_pin_entry_t *pin_config;
	io_data_pin_entry_t *pin_data;

	if(io >= io_id_size)
	{
		if(error)
			string_cat(error, "io out of range\n");
		return(io_error);
	}

	info = &io_info[io];
	data = &io_data[io];

	if(pin >= info->pins)
	{
		if(error)
			string_cat(error, "pin out of range\n");
		return(io_error);
	}

	pin_config = &config.io_config[io][pin];
	pin_data = &data->pin[pin];

	return(io_write_pin_x(error, info, pin_data, pin_config, pin, value));
}

irom void io_init(void)
{
	const io_info_entry_t *info;
	io_data_entry_t *data;
	io_config_pin_entry_t *pin_config;
	io_data_pin_entry_t *pin_data;
	int io, pin;
	int i2c_sda = -1;
	int i2c_scl = -1;
	int i2c_delay = -1;

	for(io = 0; io < io_id_size; io++)
	{
		info = &io_info[io];
		data = &io_data[io];

		for(pin = 0; pin < info->pins; pin++)
		{
			pin_data = &data->pin[pin];
			pin_data->direction = io_dir_none;
			pin_data->delay = 0;
		}

		if(info->init_fn(info) == io_ok)
		{
			data->detected = true;

			for(pin = 0; pin < info->pins; pin++)
			{
				pin_config = &config.io_config[io][pin];
				pin_data = &data->pin[pin];

				if(info->init_pin_mode_fn((string_t *)0, info, pin_data, pin_config, pin) == io_ok)
				{
					switch(pin_config->mode)
					{
						case(io_pin_disabled):
						case(io_pin_input_digital):
						case(io_pin_counter):
						case(io_pin_input_analog):
						case(io_pin_error):
						{
							break;
						}

						case(io_pin_output_digital):
						case(io_pin_timer):
						{
							io_write_pin_x((string_t *)0, info, pin_data, pin_config, pin, pin_config->flags.autostart);
							break;
						}

						case(io_pin_output_analog):
						{
							if(pin_config->flags.autostart)
								io_write_pin_x((string_t *)0, info, pin_data, pin_config, pin, -1);
							else
								io_write_pin_x((string_t *)0, info, pin_data, pin_config, pin, pin_config->shared.output_analog.lower_bound);

							break;
						}

						case(io_pin_i2c):
						{
							if(pin_config->shared.i2c.pin_mode == io_i2c_sda)
								i2c_sda = pin;

							if(pin_config->shared.i2c.pin_mode == io_i2c_scl)
							{
								i2c_scl = pin;
								i2c_delay = pin_config->delay;
							}

							if((i2c_sda >= 0) && (i2c_scl >= 0) && (i2c_delay >= 0))
								i2c_init(i2c_sda, i2c_scl, i2c_delay);

							break;
						}
					}
				}
			}
		}
	}
}

irom void io_periodic(void)
{
	const io_info_entry_t *info;
	io_data_entry_t *data;
	io_config_pin_entry_t *pin_config;
	io_data_pin_entry_t *pin_data;
	int io, pin, status_io, status_pin, value;
	io_flags_t flags = { .counter_triggered = 0 };

	status_io = config.status_trigger_io.io;
	status_pin = config.status_trigger_io.pin;

	for(io = 0; io < io_id_size; io++)
	{
		info = &io_info[io];
		data = &io_data[io];

		if(!data->detected)
			continue;

		if(info->periodic_fn)
			info->periodic_fn(io, info, data, &flags);

		for(pin = 0; pin < info->pins; pin++)
		{
			pin_config = &config.io_config[io][pin];
			pin_data = &data->pin[pin];

			switch(pin_config->mode)
			{
				case(io_pin_disabled):
				case(io_pin_input_digital):
				case(io_pin_counter):
				case(io_pin_output_digital):
				case(io_pin_input_analog):
				case(io_pin_i2c):
				case(io_pin_error):
				{
					break;
				}

				case(io_pin_timer):
				{
					if((pin_data->direction != io_dir_none) && (pin_data->delay >= 10) && ((pin_data->delay -= 10) <= 0))
					{
						switch(pin_data->direction)
						{
							case(io_dir_none):
							case(io_dir_toggle):
							{
								break;
							}

							case(io_dir_up):
							{
								info->write_pin_fn((string_t *)0, info, pin_data, pin_config, pin, 1);
								pin_data->direction = io_dir_down;
								break;
							}

							case(io_dir_down):
							{
								info->write_pin_fn((string_t *)0, info, pin_data, pin_config, pin, 0);
								pin_data->direction = io_dir_up;
								break;
							}
						}

						if(pin_config->flags.repeat)
							pin_data->delay = pin_config->delay;
						else
						{
							pin_data->delay = 0;
							pin_data->direction = io_dir_none;
						}
					}

					break;
				}

				case(io_pin_output_analog):
				{
					if((pin_config->shared.output_analog.upper_bound > pin_config->shared.output_analog.lower_bound) &&
							(pin_config->delay > 0) && (pin_data->direction != io_dir_none))
					{
						if(info->read_pin_fn((string_t *)0, info, pin_data, pin_config, pin, &value) == io_ok)
						{
							if(pin_data->direction == io_dir_up) 
							{
								value *= (pin_config->delay / 10000.0) + 1;

								if(value >= pin_config->shared.output_analog.upper_bound)
								{
									value = pin_config->shared.output_analog.upper_bound;
									pin_data->direction = io_dir_down;
								}
							}
							else
							{
								value /= (pin_config->delay / 10000.0) + 1;

								if(value <= pin_config->shared.output_analog.lower_bound)
								{
									value = pin_config->shared.output_analog.lower_bound;

									if(pin_config->flags.repeat)
										pin_data->direction = io_dir_up;
									else
										pin_data->direction = io_dir_none;
								}
							}

							info->write_pin_fn((string_t *)0, info, pin_data, pin_config, pin, value);
						}
					}

					break;
				}
			}
		}
	}

	if((flags.counter_triggered) && (status_io >= 0) && (status_pin >= 0))
		io_write_pin((string_t *)0, status_io, status_pin, -1);
}

/* app commands */

irom app_action_t application_function_io_mode(const string_t *src, string_t *dst)
{
	const io_info_entry_t	*info;
	io_data_entry_t			*data;
	io_config_pin_entry_t	*pin_config;
	io_data_pin_entry_t		*pin_data;
	io_pin_mode_t			mode;
	int io, pin;

	if(parse_int(1, src, &io, 0) != parse_ok)
	{
		io_config_dump(dst, &config, -1, -1, false);
		return(app_action_normal);
	}

	if((io < 0) || (io >= io_id_size))
	{
		string_format(dst, "invalid io %d\n", io);
		return(app_action_error);
	}

	info = &io_info[io];
	data = &io_data[io];

	if(!data->detected)
	{
		string_format(dst, "io %d not detected\n", io);
		return(app_action_error);
	}

	if(parse_int(2, src, &pin, 0) != parse_ok)
	{
		io_config_dump(dst, &config, io, -1, false);
		return(app_action_normal);
	}

	if((pin < 0) || (pin >= info->pins))
	{
		string_cat(dst, "io pin out of range\n");
		return(app_action_error);
	}

	pin_config = &config.io_config[io][pin];
	pin_data = &data->pin[pin];

	if(parse_string(3, src, dst) != parse_ok)
	{
		string_clear(dst);
		io_config_dump(dst, &config, io, pin, false);
		return(app_action_normal);
	}

	if((mode = io_mode_from_string(dst)) == io_pin_error)
	{
		string_copy(dst, "invalid mode\n");
		return(app_action_error);
	}

	string_clear(dst);

	switch(mode)
	{
		case(io_pin_input_digital):
		{
			if(!info->caps.input_digital)
			{
				string_cat(dst, "digital input mode invalid for this io\n");
				return(app_action_error);
			}

			break;
		}

		case(io_pin_counter):
		{
			if(!info->caps.counter)
			{
				string_cat(dst, "counter mode invalid for this io\n");
				return(app_action_error);
			}

			int debounce;

			if((parse_int(4, src, &debounce, 0) != parse_ok))
			{
				string_cat(dst, "counter: <debounce ms>\n");
				return(app_action_error);
			}

			pin_config->delay = debounce;

			break;
		}

		case(io_pin_output_digital):
		{
			if(!info->caps.output_digital)
			{
				string_cat(dst, "digital output mode invalid for this io\n");
				return(app_action_error);
			}

			break;
		}

		case(io_pin_timer):
		{
			io_direction_t direction;
			int delay;

			if(!info->caps.output_digital)
			{
				string_cat(dst, "timer mode invalid for this io\n");
				return(app_action_error);
			}

			if(parse_string(4, src, dst) != parse_ok)
			{
				string_copy(dst, "timer: <direction>:up/down <delay>:ms\n");
				return(app_action_error);
			}

			if(string_match(dst, "up"))
				direction = io_dir_up;
			else if(string_match(dst, "down"))
				direction = io_dir_down;
			else
			{
				string_cat(dst, ": timer direction invalid\n");
				return(app_action_error);
			}

			string_clear(dst);

			if((parse_int(5, src, &delay, 0) != parse_ok))
			{
				string_copy(dst, "timer: <direction>:up/down <delay>:ms\n");
				return(app_action_error);
			}

			if(delay < 10)
			{
				string_cat(dst, "timer: delay too small: must be >= 10 ms\n");
				return(app_action_error);
			}

			pin_config->direction = direction;
			pin_config->delay = delay;

			break;
		}

		case(io_pin_input_analog):
		{
			if(!info->caps.input_analog)
			{
				string_cat(dst, "analog input mode invalid for this io\n");
				return(app_action_error);
			}

			break;
		}

		case(io_pin_output_analog):
		{
			int lower_bound = 0;
			int upper_bound = 0;
			int speed = 0;

			if(!info->caps.output_analog)
			{
				string_cat(dst, "analog output mode invalid for this io\n");
				return(app_action_error);
			}

			parse_int(4, src, &lower_bound, 0);
			parse_int(5, src, &upper_bound, 0);
			parse_int(6, src, &speed, 0);

			if((lower_bound < 0) || (lower_bound > 65535))
			{
				string_format(dst, "outputa: lower bound out of range: %d\n", lower_bound);
				return(app_action_error);
			}

			if(upper_bound == 0)
				upper_bound = lower_bound;

			if((upper_bound < 0) || (upper_bound > 65535))
			{
				string_format(dst, "outputa: upper bound out of range: %d\n", upper_bound);
				return(app_action_error);
			}

			if(upper_bound < lower_bound)
			{
				string_cat(dst, "upper bound below lower bound\n");
				return(app_action_error);
			}

			if((speed < 0) || (speed > 65535))
			{
				string_format(dst, "outputa: speed out of range: %d\n", speed);
				return(app_action_error);
			}

			pin_config->shared.output_analog.lower_bound = lower_bound;
			pin_config->shared.output_analog.upper_bound = upper_bound;
			pin_config->delay = speed;

			break;
		}

		case(io_pin_i2c):
		{
			int delay = 0;
			io_i2c_t pin_mode;

			if(!info->caps.i2c)
			{
				string_cat(dst, "i2c mode invalid for this io\n");
				return(app_action_error);
			}

			if(parse_string(4, src, dst) != parse_ok)
			{
				string_copy(dst, "i2c: <pin mode>=sda|scl\n");
				return(app_action_error);
			}

			if((pin_mode = io_i2c_pin_from_string(dst)) == io_i2c_error)
			{
				string_copy(dst, "i2c: <pin mode>=sda|scl\n");
				return(app_action_error);
			}

			string_clear(dst);

			if(pin_mode == io_i2c_scl)
			{
				if(parse_int(5, src, &delay, 0) != parse_ok)
				{
					string_copy(dst, "i2c: scl <delay>\n");
					return(app_action_error);
				}
			}

			pin_config->delay = delay;
			pin_config->shared.i2c.pin_mode = pin_mode;

			break;
		}

		case(io_pin_disabled):
		{
			break;
		}

		case(io_pin_error):
		{
			string_cat(dst, "unsupported io mode\n");
			return(app_action_error);
		}
	}

	pin_config->mode = mode;

	if(info->init_pin_mode_fn && (info->init_pin_mode_fn(dst, info, pin_data, pin_config, pin) != io_ok))
	{
		pin_config->mode = io_pin_disabled;
		return(app_action_error);
	}

	io_config_dump(dst, &config, io, pin, false);

	return(app_action_normal);
}

irom app_action_t application_function_io_read(const string_t *src, string_t *dst)
{
	const io_info_entry_t *info;
	io_config_pin_entry_t *pin_config;
	int io, pin, value;

	if(parse_int(1, src, &io, 0) != parse_ok)
	{
		string_cat(dst, "io-read: <io> <pin>\n");
		return(app_action_error);
	}

	if((io < 0) || (io >= io_id_size))
	{
		string_format(dst, "invalid io %d\n", io);
		return(app_action_error);
	}

	info = &io_info[io];

	if(parse_int(2, src, &pin, 0) != parse_ok)
	{
		string_cat(dst, "get: <io> <pin>\n");
		return(app_action_error);
	}

	if((pin < 0) || (pin >= info->pins))
	{
		string_cat(dst, "io pin out of range\n");
		return(app_action_error);
	}

	pin_config = &config.io_config[io][pin];

	io_string_from_mode(dst, pin_config->mode);

	if(pin_config->mode == io_pin_i2c)
	{
		string_cat(dst, "/");
		io_string_from_i2c_type(dst, pin_config->shared.i2c.pin_mode);
	}

	string_cat(dst, ": ");

	if(io_read_pin(dst, io, pin, &value) != io_ok)
		return(app_action_error);

	string_format(dst, "[%d]\n", value);

	return(app_action_normal);
}

irom app_action_t application_function_io_write(const string_t *src, string_t *dst)
{
	const io_info_entry_t *info;
	io_config_pin_entry_t *pin_config;
	int io, pin, value;

	if(parse_int(1, src, &io, 0) != parse_ok)
	{
		string_cat(dst, "io-write <io> <pin> <value>\n");
		return(app_action_error);
	}

	if((io < 0) || (io >= io_id_size))
	{
		string_format(dst, "invalid io %d\n", io);
		return(app_action_error);
	}

	info = &io_info[io];

	if(parse_int(2, src, &pin, 0) != parse_ok)
	{
		string_cat(dst, "io-write <io> <pin> <value>\n");
		return(app_action_error);
	}

	if((pin < 0) || (pin >= info->pins))
	{
		string_cat(dst, "invalid pin\n");
		return(app_action_error);
	}

	pin_config = &config.io_config[io][pin];

	value = 0;
	parse_int(3, src, &value, 0);

	io_string_from_mode(dst, pin_config->mode);
	string_cat(dst, ": ");

	if(io_write_pin(dst, io, pin, value) != io_ok)
	{
		string_cat(dst, "\n");
		return(app_action_error);
	}

	if(io_read_pin(dst, io, pin, &value) != io_ok)
	{
		string_cat(dst, "\n");
		return(app_action_error);
	}

	string_format(dst, "[%d]\n", value);

	return(app_action_normal);
}

irom static app_action_t application_function_io_clear_set_flag(const string_t *src, string_t *dst, int value)
{
	const io_info_entry_t *info;
	io_data_entry_t *data;
	io_data_pin_entry_t *pin_data;
	io_config_pin_entry_t *pin_config;
	int io, pin;
	io_pin_flag_t saved_flags;

	if(parse_int(1, src, &io, 0) != parse_ok)
	{
		string_cat(dst, "io-flag <io> <pin> <flag>\n");
		return(app_action_error);
	}

	if((io < 0) || (io >= io_id_size))
	{
		string_format(dst, "invalid io %d\n", io);
		return(app_action_error);
	}

	info = &io_info[io];
	data = &io_data[io];

	if(parse_int(2, src, &pin, 0) != parse_ok)
	{
		string_cat(dst, "io-flag <io> <pin> <flag>\n");
		return(app_action_error);
	}

	if((pin < 0) || (pin >= info->pins))
	{
		string_cat(dst, "invalid pin\n");
		return(app_action_error);
	}

	pin_data = &data->pin[pin];
	pin_config = &config.io_config[io][pin];

	saved_flags = pin_config->flags;

	if((parse_string(3, src, dst) == parse_ok) && !pin_flag_from_string(dst, pin_config, value))
	{
		string_copy(dst, "io-flag <io> <pin> <flag>\n");
		return(app_action_error);
	}

	if(pin_config->flags.pullup && !info->caps.pullup)
	{
		pin_config->flags = saved_flags;
		string_copy(dst, "io does not support pullup\n");
		return(app_action_error);
	}

	if(info->init_pin_mode_fn && (info->init_pin_mode_fn(dst, info, pin_data, pin_config, pin) != io_ok))
	{
		pin_config->flags = saved_flags;
		string_copy(dst, "cannot enable this flag\n");
		return(app_action_error);
	}

	string_clear(dst);
	string_format(dst, "flags for pin %d/%d:", io, pin);

	pin_string_from_flags(dst, pin_config);

	string_cat(dst, "\n");

	return(app_action_normal);
}

irom app_action_t application_function_io_set_flag(const string_t *src, string_t *dst)
{
	return(application_function_io_clear_set_flag(src, dst, 1));
}

irom app_action_t application_function_io_clear_flag(const string_t *src, string_t *dst)
{
	return(application_function_io_clear_set_flag(src, dst, 0));
}

/* dump */

typedef enum
{
	ds_id_io,
	ds_id_pin,
	ds_id_flags_1,
	ds_id_flags_2,
	ds_id_mode,
	ds_id_disabled,
	ds_id_input,
	ds_id_counter,
	ds_id_output,
	ds_id_timer,
	ds_id_analog_output,
	ds_id_i2c_sda,
	ds_id_i2c_scl,
	ds_id_unknown,
	ds_id_not_detected,
	ds_id_info_1,
	ds_id_info_2,
	ds_id_header,
	ds_id_footer,
	ds_id_preline,
	ds_id_postline,
	ds_id_error,

	ds_id_length,
	ds_id_invalid = ds_id_length
} dump_string_id_t;

typedef const char string_array_t[ds_id_length][256];

typedef struct {
	const string_array_t plain;
	const string_array_t html;
} dump_string_t;

static const roflash dump_string_t dump_strings =
{
	.plain =
	{
		"io[%d]: %s@%x",
		"  pin: %2d",
		" flags:",
		", ",
		"mode: ",
		"disabled",
		"input, state: %s",
		"counter, counter: %d, debounce: %d",
		"output, state: %s",
		"timer, config direction: %s, delay: %d ms, current direction: %s, delay: %d, state: %s",
		"analog output, min/static: %d, max: %d, speed: %d, current direction: %s, value: %d",
		"i2c/sda",
		"i2c/scl, delay: %d",
		"unknown",
		"  not found\n",
		", info: ",
		"",
		"",
		"",
		"",
		"\n",
		"error",
	},

	.html =
	{
		"<td>io[%d]</td><td>%s@%x</td>",
		"<td></td><td>%d</td>",
		"<td>",
		"</td>",
		"",
		"<td>disabled</td>",
		"<td>input</td><td>state: %s</td>",
		"<td>counter</td><td><td>counter: %d</td><td>debounce: %d</td>",
		"<td>output</td><td>state: %s</td>",
		"<td>timer</td><td>config direction: %s, delay: %d ms</td><<td>current direction %s, delay: %d, state: %s</td>",
		"<td>analog output</td><td>min/static: %d, max: %d, delay: %d, current direction: %s, value: %d",
		"<td>i2c</td><td>sda</td>",
		"<td>i2c</td><td>scl, delay: %d</td>",
		"<td>unknown</td>",
		"<td>not found</td>",
		"<td>",
		"</td>",
		"<table border=\"1\"><tr><th>index</th><th>name</th><th>mode</th><th colspan=\"8\"></th></tr>",
		"</table>\n",
		"<tr>",
		"</trd>\n",
		"<td>error</td>",
	}
};

irom void io_config_dump(string_t *dst, const config_t *cfg, int io_id, int pin_id, bool html)
{
	const io_info_entry_t *info;
	io_data_entry_t *data;
	io_data_pin_entry_t *pin_data;
	const io_config_pin_entry_t *pin_config;
	const string_array_t *strings;
	int io, pin, value;
	io_error_t error;

	if(html)
		strings = &dump_strings.html;
	else
		strings = &dump_strings.plain;

	string_cat_ptr(dst, (*strings)[ds_id_header]);

	for(io = 0; io < io_id_size; io++)
	{
		if((io_id >= 0) && (io_id != io))
			continue;

		info = &io_info[io];
		data = &io_data[io];

		string_cat_ptr(dst, (*strings)[ds_id_preline]);
		string_format_ptr(dst, (*strings)[ds_id_io], io, info->name, info->address);
		string_cat_ptr(dst, (*strings)[ds_id_postline]);

		if(!data->detected)
		{
			string_cat_ptr(dst, (*strings)[ds_id_preline]);
			string_cat_ptr(dst, (*strings)[ds_id_not_detected]);
			string_cat_ptr(dst, (*strings)[ds_id_postline]);
			continue;
		}

		for(pin = 0; pin < info->pins; pin++)
		{
			if((pin_id >= 0) && (pin_id != pin))
				continue;

			pin_config = &cfg->io_config[io][pin];
			pin_data = &data->pin[pin];

			string_cat_ptr(dst, (*strings)[ds_id_preline]);
			string_format_ptr(dst, (*strings)[ds_id_pin], pin);

			string_cat_ptr(dst, (*strings)[ds_id_flags_1]);
			pin_string_from_flags(dst, pin_config);
			string_cat_ptr(dst, (*strings)[ds_id_flags_2]);

			string_cat_ptr(dst, (*strings)[ds_id_mode]);

			if(pin_config->mode != io_pin_disabled)
				if((error = io_read_pin_x(dst, info, pin_data, pin_config, pin, &value)) != io_ok)
					string_cat(dst, "\n");
				else
					(void)0;
			else
				error = io_ok;

			switch(pin_config->mode)
			{
				case(io_pin_disabled):
				{
					string_cat_ptr(dst, (*strings)[ds_id_disabled]);

					break;
				}

				case(io_pin_input_digital):
				{
					if(error == io_ok)
						string_format_ptr(dst, (*strings)[ds_id_input], onoff(value));
					else
						string_cat_ptr(dst, (*strings)[ds_id_error]);

					break;
				}

				case(io_pin_counter):
				{
					if(error == io_ok)
						string_format_ptr(dst, (*strings)[ds_id_counter], value, pin_config->delay);
					else
						string_cat_ptr(dst, (*strings)[ds_id_error]);

					break;
				}

				case(io_pin_output_digital):
				{
					if(error == io_ok)
						string_format_ptr(dst, (*strings)[ds_id_output], onoff(value));
					else
						string_cat_ptr(dst, (*strings)[ds_id_error]);

					break;
				}

				case(io_pin_timer):
				{
					if(error == io_ok)
						string_format_ptr(dst, (*strings)[ds_id_timer],
								pin_config->direction == io_dir_up ? "up" : (pin_config->direction == io_dir_down ? "down" : "none"),
								pin_config->delay,
								pin_data->direction == io_dir_up ? "up" : (pin_data->direction == io_dir_down ? "down" : "none"),
								pin_data->delay,
								onoff(value));
					else
						string_cat_ptr(dst, (*strings)[ds_id_error]);

					break;
				}

				case(io_pin_output_analog):
				{
					if(error == io_ok)
						string_format_ptr(dst, (*strings)[ds_id_analog_output],
								pin_config->shared.output_analog.lower_bound,
								pin_config->shared.output_analog.upper_bound,
								pin_config->delay,
								pin_config->direction == io_dir_up ? "up" : (pin_config->direction == io_dir_down ? "down" : "none"),
								value);
					else
						string_cat_ptr(dst, (*strings)[ds_id_error]);

					break;
				}

				case(io_pin_i2c):
				{
					if(pin_config->shared.i2c.pin_mode == io_i2c_sda)
						string_cat_ptr(dst, (*strings)[ds_id_i2c_sda]);
					else
						string_format_ptr(dst, (*strings)[ds_id_i2c_scl], pin_config->delay);

					break;
				}

				default:
				{
					string_cat_ptr(dst, (*strings)[ds_id_unknown]);

					break;
				}
			}

			string_cat_ptr(dst, (*strings)[ds_id_info_1]);
			info->get_pin_info_fn(dst, info, pin_data, pin_config, pin);
			string_cat_ptr(dst, (*strings)[ds_id_info_2]);

			string_cat_ptr(dst, (*strings)[ds_id_postline]);
		}
	}

	string_cat_ptr(dst, (*strings)[ds_id_footer]);
}
