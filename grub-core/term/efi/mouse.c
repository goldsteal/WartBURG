/* mouse.c - EFI pointer (mouse / touch) navigation for the boot menu.
 *
 * Reads the EFI Absolute Pointer (touchscreens, tablets, qemu `usb-tablet`) and
 * Simple Pointer (PS/2-style mice) protocols and surfaces motion + clicks as
 * GRUB key codes through a `grub_term_input' device: drag/move -> arrow keys,
 * left-click / tap -> Enter, right-click -> Esc. Because it speaks via the
 * keyboard input path, ANY menu (WartBURG's renderer or stock gfxterm) becomes
 * pointer-navigable with no menu-code changes -- just `terminal_input --append
 * mouse'.
 *
 * Design follows a1ive's grub term/efi/mouse.c (GPLv3+) -- the idea of exposing
 * pointer events as keystrokes via a terminal input. Reworked for clean GRUB
 * 2.15 and extended to the Absolute Pointer protocol (touch / tablet). GPLv3+.
 */

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/term.h>
#include <grub/dl.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>

GRUB_MOD_LICENSE ("GPLv3+");

/* ---- EFI Simple Pointer Protocol (relative; mice) ---- */
struct grub_efi_simple_pointer_mode
{
  grub_efi_uint64_t resolution_x;
  grub_efi_uint64_t resolution_y;
  grub_efi_uint64_t resolution_z;
  grub_efi_boolean_t left_button;
  grub_efi_boolean_t right_button;
};
struct grub_efi_simple_pointer_state
{
  grub_efi_int32_t relative_movement_x;
  grub_efi_int32_t relative_movement_y;
  grub_efi_int32_t relative_movement_z;
  grub_efi_boolean_t left_button;
  grub_efi_boolean_t right_button;
};
struct grub_efi_simple_pointer_protocol
{
  grub_efi_status_t (__grub_efi_api *reset) (
      struct grub_efi_simple_pointer_protocol *this,
      grub_efi_boolean_t extended_verification);
  grub_efi_status_t (__grub_efi_api *get_state) (
      struct grub_efi_simple_pointer_protocol *this,
      struct grub_efi_simple_pointer_state *state);
  grub_efi_event_t wait_for_input;
  struct grub_efi_simple_pointer_mode *mode;
};

/* ---- EFI Absolute Pointer Protocol (absolute; touch/tablet) ---- */
struct grub_efi_absolute_pointer_mode
{
  grub_efi_uint64_t absolute_min_x, absolute_min_y, absolute_min_z;
  grub_efi_uint64_t absolute_max_x, absolute_max_y, absolute_max_z;
  grub_efi_uint32_t attributes;
};
struct grub_efi_absolute_pointer_state
{
  grub_efi_uint64_t current_x, current_y, current_z;
  grub_efi_uint32_t active_buttons;	/* bit0 = touch/left active */
};
struct grub_efi_absolute_pointer_protocol
{
  grub_efi_status_t (__grub_efi_api *reset) (
      struct grub_efi_absolute_pointer_protocol *this,
      grub_efi_boolean_t extended_verification);
  grub_efi_status_t (__grub_efi_api *get_state) (
      struct grub_efi_absolute_pointer_protocol *this,
      struct grub_efi_absolute_pointer_state *state);
  grub_efi_event_t wait_for_input;
  struct grub_efi_absolute_pointer_mode *mode;
};

static grub_guid_t simple_guid = GRUB_EFI_SIMPLE_POINTER_PROTOCOL_GUID;
static grub_guid_t absolute_guid = GRUB_EFI_ABSOLUTE_POINTER_PROTOCOL_GUID;

static struct grub_efi_simple_pointer_protocol *sp;
static struct grub_efi_absolute_pointer_protocol *ap;

/* edge/accumulator state so one gesture maps to one discrete key */
static grub_efi_uint64_t ap_last_x, ap_last_y;
static int ap_have_last, ap_btn_prev;
static int sp_btn_prev, sp_rbtn_prev;
static grub_efi_int32_t sp_acc_x, sp_acc_y;

#define SP_STEP 40	/* relative units per arrow step (simple pointer) */

static void *
mouse_open_first (grub_guid_t *guid)
{
  grub_efi_handle_t *handles;
  grub_efi_uintn_t num = 0;
  void *proto = 0;

  handles = grub_efi_locate_handle (GRUB_EFI_BY_PROTOCOL, guid, 0, &num);
  if (handles && num)
    proto = grub_efi_open_protocol (handles[0], guid,
				    GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);
  grub_free (handles);
  grub_errno = GRUB_ERR_NONE;
  return proto;
}

static grub_err_t
mouse_init (struct grub_term_input *term __attribute__ ((unused)))
{
  ap = mouse_open_first (&absolute_guid);
  if (ap && ap->reset)
    ap->reset (ap, 1);
  sp = mouse_open_first (&simple_guid);
  if (sp && sp->reset)
    sp->reset (sp, 1);
  ap_have_last = ap_btn_prev = sp_btn_prev = sp_rbtn_prev = 0;
  sp_acc_x = sp_acc_y = 0;
  grub_dprintf ("mouse", "init: absolute=%d simple=%d\n", ap ? 1 : 0, sp ? 1 : 0);
  return GRUB_ERR_NONE;
}

static int
emit (int k)
{
  grub_dprintf ("mouse", "emit 0x%x\n", k);
  return k;
}

static int
mouse_getkey (struct grub_term_input *term __attribute__ ((unused)))
{
  if (ap)
    {
      struct grub_efi_absolute_pointer_state st;
      if (ap->get_state (ap, &st) == GRUB_EFI_SUCCESS)
	{
	  int btn = (st.active_buttons & 1) ? 1 : 0;
	  if (btn && ! ap_btn_prev)
	    {
	      ap_btn_prev = 1;
	      return emit ('\r');			/* tap / click -> select */
	    }
	  if (! btn)
	    ap_btn_prev = 0;
	  if (! ap_have_last)
	    {
	      ap_last_x = st.current_x;
	      ap_last_y = st.current_y;
	      ap_have_last = 1;
	    }
	  else
	    {
	      grub_efi_int64_t dx = (grub_efi_int64_t) st.current_x - ap_last_x;
	      grub_efi_int64_t dy = (grub_efi_int64_t) st.current_y - ap_last_y;
	      grub_efi_int64_t sx = (ap->mode->absolute_max_x
				     - ap->mode->absolute_min_x) / 8 + 1;
	      grub_efi_int64_t sy = (ap->mode->absolute_max_y
				     - ap->mode->absolute_min_y) / 8 + 1;
	      grub_efi_int64_t adx = dx < 0 ? -dx : dx;
	      grub_efi_int64_t ady = dy < 0 ? -dy : dy;
	      if (adx >= sx || ady >= sy)
		{
		  ap_last_x = st.current_x;
		  ap_last_y = st.current_y;
		  if (adx >= ady)
		    return emit (dx > 0 ? GRUB_TERM_KEY_RIGHT : GRUB_TERM_KEY_LEFT);
		  return emit (dy > 0 ? GRUB_TERM_KEY_DOWN : GRUB_TERM_KEY_UP);
		}
	    }
	}
      else
	grub_errno = GRUB_ERR_NONE;
    }

  if (sp)
    {
      struct grub_efi_simple_pointer_state st;
      if (sp->get_state (sp, &st) == GRUB_EFI_SUCCESS)
	{
	  if (st.left_button && ! sp_btn_prev)
	    {
	      sp_btn_prev = 1;
	      return emit ('\r');
	    }
	  if (! st.left_button)
	    sp_btn_prev = 0;
	  if (st.right_button && ! sp_rbtn_prev)
	    {
	      sp_rbtn_prev = 1;
	      return emit (GRUB_TERM_ESC);
	    }
	  if (! st.right_button)
	    sp_rbtn_prev = 0;
	  sp_acc_x += st.relative_movement_x;
	  sp_acc_y += st.relative_movement_y;
	  if (sp_acc_x >= SP_STEP)  { sp_acc_x = 0; return emit (GRUB_TERM_KEY_RIGHT); }
	  if (sp_acc_x <= -SP_STEP) { sp_acc_x = 0; return emit (GRUB_TERM_KEY_LEFT); }
	  if (sp_acc_y >= SP_STEP)  { sp_acc_y = 0; return emit (GRUB_TERM_KEY_DOWN); }
	  if (sp_acc_y <= -SP_STEP) { sp_acc_y = 0; return emit (GRUB_TERM_KEY_UP); }
	}
      else
	grub_errno = GRUB_ERR_NONE;
    }

  return GRUB_TERM_NO_KEY;
}

static struct grub_term_input grub_mouse_input =
  {
    .name = "mouse",
    .init = mouse_init,
    .getkey = mouse_getkey
  };

GRUB_MOD_INIT (mouse)
{
  grub_term_register_input ("mouse", &grub_mouse_input);
}

GRUB_MOD_FINI (mouse)
{
  grub_term_unregister_input (&grub_mouse_input);
}
