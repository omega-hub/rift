#include <omega.h>
#include <omegaGl.h>

#ifndef RIFT_EMULATE
	#include <OVR.h>
#endif

#include "OVRShaders.h"

using namespace omega;

// This is the flag value we use to mark tiles that should use the oculus rift
// barrel correction shaders during postprocessing
uint RiftEnabledFlag = 1 << 16;

// The global service instance, used by the python API to control the service.
class OculusRiftService* sInstance = NULL;

///////////////////////////////////////////////////////////////////////////////
// The oculus rift service implements the ICameraListener interface to perform
// postprocessing during rendering.
class OculusRiftService: public Service, ICameraListener
{
public:
	static OculusRiftService* New() { return new OculusRiftService(); }

	OculusRiftService():
		myCamera(NULL),
		myInitialized(false)
		{
		}

	// Service overrides
	virtual void initialize();
	virtual void start();
	virtual void poll();
	virtual void stop();
	virtual void dispose();

	virtual void initializeGraphics(Camera* cam, const DrawContext& context);

	// ICameraListener overrides
	virtual void beginDraw(Camera* cam, DrawContext& context);
	virtual void endDraw(Camera* cam, DrawContext& context);

	// Barrel shader parameters
	float getLensOffset() { return myLensOffset; }
	void setLensOffset(float value) { myLensOffset = value; }
	void setDistortionParam(int index, float value) { myDistortion[index] = value; }
	float getDistortionParam(int index) { return myDistortion[index]; }
	void setScaleFactor(float value) { myScaleFactor = value; }
	float getScaleFactor() { return myScaleFactor; }

private:
	void initOVR();
	void beginEyeDraw();
	void drawEyeQuad(float x, float y, float w, float h, float u, float v, float uw, float vh);
	void endEyeDraw();

	
private:
	bool myInitialized;
	Ref<Camera> myCamera;
	Ref<RenderTarget> myRenderTarget;
	Ref<Texture> myRenderTexture;
	Ref<Texture> myDepthTexture;
	Vector2f myViewportSize;

	Vector4f myDistortion;
	Vector4f myScaleParams;
	float myLensOffset;
	float myScaleFactor;
	float myAspectRatio;

	// Shader stuff
	GLuint myPostprocessProgram;
	GLint  myLensCenterUniform;
	GLint  myScreenCenterUniform;
	GLint  myScaleUniform;
	GLint  myScaleInUniform;
	GLint  myHmdWarpParamUniform;
	GLint  myTexture0Uniform;
	
#ifndef RIFT_EMULATE
    /// OVR hardware
    OVR::Ptr<OVR::DeviceManager>  myManager;
    OVR::Ptr<OVR::HMDDevice>      myHMD;
    OVR::Ptr<OVR::SensorDevice>   mySensor;
    OVR::SensorFusion             mySFusion;
    OVR::HMDInfo                  myHMDInfo;
#endif
};

///////////////////////////////////////////////////////////////////////////////
// Python API follows

// this fuction register the Oculus Rift service with the omegalib service
// manager
void registerService()
{
	ServiceManager* sm = SystemManager::instance()->getServiceManager();
	sm->registerService("OculusRiftService", (ServiceAllocator)OculusRiftService::New);
}

// Returns true when the rift service is enabled
bool isEnabled()
{
	if(sInstance != NULL) return true;
	return false;
}

// Returns a service instance
OculusRiftService* getService()
{
	return sInstance;
}

///////////////////////////////////////////////////////////////////////////////
// Python wrapper code.
#ifdef OMEGA_USE_PYTHON
#include "omega/PythonInterpreterWrapper.h"
BOOST_PYTHON_MODULE(rift)
{
	PYAPI_REF_BASE_CLASS(OculusRiftService)
		PYAPI_METHOD(OculusRiftService, getLensOffset)
		PYAPI_METHOD(OculusRiftService, setLensOffset)
		PYAPI_METHOD(OculusRiftService, setScaleFactor)
		PYAPI_METHOD(OculusRiftService, getScaleFactor)
		PYAPI_METHOD(OculusRiftService, setDistortionParam)
		PYAPI_METHOD(OculusRiftService, getDistortionParam)
		;

	def("registerService", registerService);
	def("isEnabled", isEnabled);
	def("getService", getService, PYAPI_RETURN_REF);
}
#endif


///////////////////////////////////////////////////////////////////////////////
void OculusRiftService::initialize() 
{
	sInstance = this;

	// Loop through display tiles and see which ones are marked to use the rift.
	// This is done for performance reasons: checking a flag at render time is
	// faster than going through the tile settings.
	DisplayConfig& cfg = SystemManager::instance()->getDisplaySystem()->getDisplayConfig();
	typedef KeyValue<String, DisplayTileConfig*> TileItem;
	foreach(TileItem tile, cfg.tiles)
	{
		bool riftEnabled = Config::getBoolValue("riftEnabled", *tile->settingData, false);
		if(riftEnabled)
		{
			tile->flags |= RiftEnabledFlag;
			// Force the stereo mode for this tile to be side-by-side.
			tile->stereoMode = DisplayTileConfig::SideBySide;
			tile->isHMD = true;
			ofmsg("OculusRiftService::initialize: rift postprocessing enabled for tile %1%", %tile->name);
		}
	}
	
	// Set default distortion values
	myDistortion = Vector4f(1.0f, 0.5f, 0.25f, 0.0f);

	// Set default scale parameters
	myScaleParams = Vector4f(0.145806f,  0.233290f, 4.0f, 2.5f);

	// Set default lens offset parameter
	myLensOffset =  0.0f;

	myScaleFactor = 0.8f;
	myAspectRatio = 1.6f;

	// Initialize the Oculus Rift
	initOVR();

	// The camera does not exist yet here, so we deal with it in the poll function.
}

///////////////////////////////////////////////////////////////////////////////
void OculusRiftService::initOVR()
{
#ifndef RIFT_EMULATE
	omsg("\n\n>>>>> OculusRiftService::initOVR");
	
    OVR::System::Init(OVR::Log::ConfigureDefaultLog(OVR::LogMask_All));
	
    myManager = *OVR::DeviceManager::Create();
    myHMD  = *myManager->EnumerateDevices<OVR::HMDDevice>().CreateDevice();
    if (myHMD != NULL)
    {
        mySensor = *myHMD->GetSensor();

        // This will initialize HMDInfo with information about configured IPD,
        // screen size and other variables needed for correct projection.
        // We pass HMD DisplayDeviceName into the renderer to select the
        // correct monitor in full-screen mode.
        if (myHMD->GetDeviceInfo(&myHMDInfo))
        {
			int windowWidth  = myHMDInfo.HResolution;
			int windowHeight = myHMDInfo.VResolution;
			ofmsg("HMD resolution: %1% x %2%", %windowWidth %windowHeight);
			
			float ets = myHMDInfo.EyeToScreenDistance;
			ofmsg("Eye to screen distance: %1%", %ets);
			
			float sw = myHMDInfo.HScreenSize;
			float sh = myHMDInfo.VScreenSize;
			ofmsg("Screen size: %1% %2%", %sw %sh );
			
			myAspectRatio = float(windowWidth) / float(windowHeight);
			
			myDistortion[0] = myHMDInfo.DistortionK[0];
			myDistortion[1] = myHMDInfo.DistortionK[1];
			myDistortion[2] = myHMDInfo.DistortionK[2];
			myDistortion[3] = myHMDInfo.DistortionK[3];
		
			// distance between lens centers
			myLensOffset = myHMDInfo.LensSeparationDistance;
			ofmsg("Lens separation distance: %1%", %myLensOffset);

			myLensOffset = 0.5 - myLensOffset / sw;
			ofmsg("normalized lens offset: %1%", %myLensOffset);
        }
        if (mySensor)
        {
            // We need to attach sensor to SensorFusion object for it to receive 
            // body frame messages and update orientation. SFusion.GetOrientation() 
            // is used in OnIdle() to orient the view.
            mySFusion.AttachToSensor(mySensor);
        }
    }

    if (myHMDInfo.HResolution > 0)
	omsg("<<<<<< OculusRiftService::initOVR\n\n");
#endif
}

///////////////////////////////////////////////////////////////////////////////
void OculusRiftService::dispose() 
{
	sInstance = NULL;
#ifndef RIFT_EMULATE
    // No OVR functions involving memory are allowed after this.
    OVR::System::Destroy();
#endif
}

///////////////////////////////////////////////////////////////////////////////
void OculusRiftService::start() 
{}

///////////////////////////////////////////////////////////////////////////////
void OculusRiftService::poll() 
{
	if(myCamera == NULL)
	{
		// Register myself as a camera listener.
		myCamera = Engine::instance()->getDefaultCamera();
		myCamera->addListener(this);
	}
	
#ifndef RIFT_EMULATE
	// Update head orientation;
	OVR::Quatf o = mySFusion.GetOrientation();
	Quaternion q = Quaternion(o.w, o.x, o.y, o.z);
	//q = q * Math::quaternionFromEuler(Vector3f(0, 0, Math::Pi));
	myCamera->setOrientation(q);
	myCamera->setHeadOrientation(Math::quaternionFromEuler(Vector3f(0, 0, Math::Pi)));
	/*if(myCamera->getController() != NULL)
	{
		myCamera->getController()->reset();
	}*/
	if(myCamera != NULL)
	{
		// Match the projection eye separation with the lens separation.
		myCamera->setEyeSeparation(-myHMDInfo.LensSeparationDistance / 2);
	}
#endif
}

///////////////////////////////////////////////////////////////////////////////
void OculusRiftService::stop() 
{}

///////////////////////////////////////////////////////////////////////////////
void OculusRiftService::initializeGraphics(Camera* cam, const DrawContext& context)
{
	myViewportSize = Vector2f(
		context.tile->pixelSize[0] * 2, context.tile->pixelSize[1] * 2);

	Renderer* r = context.renderer;

	myRenderTarget = r->createRenderTarget(RenderTarget::RenderToTexture);
	myRenderTexture = r->createTexture();
	myRenderTexture->initialize(myViewportSize[0], myViewportSize[1]);
	myDepthTexture = r->createTexture();
	myDepthTexture->initialize(myViewportSize[0], myViewportSize[1], GL_DEPTH_COMPONENT);
	myRenderTarget->setTextureTarget(myRenderTexture, myDepthTexture);

	// Setup shaders. Use some functions from the omegalib draw interface class 
	// to simplify shader and program creation.
	DrawInterface* di = r->getRenderer();
	GLuint vs = di->makeShaderFromSource(PostProcessVertexShaderSrc, 
		DrawInterface::VertexShader);

	GLuint fs = di->makeShaderFromSource(PostProcessFragShaderSrc, 
		DrawInterface::FragmentShader);

	myPostprocessProgram = di->createProgram(vs, fs);
	if(oglError) return;

	myLensCenterUniform = glGetUniformLocation(myPostprocessProgram, "LensCenter");
	myScreenCenterUniform = glGetUniformLocation(myPostprocessProgram, "ScreenCenter");
	myScaleUniform = glGetUniformLocation(myPostprocessProgram, "Scale");
	myScaleInUniform = glGetUniformLocation(myPostprocessProgram, "ScaleIn");
	myHmdWarpParamUniform = glGetUniformLocation(myPostprocessProgram, "HmdWarpParam");
	myTexture0Uniform = glGetUniformLocation(myPostprocessProgram, "Texture0");

	myInitialized = true;
}

///////////////////////////////////////////////////////////////////////////////
void OculusRiftService::beginDraw(Camera* cam, DrawContext& context)
{
	// Do we need to do rift postprocessing on this tile?
	if(context.tile->flags & RiftEnabledFlag)
	{
		if(context.task == DrawContext::SceneDrawTask)
		{
			// Create a render target if we have not done it yet.
			if(!myInitialized) initializeGraphics(cam, context);

			if(context.eye == DrawContext::EyeLeft)
			{
				context.viewport.min[0] = 0;
				context.viewport.max[0] = myViewportSize[0] / 2;
				context.viewport.min[1] = 0;
				context.viewport.max[1] = myViewportSize[1];
			}
			else if(context.eye == DrawContext::EyeRight)
			{
				context.viewport.min[0] = myViewportSize[0] / 2;
				context.viewport.max[0] = myViewportSize[0];
				context.viewport.min[1] = 0;
				context.viewport.max[1] = myViewportSize[1];
			}
			
			myRenderTarget->bind();
		}
		// After all overlay rendering is done we have our full side-by-side
		// picture in the target texture. Now render it to the output framebuffer
		// performing barrel distortion postprocessing.
		else if(context.task == DrawContext::OverlayDrawTask && 
			context.eye == DrawContext::EyeCyclop)
		{
			DrawInterface* di = context.renderer->getRenderer();
			di->beginDraw2D(context);

			beginEyeDraw();

			myScaleParams[0] = 0.25f * myScaleFactor; //0.25 * myScaleFactor;
			myScaleParams[1] = 0.25f * myScaleFactor * myAspectRatio;
			myScaleParams[2] = 4.0f;
			myScaleParams[3] = 4.0f / myAspectRatio;

			// Set uniforms common to left and right eye
			glUniform2f(myScaleUniform, myScaleParams[0],  myScaleParams[1]);

			glUniform2f(myScaleInUniform, myScaleParams[2], myScaleParams[3]);

			glUniform4f(myHmdWarpParamUniform,
				myDistortion[0],
				myDistortion[1],
				myDistortion[2],
				myDistortion[3]);

			// Set texture binding to texture unit 0 (the default used by the
			// drawRectTexture function).
			glUniform1i(myTexture0Uniform, 0);

			// Draw left eye
			// The left screen is centered at (0.25, 0.5)
			glUniform2f(myLensCenterUniform, 0.25f + myLensOffset, 0.5f);
			glUniform2f(myScreenCenterUniform, 0.25f, 0.5f);
			drawEyeQuad(-1, -1, 1, 2, 0, 0, 0.5, 1);

			// Draw right eye
			glUniform2f(myLensCenterUniform, 0.75f - myLensOffset, 0.5f);
			glUniform2f(myScreenCenterUniform, 0.75f, 0.5f);
			drawEyeQuad(0, -1, 1, 2, 0.5, 0, 0.5, 1);

			endEyeDraw();

			di->endDraw();

			myRenderTarget->clear();
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
void OculusRiftService::endDraw(Camera* cam, DrawContext& context)
{
	if(context.task == DrawContext::SceneDrawTask)
	{
		// Do we need to do rift postprocessing on this tile?
		if(context.tile->flags & RiftEnabledFlag)
		{
			myRenderTarget->unbind();
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
void OculusRiftService::beginEyeDraw()
{
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();

	glUseProgram(myPostprocessProgram);
	if(oglError) return;

	myRenderTexture->bind(GpuContext::TextureUnit0);
	if(oglError) return;
}

///////////////////////////////////////////////////////////////////////////////
void OculusRiftService::drawEyeQuad(
	float x, float y, float w, float h, float u, float v, float uw, float vh)
{
	glBegin(GL_QUADS);
	glTexCoord2f(u, v);
	glVertex2f(x, y);

	glTexCoord2f(u + uw, v);
	glVertex2f(x + w, y);

	glTexCoord2f(u + uw, v + vh);
	glVertex2f(x + w, y + h);

	glTexCoord2f(u, v + vh);
	glVertex2f(x, y + h);

	glEnd();
}

///////////////////////////////////////////////////////////////////////////////
void OculusRiftService::endEyeDraw()
{
	myRenderTexture->unbind();
	if(oglError) return;

	glUseProgram(0);

	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();

	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
}
