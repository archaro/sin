# Live updates

A running Sinistra instance allows for the possibility that items may be modified, and new items created, dynamically.  This includes code items.  The practical upshot of this is that Sinistra is internally extensible.  For those greybeards who remember FORTH, the concept is similar to 'words'.

The exception to this is that (for reasons which should be clear) an item which is currently being executed cannot be replaced in-flight.  An example:

```
@source = "hello = code ( sys.log{\"Hello\\n"}; );";
@result = sys.compile{@source};
hello;
```

The above code will output `"Hello!"` to the system log.  You can get even more creative by passing a parameter:

```
@source = "hello = code {@name} ( if !@name then @name = \"Unknown User\"; endif; sys.log{\"Hello \"}; sys.log{@name}; sys.log{\"!\\n\"}; );";
@result = sys.compile{@source};
hello{"Boris"};
hello;
```

This code will output `"Hello Boris!"` followed by `"Hello Unknown User!"`.  Observe that if a code item expects a parameter but does not receive one, it is initialised to `nil`.

Also note that `sys.compile` returns true if the code compiled successfully, and false on error.  If there is a compilation error, the `error` item will have some interesting information.  See the [error documentation](../reference/errors.md) for more information on errors and what they mean.
