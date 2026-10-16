#ifdef CONFIG_APP_LOGGING_SHELL

#include <zephyr/shell/shell.h>

/* Handlers live in logging_shell_bridge.cpp; the command table stays in C so the
 * shell macros need no C++ accommodations. */
int app_logging_shell_start(const struct shell *sh);
int app_logging_shell_stop(const struct shell *sh);
int app_logging_shell_status(const struct shell *sh);
int app_logging_shell_config(const struct shell *sh, size_t argc, char **argv);
int app_logging_shell_reapply_sensors(const struct shell *sh);
int app_logging_shell_unmount_sd(const struct shell *sh);

static int cmd_logging_start(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return app_logging_shell_start(sh);
}

static int cmd_logging_stop(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return app_logging_shell_stop(sh);
}

static int cmd_logging_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return app_logging_shell_status(sh);
}

static int cmd_logging_config(const struct shell *sh, size_t argc, char **argv)
{
	return app_logging_shell_config(sh, argc, argv);
}

static int cmd_logging_reapply_sensors(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return app_logging_shell_reapply_sensors(sh);
}

static int cmd_logging_unmount_sd(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return app_logging_shell_unmount_sd(sh);
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_logging,
	SHELL_CMD(start, NULL, "Request a logging start (a new file), as the CDMP command does.", cmd_logging_start),
	SHELL_CMD(stop, NULL, "Request a logging stop (the file is finalized).", cmd_logging_stop),
	SHELL_CMD(status, NULL, "Whether logging is active, the log writer's state and counters.", cmd_logging_status),
	SHELL_CMD_ARG(config, NULL,
		"Store the logging interval and size limit, keeping the sensor entries; "
		"prints the previous values. Logging stops on every unit.\n"
		"Usage: logging config <interval_ms> <max_log_size_mb>",
		cmd_logging_config, 3, 0),
	SHELL_CMD(reapply-sensors, NULL,
		"Apply the stored sensors configuration again, as an SMP apply does: "
		"processing restarts and logging stops on every unit.",
		cmd_logging_reapply_sensors),
	SHELL_CMD(unmount-sd, NULL, "Bench fault: unmount the SD card under the writer; reboot to recover.",
		  cmd_logging_unmount_sd),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(logging, &sub_logging, "Log writer bench commands", NULL);

#endif /* CONFIG_APP_LOGGING_SHELL */
