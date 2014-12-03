**DEPRECATED** use the oculus module: https://github.com/omega-hub/oculus

# rift - Oculus Rift support for omegalib
This module adds Oculus Rift support to omegalib, reading sensor data from the device and rendering using a post-processing shader that corrects for the Oculs Rift lens distortion and chromatic aberration.

All the code and shaders are included in the module. An example system configuration file for the rift is in `<omegalib root>/data/system/rift.cfg`

## Using the rift module
To run your application using the Oculus Rift render, simply choose an oculus rift configuration at startup. For instance when using orun:
```
> orun -c system/rift.cfg -s <script name>
```	

## Creating a rift configuration
Creating your own configuration for the Oculus Rift is very easy. You just need to register the `rift` module during initialization, Add it to the running services, and specify which tiles should enable rift postprocesing:
```
config:
{
	// load the Oculus Rift support module and register it.
	initCommand = "import rift; rift.registerService();";
	
	display:
	{
		[...]
		
		tiles:
		{
			local:
			{
				t0x0: 
				{ 
					// Enable Oculus Rift postprocessing for tile t0x0
					riftEnabled = true; 
				};
			};
		};
	
	services:
	{
		[...]
		
		// Add the OculusRiftService to the running services
		OculusRiftService: {};
	};

	[...]	
};
```

## How does it work
The `OculusRiftService` runs as a standard omicron service to generate events tracking the rift sensor orientation. The service also implements the `ICameraListener`, allowing it to be attached to the default omegalib camera and redirect all rendering to a texture. At the end of a frame, the texture is rendered to the output framebuffer after running through a postprocessing shader.

During initialization, the service checks the display configuration and keeps track of all the tiles for which rift postprocessing is enabled. It also modifies the stereo mode for the tiles, to enable side-by-side stereo rendering.

**NOTE** Altough the rift module requires `LibOVR` to interface to an Oculus Rift, you can build it with the `RIFT_EMULATE` option enabled to remove the dependency.

## Python API
The `rift` module exposes several functions to control the service:

### Global functions
| **Function** | Description
|---|---|
| `registerService()` | Used in the configuration file initCommand to register the Oculus Rit service. Should never be used at runtime. |
| `bool isEnabled()` | Returns true if the service is enabled and running
| `OculusRiftService getService()` | returns a service instance or `None` if the service is not running. |

### `OculusRiftService`
| **Method** | Description
|---|---|
| `float getLensOffset()`, `setLensOffset(float value)` | Sets or gets the distance between the two lenses. |
| `float getScaleParam(int index)`, `setScaleParam(int index, float value)` | Sets or gets one of the 4 scale parameters. |
| `float getDistortionParam(int index)`, `setDistortionParam(int index, float value)` | Sets or gets one of the 4 distortion parameters. |
