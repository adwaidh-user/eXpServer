#include "core/xps_core.h"
#include "utils/xps_logger.h"
#include "xps.h"
#include <signal.h>
#include <stdlib.h>

// Global variables
xps_core_t* core;

void sigint_handler(int signum);

int main() {
    signal(SIGINT, sigint_handler);

    core = xps_core_create();

    xps_core_start(core);
}

void sigint_handler(int signum) {
    logger(LOG_WARNING, "sigint_handler()", "SIGINT recived");

    xps_core_destroy(core);

    exit(EXIT_SUCCESS);
}
