#include <grub/types.h>
#include <grub/misc.h>
#include <grub/command.h>
#include <grub/dl.h>

GRUB_MOD_LICENSE("GPLv3+");

static grub_command_t cmd;

static grub_err_t
grub_cmd_wartburg(grub_command_t command __attribute__ ((unused)),
                  int argc __attribute__ ((unused)),
                  char **argv __attribute__ ((unused)))
{
    grub_printf("WartBURG active.\n");
    return GRUB_ERR_NONE;
}

GRUB_MOD_INIT(wartburg)
{
    grub_printf("\n=== WartBURG Initialized ===\n");

    cmd = grub_register_command("wartburg",
                                grub_cmd_wartburg,
                                0,
                                "Activate WartBURG.");
}

GRUB_MOD_FINI(wartburg)
{
    grub_unregister_command(cmd);
}
