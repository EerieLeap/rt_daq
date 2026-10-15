#ifdef CONFIG_APP_SENSOR_SHELL

#include <zephyr/shell/shell.h>

/* Handlers live in sensor_shell_bridge.cpp; the command table stays in C so the shell macros need no
 * C++ accommodations. */
int app_sensor_shell_set(const struct shell *sh, size_t argc, char **argv);
int app_sensor_shell_get(const struct shell *sh, size_t argc, char **argv);

static int cmd_sensor_set(const struct shell *sh, size_t argc, char **argv)
{
	return app_sensor_shell_set(sh, argc, argv);
}

static int cmd_sensor_get(const struct shell *sh, size_t argc, char **argv)
{
	return app_sensor_shell_get(sh, argc, argv);
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_sensor,
	SHELL_CMD_ARG(set, NULL,
		"Commit a value to a sensor, as its source would: triggers, the log and the CAN messages "
		"see it. Only while sensor processing runs.\n"
		"Usage: sensor set <id> <value>",
		cmd_sensor_set, 3, 0),
	SHELL_CMD_ARG(get, NULL,
		"Print a sensor's latest reading.\n"
		"Usage: sensor get <id>",
		cmd_sensor_get, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(sensor, &sub_sensor, "Sensor bench commands", NULL);

#endif /* CONFIG_APP_SENSOR_SHELL */
