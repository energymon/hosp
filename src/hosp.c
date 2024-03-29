/**
 * A library for managing an ODROID Smart Power device over USB.
 *
 * @author Connor Imes
 * @date 2018-05-22
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hidapi.h>
#include <hosp.h>

#define HOSP_BUF_SIZE            65
#define HOSP_REQUEST_DATA        0x37
#define HOSP_REQUEST_STARTSTOP   0x80
#define HOSP_REQUEST_STATUS      0x81
#define HOSP_REQUEST_ONOFF       0x82
#define HOSP_REQUEST_VERSION     0x83

#define HOSP_STATUS_ON           0x01
#define HOSP_STATUS_STARTED      0x01

#ifndef HOSP_DEBUG
  #define HOSP_DEBUG 0
#endif

struct hosp_device {
  hid_device* dev;
  unsigned char buf[HOSP_BUF_SIZE];
  int is_own_dev;
};

// Returns 0 on success, -errno on failure
static int hosp_write(hosp_device* hosp, unsigned char type) {
  hosp->buf[0] = 0x00;
  hosp->buf[1] = type;
#if HOSP_DEBUG
  printf("hosp_write: %s\n", hosp->buf);
#endif
  errno = 0;
  if (hid_write(hosp->dev, hosp->buf, sizeof(hosp->buf)) == -1) {
    // HIDAPI not guaranteed to set errno
    if (!errno) {
      errno = EIO;
    }
    return -errno;
  }
  return 0;
}

// Returns 0 on success, -errno on failure, 1 if data is not ready
static int hosp_read(hosp_device* hosp, unsigned char type) {
  hosp->buf[0] = 0x00;
  hosp->buf[1] = type;
  errno = 0;
  if (hid_read(hosp->dev, hosp->buf, sizeof(hosp->buf)) == -1) {
    // HIDAPI not guaranteed to set errno
    if (!errno) {
      errno = EIO;
    }
    return -errno;
  }
#if HOSP_DEBUG
  printf("hosp_read: %s\n", hosp->buf);
#endif
  return hosp->buf[0] != type;
}

struct hid_device_info* hosp_enumerate(void) {
  errno = 0;
  struct hid_device_info* dev_info = hid_enumerate(HOSP_VENDOR_ID, HOSP_PRODUCT_ID);
  if (dev_info == NULL && !errno) {
    errno = EIO;
  }
  return dev_info;
}

hosp_device* hosp_open(void) {
  return hosp_open_device(NULL);
}

hosp_device* hosp_open_device(hid_device* dev) {
  hosp_device* hosp;
  if ((hosp = calloc(1, sizeof(hosp_device))) == NULL) {
    return NULL;
  }
  if (dev == NULL) {
    hosp->is_own_dev = 1;
    // get the HID device handle
    if ((hosp->dev = hid_open(HOSP_VENDOR_ID, HOSP_PRODUCT_ID, NULL)) == NULL) {
      if (!errno) {
        errno = EIO;
      }
      free(hosp);
      return NULL;
    }
  } else {
    hosp->dev = dev;
  }
  return hosp;
}

int hosp_close(hosp_device* hosp) {
  errno = 0;
  if (hosp->is_own_dev) {
    // close the HID device handle
    hid_close(hosp->dev);
  }
  free(hosp);
  return -errno;
}

hid_device* hosp_get_device(hosp_device* hosp) {
  return hosp->dev;
}

int hosp_request_version_write(hosp_device* hosp) {
  return hosp_write(hosp, HOSP_REQUEST_VERSION);
}

int hosp_request_version_read(hosp_device* hosp, char* buf, size_t bufsize) {
  size_t bytes;
  char* ptr;
  int ret;
  if (!(ret = hosp_read(hosp, HOSP_REQUEST_VERSION))) {
    bytes = bufsize < 17 ? bufsize - 1 : 16;
    // strncpy can get a stringop-truncation warning here, so use stpncpy
    ptr = stpncpy(buf, (char*) &hosp->buf[1], bytes);
    *ptr = '\0';
  }
  return ret;
}

int hosp_request_status_write(hosp_device* hosp) {
  return hosp_write(hosp, HOSP_REQUEST_STATUS);
}

int hosp_request_status_read(hosp_device* hosp, int* is_on, int* is_started) {
  int ret;
  if (!(ret = hosp_read(hosp, HOSP_REQUEST_STATUS))) {
    if (is_on) {
      *is_on = (hosp->buf[2] == HOSP_STATUS_ON);
    }
    if (is_started) {
      *is_started = (hosp->buf[1] == HOSP_STATUS_STARTED);
    }
  }
  return ret;
}

int hosp_request_onoff_write(hosp_device* hosp) {
  return hosp_write(hosp, HOSP_REQUEST_ONOFF);
}

int hosp_request_startstop_write(hosp_device* hosp) {
  return hosp_write(hosp, HOSP_REQUEST_STARTSTOP);
}

static void hosp_str_units_to_milliunits(const char* str, size_t len, unsigned int* val) {
  size_t s;
  if (val != NULL) {
    if (str[0] == '-') {
      // device is off
      *val = 0;
    } else {
      *val = 1000 * (unsigned int) atoi(str);
      for (s = 1; s < len && str[s - 1] != '.'; s++);
      *val += (unsigned int) atoi(&str[s]);
    }
  }
}

int hosp_request_data_write(hosp_device* hosp) {
  return hosp_write(hosp, HOSP_REQUEST_DATA);
}

int hosp_request_data_read(hosp_device* hosp, unsigned int* mV, unsigned int* mA, unsigned int* mW, unsigned int* mWh) {
  int ret;
  if (!(ret = hosp_read(hosp, HOSP_REQUEST_DATA))) {
    // Reply when device is off: "7 5.000V  -.--- A -.---W  -.---Wh" followed by garbage characters
    // Dashes are replaced with actual values when device is on
    // Volts are always shown, even when device is off
    hosp_str_units_to_milliunits((char*) &hosp->buf[2], 5, mV);
    hosp_str_units_to_milliunits((char*) &hosp->buf[10], 5, mA);
    hosp_str_units_to_milliunits((char*) &hosp->buf[17], 6, mW);
    hosp_str_units_to_milliunits((char*) &hosp->buf[24], 7, mWh);
  }
  return ret;
}
