/* wartburg_discover.c - zero-config EFI OS discovery (rEFInd-style).
 *
 * Scans every FAT (EFI System) partition for well-known OS boot loaders and
 * synthesizes `chainloader' menu entries, auto-classed so the WartBURG / gfxmenu
 * OS-detection icon engine pictures them. Entries are added to the live menu via
 * grub_normal_add_menu_entry (the same API grub.cfg uses); no GRUB core file is
 * patched, so WartBURG stays fully removable. GPLv3+ (see wartburg.c).
 */

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/env.h>
#include <grub/device.h>
#include <grub/disk.h>
#include <grub/fs.h>
#include <grub/file.h>
#include <grub/normal.h>
#include <grub/wartburg_widget.h>

/* Well-known EFI loaders, most-specific first. We add at most one entry per
   (device, class): the first matching path wins (shim before grub, etc.). */
static const struct wb_loader
{
  const char *path;
  const char *title;
  const char *class;
} wb_loaders[] =
  {
    { "/EFI/Microsoft/Boot/bootmgfw.efi", "Windows Boot Manager", "windows" },
    { "/EFI/ubuntu/shimx64.efi",          "Ubuntu",      "ubuntu" },
    { "/EFI/ubuntu/grubx64.efi",          "Ubuntu",      "ubuntu" },
    { "/EFI/pop/grubx64.efi",             "Pop!_OS",     "ubuntu" },
    { "/EFI/fedora/shimx64.efi",          "Fedora",      "fedora" },
    { "/EFI/fedora/grubx64.efi",          "Fedora",      "fedora" },
    { "/EFI/debian/grubx64.efi",          "Debian",      "debian" },
    { "/EFI/opensuse/grubx64.efi",        "openSUSE",    "opensuse" },
    { "/EFI/Manjaro/grubx64.efi",         "Manjaro",     "manjaro" },
    { "/EFI/arch/grubx64.efi",            "Arch Linux",  "arch" },
    { "/EFI/zorin/grubx64.efi",           "Zorin OS",    "zorin" },
    { "/EFI/systemd/systemd-bootx64.efi", "systemd-boot","uefi" },
    { "/EFI/BOOT/BOOTX64.EFI",            "UEFI Default","uefi" },
  };

#define WB_NLOADERS (sizeof (wb_loaders) / sizeof (wb_loaders[0]))

struct wb_scan_ctx
{
  const char *root;	/* booted device name, to skip our own loader */
  int count;
};

static int
wb_file_exists (const char *dev, const char *path)
{
  char *full;
  grub_file_t f;

  full = grub_xasprintf ("(%s)%s", dev, path);
  if (! full)
    return 0;
  f = grub_file_open (full, GRUB_FILE_TYPE_NONE);
  grub_free (full);
  if (f)
    {
      grub_file_close (f);
      return 1;
    }
  grub_errno = GRUB_ERR_NONE;
  return 0;
}

static void
wb_add_loader (const char *dev, const struct wb_loader *l, int *seq)
{
  char *src, *id;
  const char *args[2];
  char *classes[2];

  src = grub_xasprintf ("insmod chain\nchainloader (%s)%s\n", dev, l->path);
  id = grub_xasprintf ("wartburg_efi_%d", *seq);
  if (! src || ! id)
    {
      grub_free (src);
      grub_free (id);
      return;
    }

  args[0] = l->title;
  args[1] = 0;
  classes[0] = (char *) l->class;
  classes[1] = 0;

  grub_normal_add_menu_entry (1, args, classes, id, 0, 0, 0, src, 0, 0);
  grub_errno = GRUB_ERR_NONE;
  (*seq)++;

  grub_free (src);
  grub_free (id);
}

static int
wb_scan_device (const char *name, void *data)
{
  struct wb_scan_ctx *ctx = data;
  grub_device_t dev;
  grub_fs_t fs;
  unsigned i;
  int added[WB_NLOADERS];	/* which loader rows we added on this device */

  dev = grub_device_open (name);
  if (! dev)
    {
      grub_errno = GRUB_ERR_NONE;
      return 0;
    }
  fs = grub_fs_probe (dev);
  if (! fs || grub_strcmp (fs->name, "fat") != 0)
    {
      grub_errno = GRUB_ERR_NONE;
      grub_device_close (dev);
      return 0;
    }

  grub_memset (added, 0, sizeof (added));
  for (i = 0; i < WB_NLOADERS; i++)
    {
      unsigned j;
      int dup = 0;

      /* one entry per class per device (shim wins over grub, etc.) */
      for (j = 0; j < i; j++)
	if (added[j] && grub_strcmp (wb_loaders[j].class, wb_loaders[i].class) == 0)
	  {
	    dup = 1;
	    break;
	  }
      if (dup)
	continue;

      /* don't list ourselves: the default BOOT path on the booted device */
      if (ctx->root && grub_strcmp (name, ctx->root) == 0
	  && grub_strcmp (wb_loaders[i].path, "/EFI/BOOT/BOOTX64.EFI") == 0)
	continue;

      if (wb_file_exists (name, wb_loaders[i].path))
	{
	  wb_add_loader (name, &wb_loaders[i], &ctx->count);
	  added[i] = 1;
	  grub_dprintf ("wartburg", "discover: (%s)%s -> %s [%s]\n",
			name, wb_loaders[i].path, wb_loaders[i].title,
			wb_loaders[i].class);
	}
    }

  grub_device_close (dev);
  return 0;
}

/* Scan all FAT partitions, add a chainloader menu entry per discovered OS
   loader. Returns the number of entries added. */
int
grub_wartburg_discover (void)
{
  struct wb_scan_ctx ctx;

  ctx.root = grub_env_get ("root");
  ctx.count = 0;
  grub_device_iterate (wb_scan_device, &ctx);
  grub_errno = GRUB_ERR_NONE;
  grub_dprintf ("wartburg", "discover: %d EFI loader(s) added\n", ctx.count);
  return ctx.count;
}
