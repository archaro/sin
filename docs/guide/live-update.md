# Live updates

A running Sinistra instance allows for the possibility that items may be modified, and new items created, dynamically.  This includes code items.  The practical upshot of this is that Sinistra is internally extensible.  For those greybeards who remember FORTH, the concept is similar to 'words'.

The exception to this is that (for reasons which should be clear) an item which is currently being executed cannot be replaced in-flight.
