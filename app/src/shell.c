/**
 * @file   shell.cpp
 * @author Dennis Sitelew
 * @date   Aug. 02, 2024
 */

#if CONFIG_SHELL
#include <zephyr/shell/shell.h>

SHELL_SUBCMD_SET_CREATE(hei_sub_cmd, (hei));
SHELL_CMD_REGISTER(hei, &hei_sub_cmd, "Hassio E-Ink commands", NULL);

#endif // CONFIG_SHELL
