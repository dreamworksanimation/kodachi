# Summary

Kodachi is a scene graph processing ecosystem. This package pulls in a minimal amount of Katana libraries and header files to enable the use of the Geolib3 scene graph processing engine and for the authoring of ops and optrees. The FnKat namespace is replaced with the kodachi namespace where appropriate.

# KodachiRuntime
A thread-safe wrapper around the Geolib3 runtime and its child classes, Matches the Geolib3 API with a few exceptions. Much of this class is likely no longer relevant with recent versions of Katana and Geolib3 MT.

# OpTreeBuilder
Uses a similar API to the GeolibRuntime Transaction for building optrees as a GroupAttribute. This allows for the easy construction of optrees from python or c++ without needing to open Katana.

# Backends and the BackendClient
Kodachi backends are plugins that do some sort of work based on the optree they are initialized with. They can be further interacted with through the getData() and setData() methods on the BackendClient. Examples of backends are the MoonrayRenderBackend and UsdExportBackend. The BackendClient takes a optree and creates and initializes a backend plugin based on the 'kodachi.backendSettings' attribute on /root.

# Kodachi Cache
Designed as a disk-enabled replacement for the AttributeKeyedCache. The KodachiCache takes a specified type of Key and uses it to generate value based on a user-defined createValue() function. Based on the settings, the created values can then be written to disk. The most popular type of Kodachi cache is the GroupAttributeCache, which takes a GroupAttribute as a Key and then produces a GroupAttribute as a value.

# Kodachi Logging
Similar to FnLogging, multiple "handlers" or sinks can be registered so that single calls into the KdLogging macros can potentially call into multiple logging systems. When no handler is specified it defaults to calling cout.



