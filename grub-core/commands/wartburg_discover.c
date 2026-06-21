/* wartburg_discover.c - zero-config EFI OS discovery (rEFInd-inspired).
 *
 * Scans every FAT (EFI System) partition's \EFI tree for OS boot loaders and
 * synthesizes `chainloader' menu entries, auto-classed so the WartBURG /
 * gfxmenu OS-detection icon engine pictures them. The heuristics are modelled
 * on rEFInd's scanner (full per-vendor directory scan; a non-loader denylist
 * like dont_scan_files; fallback \EFI\BOOT dedup; one primary loader per OS dir,
 * preferring shim -- the Secure Boot entry -- over grub) but written fresh.
 *
 * Entries are added with grub_normal_add_menu_entry (the API grub.cfg uses); no
 * GRUB core file is patched, so WartBURG stays fully removable. GPLv3+ (wartburg.c).
 */

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/env.h>
#include <grub/device.h>
#include <grub/disk.h>
#include <grub/fs.h>
#include <grub/file.h>
#include <grub/menu.h>
#include <grub/normal.h>
#include <grub/efi/pe32.h>
#include <grub/wartburg_widget.h>

/* Basename substrings (lower-cased) that are NOT OS loaders: shim helpers, MOK
   manager, the EFI shell, fallback helper, memory testers. rEFInd excludes the
   same families via its default dont_scan_files. */
static const char *wb_nonloader[] =
  {
    "mokmanager", "mmx64", "mmia32", "mmaa64",
    "fbx64", "fbia32", "fbaa64", "hashtool",
    "shell", "memtest", "drv", "fwupd", 0
  };

/* Loader basenames in descending priority. shim before grub: shim is the
   Secure Boot-signed entry that chainloads grub, so chainloading it is the
   SB-correct path. bootmgfw.efi covers Windows (nested under Microsoft\Boot). */
static const char *wb_primary[] =
  {
    "bootmgfw.efi",
    "shimx64.efi", "shimaa64.efi", "shimia32.efi",
    "grubx64.efi", "grubaa64.efi", "grubia32.efi",
    "systemd-bootx64.efi", "systemd-bootaa64.efi",
    0
  };

/* vendor dir (lower-cased) -> pretty title + icon class. */
static const struct
{
  const char *vendor;
  const char *title;
  const char *class;
} wb_vendor[] =
  {
    { "microsoft", "Windows Boot Manager", "windows" },
    { "ubuntu",    "Ubuntu",     "ubuntu" },
    { "pop",       "Pop!_OS",    "ubuntu" },
    { "fedora",    "Fedora",     "fedora" },
    { "debian",    "Debian",     "debian" },
    { "opensuse",  "openSUSE",   "opensuse" },
    { "manjaro",   "Manjaro",    "manjaro" },
    { "arch",      "Arch Linux", "arch" },
    { "endeavouros","EndeavourOS","endeavouros" },
    { "zorin",     "Zorin OS",   "zorin" },
    { "kali",      "Kali Linux", "kali" },
    { "systemd",   "systemd-boot","uefi" },
    { 0, 0, 0 }
  };

struct wb_name
{
  struct wb_name *next;
  int dir;
  char name[0];
};

struct wb_scan_ctx
{
  const char *root;	/* booted device, to skip our own loader */
  const char *skip;	/* $wartburg_discover_skip: comma list of substrings */
  int count;
};

static int
collect_hook (const char *filename, const struct grub_dirhook_info *info,
	      void *data)
{
  struct wb_name **head = data;
  struct wb_name *e;
  grub_size_t n;

  if (grub_strcmp (filename, ".") == 0 || grub_strcmp (filename, "..") == 0)
    return 0;
  n = grub_strlen (filename);
  e = grub_malloc (sizeof (*e) + n + 1);
  if (! e)
    return 0;
  e->dir = info->dir;
  grub_memcpy (e->name, filename, n + 1);
  e->next = *head;
  *head = e;
  return 0;
}

static struct wb_name *
wb_listdir (grub_fs_t fs, grub_device_t dev, const char *path)
{
  struct wb_name *head = 0;

  (fs->fs_dir) (dev, path, collect_hook, &head);
  grub_errno = GRUB_ERR_NONE;
  return head;
}

static void
wb_freelist (struct wb_name *p)
{
  while (p)
    {
      struct wb_name *n = p->next;
      grub_free (p);
      p = n;
    }
}

/* Lower-cased copy (caller frees). */
static char *
wb_lower (const char *s)
{
  char *o = grub_strdup (s);
  char *p;
  if (o)
    for (p = o; *p; p++)
      *p = grub_tolower ((grub_uint8_t) *p);
  return o;
}

static int
wb_is_nonloader (const char *base)
{
  char *lo = wb_lower (base);
  int bad = 0;
  unsigned i;

  if (! lo)
    return 0;
  for (i = 0; wb_nonloader[i]; i++)
    if (grub_strstr (lo, wb_nonloader[i]))
      {
	bad = 1;
	break;
      }
  grub_free (lo);
  return bad;
}

static int
wb_has_efi_suffix (const char *base)
{
  grub_size_t n = grub_strlen (base);
  return n > 4 && grub_strcasecmp (base + n - 4, ".efi") == 0;
}

/* Choose the best loader basename from a listed directory, or 0. */
static char *
wb_pick_primary (struct wb_name *files)
{
  struct wb_name *f;
  unsigned i;

  for (i = 0; wb_primary[i]; i++)
    for (f = files; f; f = f->next)
      if (! f->dir && grub_strcasecmp (f->name, wb_primary[i]) == 0)
	return grub_strdup (f->name);

  /* No known primary: first plausible *.efi that isn't a helper. */
  for (f = files; f; f = f->next)
    if (! f->dir && wb_has_efi_suffix (f->name) && ! wb_is_nonloader (f->name))
      return grub_strdup (f->name);
  return 0;
}

static struct wb_name *
wb_find_subdir (struct wb_name *list, const char *name)
{
  for (; list; list = list->next)
    if (list->dir && grub_strcasecmp (list->name, name) == 0)
      return list;
  return 0;
}

/* Both *title and *class come back grub_malloc'd; the caller frees them. */
static void
wb_classify (const char *vendor, const char *label, char **title, char **class)
{
  char *lo = wb_lower (vendor);
  unsigned i;

  *title = 0;
  *class = 0;
  if (lo)
    for (i = 0; wb_vendor[i].vendor; i++)
      if (grub_strcmp (lo, wb_vendor[i].vendor) == 0)
	{
	  *title = grub_strdup (wb_vendor[i].title);
	  *class = grub_strdup (wb_vendor[i].class);
	  break;
	}
  if (! *title)
    {
      /* Unknown vendor: title from volume label if any, else the dir name;
	 class is the lower-cased vendor (icon engine falls back gracefully). */
      *title = grub_strdup ((label && *label) ? label : vendor);
      *class = grub_strdup (lo ? lo : "linux");
    }
  grub_free (lo);
}

/* Already a menu entry (e.g. a hand-written grub.cfg stanza) chainloading this
   exact target? Then don't duplicate it. */
static int
wb_already_listed (const char *target)
{
  grub_menu_t menu = grub_env_get_menu ();
  grub_menu_entry_t e;

  if (! menu)
    return 0;
  for (e = menu->entry_list; e; e = e->next)
    if (e->sourcecode && grub_strstr (e->sourcecode, target))
      return 1;
  return 0;
}

static void
wb_add (struct wb_scan_ctx *ctx, const char *dev, const char *path,
	const char *title, const char *class)
{
  char *target, *src, *id;
  const char *args[2];
  char *classes[2];

  target = grub_xasprintf ("(%s)%s", dev, path);
  if (! target)
    return;
  if (wb_already_listed (target))
    {
      grub_free (target);
      return;
    }

  src = grub_xasprintf ("insmod chain\nchainloader %s\n", target);
  id = grub_xasprintf ("wartburg_efi_%d", ctx->count);
  if (src && id)
    {
      args[0] = title;
      args[1] = 0;
      classes[0] = (char *) class;
      classes[1] = 0;
      grub_normal_add_menu_entry (1, args, classes, id, 0, 0, 0, src, 0, 0);
      grub_errno = GRUB_ERR_NONE;
      grub_dprintf ("wartburg", "discover: %s -> %s [%s]\n", target, title,
		    class);
      ctx->count++;
    }
  grub_free (target);
  grub_free (src);
  grub_free (id);
}

static int
wb_excluded (struct wb_scan_ctx *ctx, const char *path)
{
  const char *p, *s;

  if (! ctx->skip || ! *ctx->skip)
    return 0;
  /* comma-separated case-sensitive substrings */
  for (p = ctx->skip; *p;)
    {
      char buf[128];
      grub_size_t n = 0;
      s = p;
      while (*p && *p != ',')
	p++;
      n = p - s;
      if (n && n < sizeof (buf))
	{
	  grub_memcpy (buf, s, n);
	  buf[n] = 0;
	  if (grub_strstr (path, buf))
	    return 1;
	}
      if (*p == ',')
	p++;
    }
  return 0;
}

/* Scan one vendor dir under /EFI (descending one level into a "Boot" subdir for
   Windows). Returns 1 if a loader entry was added. */
static int
wb_scan_vendor (struct wb_scan_ctx *ctx, const char *dev, grub_fs_t fs,
		grub_device_t gdev, const char *vendor, const char *label)
{
  char *dirpath, *base = 0, *loaderpath = 0, *title = 0, *class = 0;
  struct wb_name *files;
  int added = 0;

  dirpath = grub_xasprintf ("/EFI/%s", vendor);
  if (! dirpath)
    return 0;
  files = wb_listdir (fs, gdev, dirpath);

  base = wb_pick_primary (files);
  if (base)
    loaderpath = grub_xasprintf ("%s/%s", dirpath, base);
  else if (wb_find_subdir (files, "Boot"))
    {
      /* Windows: \EFI\Microsoft\Boot\bootmgfw.efi */
      char *sub = grub_xasprintf ("%s/Boot", dirpath);
      struct wb_name *bf = sub ? wb_listdir (fs, gdev, sub) : 0;
      base = wb_pick_primary (bf);
      if (base)
	loaderpath = grub_xasprintf ("%s/Boot/%s", dirpath, base);
      wb_freelist (bf);
      grub_free (sub);
    }

  if (loaderpath && ! wb_excluded (ctx, loaderpath))
    {
      wb_classify (vendor, label, &title, &class);
      if (title && class)
	{
	  wb_add (ctx, dev, loaderpath, title, class);
	  added = 1;
	}
      grub_free (title);
      grub_free (class);
    }

  grub_free (loaderpath);
  grub_free (base);
  grub_free (dirpath);
  wb_freelist (files);
  return added;
}

/* Icon class from a loader filename (e.g. a UKI "fedora-6.9.efi" -> "fedora");
   returns a static string (vendor-map class or `deflt`), never to be freed. */
static const char *
wb_class_from_name (const char *name, const char *deflt)
{
  char *lo = wb_lower (name);
  const char *c = deflt;
  unsigned i;

  if (lo)
    for (i = 0; wb_vendor[i].vendor; i++)
      if (grub_strstr (lo, wb_vendor[i].vendor))
	{
	  c = wb_vendor[i].class;
	  break;
	}
  grub_free (lo);
  return c;
}

/* If `path' is a Unified Kernel Image, return PRETTY_NAME from its embedded
   .osrel (os-release) PE section, else 0. UKIs are PE32+ images with .linux/
   .initrd/.osrel sections (systemd ukify layout). Bounded + failure-tolerant:
   any malformed/short read just yields 0 and the caller falls back to the
   filename. */
static char *
wb_uki_title (const char *dev, const char *path)
{
  char *full, *title = 0;
  grub_file_t f;
  struct grub_msdos_image_header dos;
  struct grub_pe32_coff_header coff;
  char sig[4];
  grub_uint32_t sectab;
  unsigned i;

  full = grub_xasprintf ("(%s)%s", dev, path);
  if (! full)
    return 0;
  f = grub_file_open (full, GRUB_FILE_TYPE_NONE);
  grub_free (full);
  if (! f)
    {
      grub_errno = GRUB_ERR_NONE;
      return 0;
    }

  if (grub_file_read (f, &dos, sizeof (dos)) != (grub_ssize_t) sizeof (dos)
      || dos.msdos_magic != GRUB_PE32_MAGIC)
    goto done;
  grub_file_seek (f, dos.pe_image_header_offset);
  if (grub_file_read (f, sig, 4) != 4
      || sig[0] != 'P' || sig[1] != 'E' || sig[2] || sig[3])
    goto done;
  if (grub_file_read (f, &coff, sizeof (coff)) != (grub_ssize_t) sizeof (coff))
    goto done;

  sectab = dos.pe_image_header_offset + 4 + sizeof (coff)
	   + coff.optional_header_size;
  for (i = 0; i < coff.num_sections; i++)
    {
      struct grub_pe32_section_table sec;
      grub_uint32_t sz;
      char *buf, *p;

      grub_file_seek (f, sectab + (grub_uint64_t) i * sizeof (sec));
      if (grub_file_read (f, &sec, sizeof (sec)) != (grub_ssize_t) sizeof (sec))
	break;
      if (grub_memcmp (sec.name, ".osrel\0", 7) != 0)
	continue;

      sz = sec.raw_data_size;
      if (sz == 0)
	break;
      if (sz > 8192)
	sz = 8192;
      buf = grub_malloc (sz + 1);
      if (! buf)
	break;
      grub_file_seek (f, sec.raw_data_offset);
      if (grub_file_read (f, buf, sz) == (grub_ssize_t) sz)
	{
	  buf[sz] = '\0';
	  for (p = buf; p; )
	    {
	      if (grub_strncmp (p, "PRETTY_NAME=", 12) == 0)
		{
		  char *v = p + 12, *e;
		  grub_size_t n;
		  if (*v == '"')
		    {
		      v++;
		      for (e = v; *e && *e != '"'; e++)
			;
		    }
		  else
		    for (e = v; *e && *e != '\n' && *e != '\r'; e++)
		      ;
		  n = e - v;
		  title = grub_malloc (n + 1);
		  if (title)
		    {
		      grub_memcpy (title, v, n);
		      title[n] = '\0';
		    }
		  break;
		}
	      p = grub_strchr (p, '\n');
	      if (p)
		p++;
	    }
	}
      grub_free (buf);
      break;
    }

 done:
  grub_file_close (f);
  grub_errno = GRUB_ERR_NONE;
  return title;
}

/* Scan a directory of standalone loader files, one entry PER file. Used for
   UKIs in \EFI\Linux (each *.efi is a self-contained, directly chainloadable
   kernel image) and for $wartburg_discover_dirs (rEFInd-style also_scan_dirs).
   Title = PRETTY_NAME from an embedded UKI .osrel if present, else filename
   minus ".efi"; class derived from the name, else `deflt`. */
static int
wb_scan_loader_dir (struct wb_scan_ctx *ctx, const char *dev, grub_fs_t fs,
		    grub_device_t gdev, const char *dirpath, const char *deflt)
{
  struct wb_name *files = wb_listdir (fs, gdev, dirpath);
  struct wb_name *f;
  int added = 0;

  for (f = files; f; f = f->next)
    {
      char *lp, *title;

      if (f->dir || ! wb_has_efi_suffix (f->name) || wb_is_nonloader (f->name))
	continue;
      lp = grub_xasprintf ("%s/%s", dirpath, f->name);
      if (! lp || wb_excluded (ctx, lp))
	{
	  grub_free (lp);
	  continue;
	}
      title = wb_uki_title (dev, lp);	/* PRETTY_NAME from a UKI, if any */
      if (! title)
	{
	  title = grub_strdup (f->name);	/* else filename minus ".efi" */
	  if (title)
	    {
	      grub_size_t n = grub_strlen (title);
	      if (n > 4)
		title[n - 4] = '\0';
	    }
	}
      if (title)
	{
	  wb_add (ctx, dev, lp, title, wb_class_from_name (f->name, deflt));
	  added++;
	}
      grub_free (title);
      grub_free (lp);
    }
  wb_freelist (files);
  return added;
}

static int
wb_scan_device (const char *name, void *data)
{
  struct wb_scan_ctx *ctx = data;
  grub_device_t dev;
  grub_fs_t fs;
  char *label = 0;
  struct wb_name *efi, *v;
  int real = 0;

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
  if (fs->fs_label)
    {
      (fs->fs_label) (dev, &label);
      grub_errno = GRUB_ERR_NONE;
    }

  efi = wb_listdir (fs, dev, "/EFI");
  /* Real vendor dirs first (skip BOOT; it's the fallback, handled after).
     \EFI\Linux is special: it holds standalone UKIs, one bootable image each. */
  for (v = efi; v; v = v->next)
    {
      if (! v->dir || grub_strcasecmp (v->name, "BOOT") == 0)
	continue;
      if (grub_strcasecmp (v->name, "Linux") == 0)
	real += wb_scan_loader_dir (ctx, name, fs, dev, "/EFI/Linux", "linux");
      else
	real += wb_scan_vendor (ctx, name, fs, dev, v->name, label);
    }

  /* rEFInd-style also_scan_dirs: extra absolute dirs of standalone loaders. */
  {
    const char *p = grub_env_get ("wartburg_discover_dirs");
    while (p && *p)
      {
	char buf[256];
	const char *s = p;
	grub_size_t n;
	while (*p && *p != ',')
	  p++;
	n = p - s;
	if (n && n < sizeof (buf))
	  {
	    grub_memcpy (buf, s, n);
	    buf[n] = '\0';
	    real += wb_scan_loader_dir (ctx, name, fs, dev, buf, "uefi");
	  }
	if (*p == ',')
	  p++;
      }
  }

  /* Fallback \EFI\BOOT\BOOT*.EFI: list only if nothing else booted from this
     volume, and never our own loader on the booted device. */
  if (! real && ! (ctx->root && grub_strcmp (name, ctx->root) == 0))
    {
      struct wb_name *bootdir = wb_find_subdir (efi, "BOOT");
      if (bootdir)
	{
	  struct wb_name *bf = wb_listdir (fs, dev, "/EFI/BOOT");
	  char *base = wb_pick_primary (bf);
	  if (base)
	    {
	      char *lp = grub_xasprintf ("/EFI/BOOT/%s", base);
	      if (lp && ! wb_excluded (ctx, lp))
		wb_add (ctx, name, lp,
			(label && *label) ? label : "UEFI Boot Loader", "uefi");
	      grub_free (lp);
	    }
	  grub_free (base);
	  wb_freelist (bf);
	}
    }

  wb_freelist (efi);
  grub_free (label);
  grub_device_close (dev);
  return 0;
}

/* Scan all FAT partitions, add a chainloader menu entry per discovered OS.
   Returns the number of entries added. */
int
grub_wartburg_discover (void)
{
  struct wb_scan_ctx ctx;

  ctx.root = grub_env_get ("root");
  ctx.skip = grub_env_get ("wartburg_discover_skip");
  ctx.count = 0;
  grub_device_iterate (wb_scan_device, &ctx);
  grub_errno = GRUB_ERR_NONE;
  grub_dprintf ("wartburg", "discover: %d EFI loader(s) added\n", ctx.count);
  return ctx.count;
}
