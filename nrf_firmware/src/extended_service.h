// extended_service.h -- Singleton owner of the preOBD Extended Service.
//
// Background: the Extended Service (`...0002`) is the GATT parent for the
// Alarm, Relay, OTA Control, and OTA Data characteristics. Originally each
// handler created its own BLEService instance with the same UUID, which
// produced multiple distinct service handles on the SoftDevice GATT table
// and made Chrome's `getPrimaryService(EXT)` return whichever happened to
// land first -- non-deterministically yielding services that lacked
// characteristics registered by sibling handlers. Symptom: the webapp
// oscillated between Awaiting and Basic across reconnects because the
// Alarm + Relay characteristic lookups intermittently failed depending on
// which duplicate service Chrome surfaced.
//
// Fix: register the Extended Service exactly once via extendedServiceInit().
// Each handler then just registers its characteristic(s) -- Bluefruit
// attaches each characteristic.begin() to the most-recently-begun service,
// so as long as extendedServiceInit() is the last service.begin() call
// before the per-handler init calls, every char lands on the same parent.

#ifndef EXTENDED_SERVICE_H
#define EXTENDED_SERVICE_H

#include <bluefruit.h>

void extendedServiceInit();

#endif // EXTENDED_SERVICE_H
