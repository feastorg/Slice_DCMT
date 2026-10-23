// setup() caps both speed loops' lead at DCMT_TACHO_MAX_LEAD where the speed
// loop is built (#63). Without the cap a held shaft banks travel and lunges
// on release.
#include "host.h"
#include "config.h"
#include "config_hardware.h"
#include <DCMotorTacho.h>

void setup();

int main()
{
    setup();

#if DCMT_ENABLE_SPEED_LOOP
    host_check("motor 1 lead capped", host_tacho_max_lead[0] == DCMT_TACHO_MAX_LEAD);
    host_check("motor 2 lead capped", host_tacho_max_lead[1] == DCMT_TACHO_MAX_LEAD);
#else
    host_check("no speed loop, no cap set", host_tacho_max_lead[0] == 0 && host_tacho_max_lead[1] == 0);
#endif
    return host_finish();
}
