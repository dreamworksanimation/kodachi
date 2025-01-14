# Summary
This package contains all of the code related to producing Moonray renders from Kodachi optrees. The Moonray render plugin for Katana (which is in the moonray_katana package) relies on this package for interfacing with Moonray.

## MoonrayRenderBackend
This is the Kodachi BackendPlugin that handles all interaction with Moonray. It is initialized with an optree and can then be used to start preview, live, or disk renders.

### Backend Settings
The 'kodachi.backendSettings' attribute on '/root' configures the MoonrayRenderBackend.

#### isLiveRender - IntAttribute
Set to 1 if optree deltas are going to be applied, 0 otherwise.

#### systemOpArgs - GroupAttribute
The system opArgs to apply if the backend is responsible for adding the implicit resolvers or terminal ops to the optree

#### appendImplicitResolvers - IntAttribute
Set to 1 if the backend should apply implicit resolvers to the optree

#### appendTerminalOps - IntAttribute
Set to 1 if the backend should apply terminal ops to the optree

#### writeToDisk - IntAttribute
Set to 1 for disk renders.

#### numThreads - IntAttribute
The number of threads for TBB and Moonray to use.

#### machineId, numMachines - IntAttribute
For multi-machine Arras renders. These are set automatically in the KodachiRenderComputation.

#### progressiveFrameMode - IntAttribute
Set to 1 to snapshot ProgressiveFrame messages instead of RendererFrame messages.

#### idPass - GroupAttribute
Enables the creation of the idPass, the various settings allow the pass to be less specific to Katana.

### Optree Deltas
Optree deltas are applied by passing them as a message through the setData() method.

## KPOPs
Backronym for Kodachi Procedural Ops. These are the terminal ops that condition kodachi location data into data that more closely resembles what the Moonray SceneContext is expecting.

## MoonrayRenderState
This is a wrapper around the rdl2::SceneContext. It takes LocationData from the scenegraph traversal and builds the SceneContext. It also handles a lot of bookkeeping for live renders, and data that needs to be deferred until then end of the traversal. The most common example of this is bindings and references to other scene objects. Since a location may reference another location that hasn't yet been processed (and thus a SceneObject for it doesn't exist) then we have to defer making that connection until we are certain that all scene objects have been created.

The rdl2::SceneContext has methods for setting attribute values, by name (as a std::string) or AttributeKey. Since the LocationData provided by the KodachiRuntime is a GroupAttribute, then we basically have a map of string_view attribute names to their values. To avoid potentially hundreds of thousands of string allocations just to call the std::string-based setters on the rdl2::SceneContext, the MoonrayRenderState maintains it's own mappings of string_view to rdl2::Attribute and then uses that to call the AttributeKey setters.

## Auto-Instancing
Auto instancing starts with each KPOP determining if they are setting attributes for a geometry that contribute to it's uniqueness. These attributes are then added to the 'rdl2.meta.autoInstancing.attrs' GroupAttribute of the location. For example, KPOPMaterial adds the material network to the autoInstancing.attrs attribute since instances must share the same material. However, KPOPNode does not add the xform attribute to the AutoInstancing.attrs attribute since instances can have their own xform.

KPOPAutoInstancing then takes the hash of the autoInstancing.attrs attribute and sets it as the instance.ID attribute on the location. The MoonrayRenderState checks for this ID when processing a scene graph location. The first time it sees an ID it registers the SceneObject created for the location as the potential instance source for the ID. The second time it sees the same ID it creates 2 instances of the SceneObject for the first location: the first to stand in for the original SceneObject since it is now an instance source, and the second to be the instance for the second location. All subsequent locations processed with the same ID create an instance of the instance source.

### Primitive Attributes

Instances are allowed to have their own "CONSTANT" rate primitive attributes, so these attributes are not taken into account when computing the instance.ID.

## GroupGeometry
GroupGeometry is a Moonray geometry procedural used to handle all cases of instances in Kodachi except for "instance array" locations, which use the Moonshine InstanceGeometry procedural. It creates instances of the geometry SceneObjects listed in the "references" attribute, and applies instance-specific attributes like xform and constant rate primitive attributes.

### Instance Source Hierarchies
Instance source hierarchies are passed to Moonray as nested GroupGeometries. The 'instance source' location itself will be represented as a GroupGeometry, as well as all non-leaf-level descecendent locations. Leaf level geometry will have GroupGeometry parents.

### Instance Locations
"instance" locations are coverted to GroupGeometries. Their "instanceSource" attribute is used to set the "references" attribute on GroupGeometry.

### Auto Instancing
GroupGeometry is also used to create instances during auto-instancing.

## KodachiGeometry Procedurals
Moonray geometry procedurals that create Moonray primitives directly from Kodachi data (currently only for meshes and curves). These procedurals hold onto their location data from scene traversal as a member variable that isn't an rdl2 Attribute. When serialized to rdl, the procedurals only write their scene graph location. During render prep, the procedurals use their location data to build the equivalent Moonray primitives and in the case of preview and disk renders, the location data is then released.

## KodachiRuntime Procedural
This procedural is used in cases where the scene context is serialized to rdl. The optree binary is serialized as a base64 string. When the rdl is loaded into raas_gui or raas_render, the procedural can then load the optree into a KodachiRuntime, and an KodachiGeometry procedurals in the scene can then re-cook their location data. This allows for more read-able rdla files since we don't have to store geometry primitive data in the file.
