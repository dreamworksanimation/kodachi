# Summary
Code specific to integrating the Moonray renderer with Katana. Moonray integration code that isn't specific to Katana (which is most of it) belongs in kodachi_moonray, which this package uses extensively.

# MoonrayRenderManager
This is a python class that takes over the handling of preview and live renders. This is done to enable multi-context renders and optree deltas. The MoonrayRenderManager builds the optree(s) and sends them to the render plugin over a ZMQ connection. The optree initially built and passed to renderboot is largely ignored.

# Render Plugin
The Render plugin for Moonray receives the optree from the MoonrayRenderManager in the case of preview and live renders. For disk renders it makes slight modifications to the optree passed to renderboot. The plugin is responsible to add ops to the optree for building render outputs and creating the backendSettings attribute. The plugin is able to determine if the new render should be multi-context, arras, preview, live, contain the Katana ID pass, etc. The correct backend is initialized and the plugin then snapshots pixel buffers and returns them to Katana over a socket.
