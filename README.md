# Summary

This package contains source code for three libraries - kodachi, kodachi_moonray, and moonray_katana. This code is from 2019 prior with older versions of Katana and Moonray and is not expected to build or be functional as-is. It simply serves as sample code with processing Katana scenes, conditioning them to interface with Moonray, and retreiving Moonray renders in Katana.

kodachi is the foundational library which kodachi_moonray and moonray_katana depends on. It wraps Katana libraries and header files for scene graph processing and contains tools around building optrees.

kodachi_moonray contains all the tools for conditioning kodachi optrees into Moonray data and interfaces with Moonray renders.

moonray_katana depends on kodachi_moonray and contains the Moonray render plugin for Katana.



