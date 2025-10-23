#include "psoc_driver/psoc_driver.h"

#define PSOC_DRIVER_VERSION "1.0.0"

const char* psoc_driver_version(void) {
    return PSOC_DRIVER_VERSION;
}

bool psoc_driver_init(void) {
    // Nothing to initialize for now
    return true;
}

void psoc_driver_cleanup(void) {
    // Nothing to cleanup for now
}
