#include <omega.h>
#include <omegaGl.h>

using namespace omega;

///////////////////////////////////////////////////////////////////////////////
class OculusRiftService: public Service, ICameraListener
{
public:
	static OculusRiftService* New() { return new OculusRiftService(); }

	OculusRiftService():
		myCamera(NULL),
		myInitialized(false)
		{
		}

	// ICameraListener overrides
	virtual void beginDraw(Camera* cam, DrawContext& context);
	virtual void endDraw(Camera* cam, DrawContext& context);

private:
};

///////////////////////////////////////////////////////////////////////////////
// Python API follows

///////////////////////////////////////////////////////////////////////////////
// Python wrapper code.
#ifdef OMEGA_USE_PYTHON
#include "omega/PythonInterpreterWrapper.h"
BOOST_PYTHON_MODULE(rift)
{
}
#endif


///////////////////////////////////////////////////////////////////////////////
void OculusRiftService::initialize() 
{
	sInstance = this;
}

///////////////////////////////////////////////////////////////////////////////
void OculusRiftService::dispose() 
{
	sInstance = NULL;
}

///////////////////////////////////////////////////////////////////////////////
void OculusRiftService::start() 
{}

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
