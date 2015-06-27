#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <math.h>

// this is libusb, see http://libusb.sourceforge.net/ 
#include <usb.h>

// same as in main.c
#define USB_LED_OFF 0
#define USB_LED_ON  1
#define USB_MEASURE_DISTANCE 2

#define NUM_DISTANCES 10

#define YES 0
#define NO  1

#define MIN_VIEW_DIST 20
#define MAX_VIEW_DIST 100
#define BRIGHTNESS_STEP 0.01
#define MIN_BRIGHTNESS 0.2
#define MAX_BRIGHTNESS 1.0

// used to get descriptor strings for device identification 
static int usbGetDescriptorString(usb_dev_handle *dev, int index, int langid, 
		char *buf, int buflen) {
	char buffer[256];
	int rval, i;

	// make standard request GET_DESCRIPTOR, type string and given index 
	// (e.g. dev->iProduct)
	rval = usb_control_msg(dev, 
			USB_TYPE_STANDARD | USB_RECIP_DEVICE | USB_ENDPOINT_IN, 
			USB_REQ_GET_DESCRIPTOR, (USB_DT_STRING << 8) + index, langid, 
			buffer, sizeof(buffer), 1000);

	if(rval < 0) // error
		return rval;

	// rval should be bytes read, but buffer[0] contains the actual response size
	if((unsigned char)buffer[0] < rval)
		rval = (unsigned char)buffer[0]; // string is shorter than bytes read

	if(buffer[1] != USB_DT_STRING) // second byte is the data type
		return 0; // invalid return type

	// we're dealing with UTF-16LE here so actual chars is half of rval,
	// and index 0 doesn't count
	rval /= 2;

	// lossy conversion to ISO Latin1 
	for(i = 1; i < rval && i < buflen; i++) {
		if(buffer[2 * i + 1] == 0)
			buf[i-1] = buffer[2 * i];
		else
			buf[i-1] = '?'; // outside of ISO Latin1 range
	}
	buf[i-1] = 0;

	return i-1;
}

static usb_dev_handle * usbOpenDevice(int vendor, char *vendorName, 
		int product, char *productName) {
	struct usb_bus *bus;
	struct usb_device *dev;
	char devVendor[256], devProduct[256];

	usb_dev_handle * handle = NULL;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	for(bus=usb_get_busses(); bus; bus=bus->next) {
		for(dev=bus->devices; dev; dev=dev->next) {			
			if(dev->descriptor.idVendor != vendor ||
					dev->descriptor.idProduct != product)
				continue;

			// we need to open the device in order to query strings 
			if(!(handle = usb_open(dev))) {
				fprintf(stderr, "Warning: cannot open USB device: %s\n",
						usb_strerror());
				continue;
			}

			// get vendor name 
			if(usbGetDescriptorString(handle, dev->descriptor.iManufacturer, 0x0409, devVendor, sizeof(devVendor)) < 0) {
				fprintf(stderr, 
						"Warning: cannot query manufacturer for device: %s\n", 
						usb_strerror());
				usb_close(handle);
				continue;
			}

			// get product name 
			if(usbGetDescriptorString(handle, dev->descriptor.iProduct, 
						0x0409, devProduct, sizeof(devVendor)) < 0) {
				fprintf(stderr, 
						"Warning: cannot query product for device: %s\n", 
						usb_strerror());
				usb_close(handle);
				continue;
			}

			if(strcmp(devVendor, vendorName) == 0 && 
					strcmp(devProduct, productName) == 0)
				return handle;
			else
				usb_close(handle);
		}
	}

	return NULL;
}

static void change_screen_brightness(float value) {

	if(value > MAX_BRIGHTNESS) {
		value = MAX_BRIGHTNESS;
	}
	char brightness_level[5];
	snprintf(brightness_level, 5, "%f", roundf(value * 100) / 100);
	char *screen_brightness_command[] = {"xrandr", "--output", "LVDS", "--brightness", brightness_level, NULL};

	execvp("xrandr", screen_brightness_command);
}

static void led_on(usb_dev_handle *handle)
{
	usb_control_msg(handle, 
			USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_IN, 
			USB_LED_ON, 0, 0, NULL, 0, 5000);
}

static void led_off(usb_dev_handle *handle)
{
	usb_control_msg(handle, 
			USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_IN, 
			USB_LED_OFF, 0, 0, NULL, 0, 5000);
}

static int average_distance(int *distances)
{
	int i, average = 0;

	for(i = 0; i < NUM_DISTANCES; i++) {
		average += distances[i];
	}

	return (average / NUM_DISTANCES);
}

int main(int argc, char **argv) {
	usb_dev_handle *handle = NULL;
	int nBytes = 0;
	char buffer[256];
	int distances[NUM_DISTANCES] = {0}, i = 0, can_measure, d;
	float value = 0.000;
	pid_t helper;

	handle = usbOpenDevice(0x16C0, "ProiectPM", 0x05DC, "BrightnessAdjuster");

	can_measure = NO;
	while(1) {
		nBytes = usb_control_msg(handle, 
				USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_IN, 
				USB_MEASURE_DISTANCE, 0, 0, (char *)buffer, sizeof(buffer), 5000);
		if(nBytes < 0)
			fprintf(stderr, "USB error: %s\n", usb_strerror());

		distances[i++] = strtol(buffer, NULL, 10);
		if (i == NUM_DISTANCES) {
			i = 0;
			can_measure = YES;
		}
		if (can_measure == YES) {
			d = average_distance(distances);
			helper = fork();
			switch (helper) {
				case -1:
					/* error forking */
					return EXIT_FAILURE;
				case 0:
					/* child process */

					value = MIN_BRIGHTNESS + (d - MIN_VIEW_DIST) * BRIGHTNESS_STEP;
					if (d > MIN_VIEW_DIST && d < MAX_VIEW_DIST) {
						led_off(handle);
						change_screen_brightness(value);
					} else if (d > MAX_VIEW_DIST) {
						led_on(handle);
						change_screen_brightness(0);
					} else if (d < MIN_VIEW_DIST) {
						led_off(handle);
					}

					exit(EXIT_FAILURE);
				default:
					wait(NULL);
					break;
			}
		}
	}
	usb_close(handle);

	return 0;
}
