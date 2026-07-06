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
 *
 * Three sources, in order: EFI Absolute Pointer, EFI Simple Pointer, and a raw
 * USB HID reader (EFI_USB_IO) for firmware that ships no pointer driver at all.
 * Stock OVMF is such firmware: its only Absolute/Simple Pointer handles are
 * ConSplitter's childless stubs (GetState == NOT_READY forever) while the
 * pointer device sits on an unbound USB_IO handle. The raw reader claims a HID
 * interface only when no firmware driver put a pointer protocol on its handle,
 * so it never races a real driver's transfers.
 */

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/env.h>
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

/* ---- EFI USB I/O Protocol (raw HID fallback) ----
 *
 * Some firmware -- notably stock OVMF -- ships NO USB pointer driver at all:
 * the only Absolute/Simple Pointer handles are ConSplitter's childless stubs,
 * whose GetState returns NOT_READY forever while the actual tablet/mouse sits
 * on an unbound EFI_USB_IO handle. In that case we read the HID interrupt
 * endpoint ourselves. Only the members we call get real signatures; the rest
 * are opaque pads to keep the vtable layout (UEFI spec 2.x, EFI_USB_IO).  */
struct grub_efi_usb_device_request
{
  grub_efi_uint8_t request_type;
  grub_efi_uint8_t request;
  grub_efi_uint16_t value;
  grub_efi_uint16_t index;
  grub_efi_uint16_t length;
} GRUB_PACKED;

struct grub_efi_usb_interface_descriptor
{
  grub_efi_uint8_t length;
  grub_efi_uint8_t descriptor_type;
  grub_efi_uint8_t interface_number;
  grub_efi_uint8_t alternate_setting;
  grub_efi_uint8_t num_endpoints;
  grub_efi_uint8_t interface_class;
  grub_efi_uint8_t interface_subclass;
  grub_efi_uint8_t interface_protocol;
  grub_efi_uint8_t interface_str;
} GRUB_PACKED;

struct grub_efi_usb_endpoint_descriptor
{
  grub_efi_uint8_t length;
  grub_efi_uint8_t descriptor_type;
  grub_efi_uint8_t endpoint_address;
  grub_efi_uint8_t attributes;
  grub_efi_uint16_t max_packet_size;
  grub_efi_uint8_t interval;
} GRUB_PACKED;

typedef grub_efi_status_t
(__grub_efi_api *grub_efi_usb_async_callback_t) (void *data,
						 grub_efi_uintn_t data_length,
						 void *context,
						 grub_efi_uint32_t status);

struct grub_efi_usb_io_protocol
{
  void *usb_control_transfer;
  void *usb_bulk_transfer;
  grub_efi_status_t (__grub_efi_api *usb_async_interrupt_transfer) (
      struct grub_efi_usb_io_protocol *this,
      grub_efi_uint8_t device_endpoint,
      grub_efi_boolean_t is_new_transfer,
      grub_efi_uintn_t polling_interval,
      grub_efi_uintn_t data_length,
      grub_efi_usb_async_callback_t interrupt_callback,
      void *context);
  grub_efi_status_t (__grub_efi_api *usb_sync_interrupt_transfer) (
      struct grub_efi_usb_io_protocol *this,
      grub_efi_uint8_t device_endpoint,
      void *data,
      grub_efi_uintn_t *data_length,
      grub_efi_uintn_t timeout,
      grub_efi_uint32_t *usb_status);
  void *usb_isochronous_transfer;
  void *usb_async_isochronous_transfer;
  grub_efi_status_t (__grub_efi_api *usb_get_device_descriptor) (
      struct grub_efi_usb_io_protocol *this, void *descriptor);
  grub_efi_status_t (__grub_efi_api *usb_get_config_descriptor) (
      struct grub_efi_usb_io_protocol *this, void *descriptor);
  grub_efi_status_t (__grub_efi_api *usb_get_interface_descriptor) (
      struct grub_efi_usb_io_protocol *this,
      struct grub_efi_usb_interface_descriptor *descriptor);
  grub_efi_status_t (__grub_efi_api *usb_get_endpoint_descriptor) (
      struct grub_efi_usb_io_protocol *this,
      grub_efi_uint8_t endpoint_index,
      struct grub_efi_usb_endpoint_descriptor *descriptor);
  void *usb_get_string_descriptor;
  void *usb_get_supported_languages;
  void *usb_port_reset;
};

#define USB_CLASS_HID		3
#define USB_HID_PROTO_KEYBOARD	1
#define USB_ENDP_INTERRUPT	3

static grub_guid_t simple_guid = GRUB_EFI_SIMPLE_POINTER_PROTOCOL_GUID;
static grub_guid_t absolute_guid = GRUB_EFI_ABSOLUTE_POINTER_PROTOCOL_GUID;
static grub_guid_t usb_io_guid = GRUB_EFI_USB_IO_PROTOCOL_GUID;

static struct grub_efi_simple_pointer_protocol *sp;
static struct grub_efi_absolute_pointer_protocol *ap;

/* raw-HID fallback state */
static struct grub_efi_usb_io_protocol *uio;
static grub_efi_uint8_t uio_ep;		/* interrupt IN endpoint address */
static grub_efi_uintn_t uio_pkt;	/* endpoint max packet size */
static grub_efi_uint64_t u_last_x, u_last_y;
static int u_have_last, u_btn_prev, u_rbtn_prev;
static grub_efi_int32_t u_acc_x, u_acc_y;

/* HID reports land here from the firmware's async-transfer callback (runs at
   raised TPL, i.e. may interrupt getkey); single producer / single consumer,
   so plain volatile indices suffice.  */
#define URING_SZ  16	/* power of two */
#define URPT_MAX  8
static volatile grub_efi_uint8_t uring[URING_SZ][URPT_MAX];
static volatile grub_efi_uint8_t uring_len[URING_SZ];
static volatile unsigned uring_wr, uring_rd;

static grub_efi_status_t __grub_efi_api
mouse_usb_callback (void *data, grub_efi_uintn_t data_length,
		    void *context __attribute__ ((unused)),
		    grub_efi_uint32_t status)
{
  unsigned wr = uring_wr;
  grub_efi_uintn_t i;

  if (status != 0 || ! data || data_length < 3)
    return GRUB_EFI_SUCCESS;		/* NAK / error frame */
  if (wr - uring_rd >= URING_SZ)
    return GRUB_EFI_SUCCESS;		/* full: drop newest */
  if (data_length > URPT_MAX)
    data_length = URPT_MAX;
  for (i = 0; i < data_length; i++)
    uring[wr % URING_SZ][i] = ((grub_efi_uint8_t *) data)[i];
  uring_len[wr % URING_SZ] = data_length;
  uring_wr = wr + 1;
  return GRUB_EFI_SUCCESS;
}

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

/* Claim the first unbound USB HID pointer interface for raw reading. A handle
   is "unbound" when no firmware pointer driver installed Absolute/Simple
   Pointer on it -- if one did, we must not race its interrupt transfers and
   the protocol path above covers the device via ConSplitter anyway.  */
static void
mouse_find_raw_hid (void)
{
  grub_efi_handle_t *handles;
  grub_efi_uintn_t num = 0, i;

  uio = 0;
  handles = grub_efi_locate_handle (GRUB_EFI_BY_PROTOCOL, &usb_io_guid,
				    0, &num);
  for (i = 0; handles && i < num && ! uio; i++)
    {
      struct grub_efi_usb_io_protocol *io;
      struct grub_efi_usb_interface_descriptor ifd;
      grub_efi_uint8_t e;

      if (grub_efi_open_protocol (handles[i], &absolute_guid,
				  GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL)
	  || grub_efi_open_protocol (handles[i], &simple_guid,
				     GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL))
	continue;			/* a real pointer driver owns it */
      io = grub_efi_open_protocol (handles[i], &usb_io_guid,
				   GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);
      if (! io
	  || io->usb_get_interface_descriptor (io, &ifd) != GRUB_EFI_SUCCESS)
	continue;
      if (ifd.interface_class != USB_CLASS_HID
	  || ifd.interface_protocol == USB_HID_PROTO_KEYBOARD)
	continue;			/* not HID, or it's a keyboard */
      for (e = 0; e < ifd.num_endpoints; e++)
	{
	  struct grub_efi_usb_endpoint_descriptor epd;
	  if (io->usb_get_endpoint_descriptor (io, e, &epd) != GRUB_EFI_SUCCESS)
	    continue;
	  if ((epd.attributes & 0x3) == USB_ENDP_INTERRUPT
	      && (epd.endpoint_address & 0x80))
	    {
	      uio = io;
	      uio_ep = epd.endpoint_address;
	      uio_pkt = epd.max_packet_size;
	      if (uio_pkt > 64)
		uio_pkt = 64;
	      grub_dprintf ("mouse",
			    "raw hid: sub=%d proto=%d ep=0x%x pkt=%d\n",
			    ifd.interface_subclass, ifd.interface_protocol,
			    uio_ep, (int) uio_pkt);
	      break;
	    }
	}
    }
  grub_free (handles);
  grub_errno = GRUB_ERR_NONE;

  if (uio)
    {
      grub_efi_status_t st;
      uring_wr = uring_rd = 0;
      st = uio->usb_async_interrupt_transfer (uio, uio_ep, 1, 8, uio_pkt,
					      mouse_usb_callback, 0);
      if (st != GRUB_EFI_SUCCESS)
	{
	  grub_dprintf ("mouse", "raw hid: async start failed (%d)\n",
			(int) st);
	  uio = 0;
	}
    }
}

static void
mouse_stop_raw_hid (void)
{
  if (! uio)
    return;
  uio->usb_async_interrupt_transfer (uio, uio_ep, 0, 0, 0, 0, 0);
  uio = 0;
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
  mouse_stop_raw_hid ();		/* init may run again on terminal_input changes */
  mouse_find_raw_hid ();
  ap_have_last = ap_btn_prev = sp_btn_prev = sp_rbtn_prev = 0;
  sp_acc_x = sp_acc_y = 0;
  u_have_last = u_btn_prev = u_rbtn_prev = 0;
  u_acc_x = u_acc_y = 0;
  grub_dprintf ("mouse", "init: absolute=%d simple=%d rawhid=%d\n",
		ap ? 1 : 0, sp ? 1 : 0, uio ? 1 : 0);
  return GRUB_ERR_NONE;
}

static int
emit (int k)
{
  grub_dprintf ("mouse", "emit 0x%x\n", k);
  return k;
}

/* ---- pointer publishing (hover/click integration) ----
 *
 * Besides synthesizing menu keys, publish the raw pointer state through
 * environment variables so a graphical menu (WartBURG) can do real
 * hover/click hit-testing.  Env vars keep the modules decoupled -- no symbol
 * dependency, either side works alone:
 *   wb_ptr_x / wb_ptr_y  -- absolute position, 0..32767 on both axes
 *   wb_ptr_b             -- button bitmask (bit0 left, bit1 right)
 *   wb_ptr_seq           -- event counter (consumer change detection)
 * A consumer that takes over sets wb_ptr_own=1, which suppresses key
 * synthesis here (it reads the vars and moves its own selection instead);
 * without a consumer the legacy move->arrows behavior is unchanged.  */

#define WB_PTR_RANGE 32767

static unsigned ptr_seq;
/* virtual cursor for relative devices, kept in the same 0..32767 space */
static int v_x = WB_PTR_RANGE / 2, v_y = WB_PTR_RANGE / 2;

static int
owned (void)
{
  const char *s = grub_env_get ("wb_ptr_own");
  return s && s[0] == '1';
}

static void
publish (int x, int y, int btn, int rbtn)
{
  char buf[16];

  grub_snprintf (buf, sizeof (buf), "%d", x);
  grub_env_set ("wb_ptr_x", buf);
  grub_snprintf (buf, sizeof (buf), "%d", y);
  grub_env_set ("wb_ptr_y", buf);
  grub_snprintf (buf, sizeof (buf), "%d", btn | (rbtn << 1));
  grub_env_set ("wb_ptr_b", buf);
  grub_snprintf (buf, sizeof (buf), "%u", ++ptr_seq);
  grub_env_set ("wb_ptr_seq", buf);
  grub_errno = GRUB_ERR_NONE;
}

static void
publish_rel (int dx, int dy, int btn, int rbtn)
{
  v_x += dx * 32;
  v_y += dy * 32;
  if (v_x < 0) v_x = 0;
  if (v_x > WB_PTR_RANGE) v_x = WB_PTR_RANGE;
  if (v_y < 0) v_y = 0;
  if (v_y > WB_PTR_RANGE) v_y = WB_PTR_RANGE;
  publish (v_x, v_y, btn, rbtn);
}

static int
mouse_getkey (struct grub_term_input *term __attribute__ ((unused)))
{
  static unsigned polls;

  if (uio && (++polls & 0xff) == 0 && uring_rd == uring_wr)
    {
      /* Event-ring pump. The firmware's async-transfer monitor is timer-
	 driven, and firmware timer callbacks may never dispatch while the
	 boot loader spins in its own poll loop (observed on OVMF: the async
	 callback stays silent forever without this). A short synchronous
	 transfer processes the xHC event ring inline -- completing pending
	 async TDs and firing our callback -- and doubles as a direct read
	 when it returns data itself. Rate-limited so the block-on-NAK
	 timeout stays invisible next to human input.  */
      grub_efi_uint8_t buf[64];
      grub_efi_uintn_t len = uio_pkt;
      grub_efi_uint32_t ust = 0;
      if (uio->usb_sync_interrupt_transfer (uio, uio_ep, buf, &len, 30, &ust)
	  == GRUB_EFI_SUCCESS)
	mouse_usb_callback (buf, len, 0, 0);
    }

  if (ap)
    {
      struct grub_efi_absolute_pointer_state st;
      /* Spec-friendly nudge: poke the protocol's wait event so firmware that
	 only refreshes state on a CheckEvent updates it before GetState. Cheap
	 and non-blocking -- never wait, this is a getkey_noblock path.  */
      if (ap->wait_for_input)
	grub_efi_system_table->boot_services->check_event (ap->wait_for_input);
      if (ap->get_state (ap, &st) == GRUB_EFI_SUCCESS)
	{
	  int btn = (st.active_buttons & 1) ? 1 : 0;
	  int rbtn = (st.active_buttons >> 1) & 1;
	  grub_efi_uint64_t spanx = ap->mode->absolute_max_x
				    - ap->mode->absolute_min_x;
	  grub_efi_uint64_t spany = ap->mode->absolute_max_y
				    - ap->mode->absolute_min_y;

	  publish (spanx ? (int) ((st.current_x - ap->mode->absolute_min_x)
				  * WB_PTR_RANGE / spanx) : 0,
		   spany ? (int) ((st.current_y - ap->mode->absolute_min_y)
				  * WB_PTR_RANGE / spany) : 0,
		   btn, rbtn);
	  if (owned ())
	    {
	      /* consumer hit-tests; keep edge state current, emit nothing */
	      ap_btn_prev = btn;
	      ap_last_x = st.current_x;
	      ap_last_y = st.current_y;
	      ap_have_last = 1;
	      goto simple;
	    }

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

 simple:
  if (sp)
    {
      struct grub_efi_simple_pointer_state st;
      if (sp->wait_for_input)
	grub_efi_system_table->boot_services->check_event (sp->wait_for_input);
      if (sp->get_state (sp, &st) == GRUB_EFI_SUCCESS)
	{
	  publish_rel (st.relative_movement_x, st.relative_movement_y,
		       st.left_button ? 1 : 0, st.right_button ? 1 : 0);
	  if (owned ())
	    {
	      sp_btn_prev = st.left_button ? 1 : 0;
	      sp_rbtn_prev = st.right_button ? 1 : 0;
	      goto rawhid;
	    }

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

 rawhid:
  while (uio && uring_rd != uring_wr)
    {
      grub_efi_uint8_t buf[URPT_MAX];
      unsigned rd = uring_rd;
      grub_efi_uintn_t len = uring_len[rd % URING_SZ], i;

      for (i = 0; i < len; i++)
	buf[i] = uring[rd % URING_SZ][i];
      uring_rd = rd + 1;

      if (len >= 3)
	{
	  int btn = buf[0] & 1;
	  int rbtn = (buf[0] >> 1) & 1;

	  if (len >= 5)
	    publish (buf[1] | ((int) buf[2] << 8), buf[3] | ((int) buf[4] << 8),
		     btn, rbtn);
	  else
	    publish_rel ((grub_efi_int8_t) buf[1], (grub_efi_int8_t) buf[2],
			 btn, rbtn);
	  if (owned ())
	    {
	      /* consumer hit-tests; keep edge/delta state current */
	      u_btn_prev = btn;
	      u_rbtn_prev = rbtn;
	      if (len >= 5)
		{
		  u_last_x = buf[1] | ((grub_efi_uint64_t) buf[2] << 8);
		  u_last_y = buf[3] | ((grub_efi_uint64_t) buf[4] << 8);
		  u_have_last = 1;
		}
	      continue;
	    }

	  if (btn && ! u_btn_prev)
	    {
	      u_btn_prev = 1;
	      return emit ('\r');
	    }
	  if (! btn)
	    u_btn_prev = 0;
	  if (rbtn && ! u_rbtn_prev)
	    {
	      u_rbtn_prev = 1;
	      return emit (GRUB_TERM_ESC);
	    }
	  if (! rbtn)
	    u_rbtn_prev = 0;

	  if (len >= 5)
	    {
	      /* absolute report (QEMU usb-tablet, touchscreens):
		 [buttons][x lo][x hi][y lo][y hi](...), 0..0x7fff  */
	      grub_efi_uint64_t x = buf[1] | ((grub_efi_uint64_t) buf[2] << 8);
	      grub_efi_uint64_t y = buf[3] | ((grub_efi_uint64_t) buf[4] << 8);
	      if (! u_have_last)
		{
		  u_last_x = x;
		  u_last_y = y;
		  u_have_last = 1;
		}
	      else
		{
		  grub_efi_int64_t dx = (grub_efi_int64_t) x - u_last_x;
		  grub_efi_int64_t dy = (grub_efi_int64_t) y - u_last_y;
		  grub_efi_int64_t step = 0x7fff / 8 + 1;
		  grub_efi_int64_t adx = dx < 0 ? -dx : dx;
		  grub_efi_int64_t ady = dy < 0 ? -dy : dy;
		  if (adx >= step || ady >= step)
		    {
		      u_last_x = x;
		      u_last_y = y;
		      if (adx >= ady)
			return emit (dx > 0 ? GRUB_TERM_KEY_RIGHT
					    : GRUB_TERM_KEY_LEFT);
		      return emit (dy > 0 ? GRUB_TERM_KEY_DOWN
					  : GRUB_TERM_KEY_UP);
		    }
		}
	    }
	  else
	    {
	      /* relative boot-mouse report: [buttons][dx][dy](...)  */
	      u_acc_x += (grub_efi_int8_t) buf[1];
	      u_acc_y += (grub_efi_int8_t) buf[2];
	      if (u_acc_x >= SP_STEP)  { u_acc_x = 0; return emit (GRUB_TERM_KEY_RIGHT); }
	      if (u_acc_x <= -SP_STEP) { u_acc_x = 0; return emit (GRUB_TERM_KEY_LEFT); }
	      if (u_acc_y >= SP_STEP)  { u_acc_y = 0; return emit (GRUB_TERM_KEY_DOWN); }
	      if (u_acc_y <= -SP_STEP) { u_acc_y = 0; return emit (GRUB_TERM_KEY_UP); }
	    }
	}
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
  mouse_stop_raw_hid ();	/* cancel the firmware's async transfer */
}
