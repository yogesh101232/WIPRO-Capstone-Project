#ifndef TEMPSENSOR_IOCTL_H
#define TEMPSENSOR_IOCTL_H

#include <linux/ioctl.h>

#define TEMP_IOC_MAGIC 't'

/* Reset the simulated sensor back to a baseline temperature (25.0 C) */
#define TEMP_IOC_RESET     _IO(TEMP_IOC_MAGIC, 1)

/* Set how aggressively the simulated temperature drifts each read.
 * Argument is an int, in tenths of a degree (e.g. 5 = up to 0.5C per read). */
#define TEMP_IOC_SET_DRIFT _IOW(TEMP_IOC_MAGIC, 2, int)

#endif
