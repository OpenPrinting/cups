/*
 * USB discovery regression tests for CUPS.
 *
 * Copyright 2026 by OpenPrinting.
 *
 * Licensed under Apache License v2.0. See the file "LICENSE" for more
 * information.
 */

#include "backend-private.h"
#include <cups/dnssd.h>

#ifdef HAVE_LIBUSB

/* Exercise the real enumeration code without opening physical USB devices. */
#define libusb_init test_libusb_init
#define libusb_exit test_libusb_exit
#define libusb_get_device_list test_libusb_get_device_list
#define libusb_free_device_list test_libusb_free_device_list
#define libusb_get_device_descriptor test_libusb_get_device_descriptor
#define libusb_get_config_descriptor test_libusb_get_config_descriptor
#define libusb_free_config_descriptor test_libusb_free_config_descriptor
#define libusb_get_bus_number test_libusb_get_bus_number
#define libusb_get_device_address test_libusb_get_device_address
#define libusb_open test_libusb_open

static cups_dnssd_t *test_dnssd_new(cups_dnssd_error_cb_t cb, void *data);
static cups_dnssd_browse_t *test_browse_new(cups_dnssd_t *dnssd, uint32_t if_index, const char *types, const char *domain, cups_dnssd_browse_cb_t cb, void *data);
static void test_dnssd_delete(cups_dnssd_t *dnssd);
static unsigned test_sleep(unsigned seconds);

#define cupsDNSSDNew test_dnssd_new
#define cupsDNSSDBrowseNew test_browse_new
#define cupsDNSSDDelete test_dnssd_delete
#define sleep test_sleep
#include "usb-libusb.c"

static struct libusb_endpoint_descriptor endpoint;
static struct libusb_interface_descriptor alts[2];
static struct libusb_interface interfaces[2];
static struct libusb_config_descriptor configs[2];
static struct libusb_device_descriptor descriptor;
static libusb_device *devices[2] = { (libusb_device *)&descriptor, NULL };
static const char *service;
static int fail_new, fail_browse, fail_callback, remove_service, duplicate;
static int init_error, config_error, open_calls, browse_calls, delete_calls;
static int descriptor_reads, descriptor_frees, sleep_seconds, failures;
static int enumeration_before_browse_done;

static void
reset(void)
{
  memset(&endpoint, 0, sizeof(endpoint));
  memset(alts, 0, sizeof(alts));
  memset(interfaces, 0, sizeof(interfaces));
  memset(configs, 0, sizeof(configs));
  memset(&descriptor, 0, sizeof(descriptor));
  endpoint.bmAttributes = LIBUSB_TRANSFER_TYPE_BULK;
  endpoint.bEndpointAddress = 1;
  alts[0].bInterfaceClass = alts[1].bInterfaceClass = LIBUSB_CLASS_PRINTER;
  alts[0].bInterfaceSubClass = alts[1].bInterfaceSubClass = 1;
  alts[0].bInterfaceProtocol = 2;
  alts[1].bInterfaceProtocol = 4;
  alts[0].bNumEndpoints = alts[1].bNumEndpoints = 1;
  alts[0].endpoint = alts[1].endpoint = &endpoint;
  interfaces[0].num_altsetting = interfaces[1].num_altsetting = 1;
  interfaces[0].altsetting = alts;
  interfaces[1].altsetting = alts + 1;
  configs[0].bNumInterfaces = 2;
  configs[0].interface = interfaces;
  configs[1].bNumInterfaces = 1;
  configs[1].interface = interfaces + 1;
  descriptor.bNumConfigurations = 1;
  descriptor.idVendor = descriptor.idProduct = 1;
  service = "01AB";
  fail_new = fail_browse = fail_callback = remove_service = duplicate = 0;
  init_error = config_error = open_calls = browse_calls = delete_calls = 0;
  descriptor_reads = descriptor_frees = sleep_seconds = 0;
  enumeration_before_browse_done = 0;
}

static void
check(const char *name, int print, int expected_opens)
{
  int ok;

  find_device(print ? print_cb : list_cb, print ? "usb://test" : NULL);
  ok = open_calls == expected_opens && descriptor_reads == descriptor_frees &&
       !enumeration_before_browse_done && sleep_seconds <= 1;
  if (print)
    ok = ok && !browse_calls && !delete_calls && !sleep_seconds;
  printf("%s: %s (opens=%d, descriptor balance=%d, browse waits=%d)\n",
         name, ok ? "PASS" : "FAIL", open_calls,
         descriptor_reads - descriptor_frees, sleep_seconds);
  if (!ok)
    failures ++;
}

int
main(void)
{
  reset(); check("legacy interface before IPP-USB", 0, 0);
  reset(); alts[0].bInterfaceProtocol = 4; alts[1].bInterfaceProtocol = 2;
  check("IPP-USB interface before legacy", 0, 0);
  reset(); configs[0].bNumInterfaces = 1; descriptor.bNumConfigurations = 2;
  check("IPP-USB in a later configuration", 0, 0);
  reset(); configs[0].bNumInterfaces = 1; interfaces[0].num_altsetting = 2;
  check("IPP-USB in a later alternate setting", 0, 0);
  reset(); service = "01ab"; check("lowercase service address", 0, 0);
  reset(); service = "02AB"; check("different bus", 0, 1);
  reset(); service = "01AC"; check("different device address", 0, 1);
  reset(); service = NULL; check("no local advertisement", 0, 1);
  reset(); configs[0].bNumInterfaces = 1;
  check("legacy-only printer with matching name", 0, 1);
  reset(); alts[1].bInterfaceClass = 0xff;
  check("protocol4 outside printer class", 0, 1);
  reset(); alts[1].bInterfaceSubClass = 2;
  check("protocol4 outside printer subclass", 0, 1);
  reset(); service = "01AB-other"; check("invalid long service name", 0, 1);
  reset(); service = "1AB"; check("invalid short service name", 0, 1);
  reset(); service = "01AZ"; check("invalid hexadecimal service name", 0, 1);
  reset(); remove_service = 1; check("withdrawn service", 0, 1);
  reset(); duplicate = remove_service = 1;
  check("one address advertised on two protocols", 0, 0);
  reset(); fail_new = 1; check("DNS-SD unavailable", 0, 1);
  reset(); fail_browse = 1; check("browse creation failure", 0, 1);
  reset(); fail_callback = 1; check("browse error discards partial results", 0, 1);
  reset(); check("existing printing path bypasses browse", 1, 1);
  reset(); init_error = 1; check("USB initialization failure cleanup", 0, 0);
  reset(); configs[0].bNumInterfaces = 1; descriptor.bNumConfigurations = 2;
  config_error = 1; check("unreadable IPP-USB configuration", 0, 1);
  return (failures ? 1 : 0);
}

int test_libusb_init(libusb_context **ctx)
{
  (void)ctx;
  if (browse_calls && !delete_calls)
    enumeration_before_browse_done = 1;
  return (init_error ? LIBUSB_ERROR_OTHER : 0);
}

void test_libusb_exit(libusb_context *ctx) { (void)ctx; }

ssize_t test_libusb_get_device_list(libusb_context *ctx, libusb_device ***list)
{
  (void)ctx;
  *list = devices;
  return (1);
}

void test_libusb_free_device_list(libusb_device **list, int unref)
{
  (void)list;
  (void)unref;
}

int test_libusb_get_device_descriptor(libusb_device *device, struct libusb_device_descriptor *desc)
{
  (void)device;
  *desc = descriptor;
  return (0);
}

int test_libusb_get_config_descriptor(libusb_device *device, uint8_t index, struct libusb_config_descriptor **config)
{
  (void)device;
  if (config_error && index == 1)
    return (LIBUSB_ERROR_IO);
  *config = configs + index;
  descriptor_reads ++;
  return (0);
}

void test_libusb_free_config_descriptor(struct libusb_config_descriptor *config)
{
  (void)config;
  descriptor_frees ++;
}

uint8_t test_libusb_get_bus_number(libusb_device *device) { (void)device; return (1); }
uint8_t test_libusb_get_device_address(libusb_device *device) { (void)device; return (0xab); }

int test_libusb_open(libusb_device *device, libusb_device_handle **handle)
{
  (void)device;
  *handle = NULL;
  open_calls ++;
  return (LIBUSB_ERROR_ACCESS);
}

static cups_dnssd_t *
test_dnssd_new(cups_dnssd_error_cb_t cb, void *data)
{
  if (fail_new)
  {
    cb(data, "Test DNS-SD unavailable");
    return (NULL);
  }
  return ((cups_dnssd_t *)&descriptor);
}

static cups_dnssd_browse_t *
test_browse_new(cups_dnssd_t *dnssd, uint32_t if_index, const char *types,
               const char *domain, cups_dnssd_browse_cb_t cb, void *data)
{
  cups_dnssd_browse_t *browse = (cups_dnssd_browse_t *)dnssd;

  browse_calls ++;
  if (if_index != CUPS_DNSSD_IF_INDEX_LOCAL || strcmp(types, "_ipp-usb._tcp") || strcmp(domain, "local."))
  {
    puts("FAIL: browse must be limited to local IPP-USB advertisements");
    failures ++;
  }
  if (fail_browse)
    return (NULL);
  if (service)
  {
    cb(browse, data, CUPS_DNSSD_FLAGS_ADD, if_index, service, types, domain);
    if (duplicate)
      cb(browse, data, CUPS_DNSSD_FLAGS_ADD, if_index, service, types, domain);
    if (remove_service)
      cb(browse, data, CUPS_DNSSD_FLAGS_NONE, if_index, service, types, domain);
  }
  if (fail_callback)
    cb(browse, data, CUPS_DNSSD_FLAGS_ERROR, if_index, NULL, types, domain);
  return (browse);
}

static void test_dnssd_delete(cups_dnssd_t *dnssd) { (void)dnssd; delete_calls ++; }
static unsigned test_sleep(unsigned seconds) { sleep_seconds += (int)seconds; return (0); }

#else
int main(void) { puts("USB discovery tests skipped (no libusb)"); return (0); }
#endif /* HAVE_LIBUSB */
