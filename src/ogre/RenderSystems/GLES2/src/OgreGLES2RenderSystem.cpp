/*
-----------------------------------------------------------------------------
This source file is part of OGRE
    (Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org

Copyright (c) 2000-2009 Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/
#define NOMINMAX
#include "OgreGLES2RenderSystem.h"
#include "OgreGLES2TextureManager.h"
#include "OgreGLES2DepthBuffer.h"
#include "OgreGLES2HardwarePixelBuffer.h"
#include "OgreGLES2HardwareBufferManager.h"
#include "OgreGLES2HardwareIndexBuffer.h"
#include "OgreGLES2HardwareVertexBuffer.h"
#include "OgreGLES2GpuProgramManager.h"
#include "OgreGLES2Util.h"
#include "OgreGLES2FBORenderTexture.h"
#include "OgreGLSLESProgramFactory.h"

#if OGRE_PLATFORM == OGRE_PLATFORM_IPHONE
#   include "OgreEAGL2Window.h"
#elif OGRE_PLATFORM == OGRE_PLATFORM_ANDROID
#	include "OgreAndroidWindow.h"
#else
#   include "OgreEGLWindow.h"
#	ifndef GL_GLEXT_PROTOTYPES
    PFNGLMAPBUFFEROESPROC glMapBufferOES = NULL;
    PFNGLUNMAPBUFFEROESPROC glUnmapBufferOES = NULL;
    PFNGLDRAWBUFFERSARBPROC glDrawBuffersARB = NULL;
    PFNGLREADBUFFERNVPROC glReadBufferNV = NULL;
    PFNGLGETCOMPRESSEDTEXIMAGENVPROC glGetCompressedTexImageNV = NULL;
    PFNGLGETTEXIMAGENVPROC glGetTexImageNV = NULL;
    PFNGLGETTEXLEVELPARAMETERFVNVPROC glGetTexLevelParameterfvNV = NULL;
    PFNGLGETTEXLEVELPARAMETERiVNVPROC glGetTexLevelParameterivNV = NULL;
#	endif

#endif

// Convenience macro from ARB_vertex_buffer_object spec
#define VBO_BUFFER_OFFSET(i) ((char *)NULL + (i))

// Copy this definition from desktop GL.  Used for polygon modes.
#define GL_FILL    0x1B02

namespace Ogre {

    GLES2RenderSystem::GLES2RenderSystem()
        : mDepthWrite(true),
          mStencilMask(0xFFFFFFFF),
          mGpuProgramManager(0),
          mGLSLESProgramFactory(0),
          mHardwareBufferManager(0),
          mRTTManager(0)
    {
        size_t i;

		LogManager::getSingleton().logMessage(getName() + " created.");

        mGLSupport = getGLSupport();

        mWorldMatrix = Matrix4::IDENTITY;
        mViewMatrix = Matrix4::IDENTITY;

        mGLSupport->addConfig();

        mColourWrite[0] = mColourWrite[1] = mColourWrite[2] = mColourWrite[3] = true;

        for (i = 0; i < OGRE_MAX_TEXTURE_LAYERS; i++)
        {
            // Dummy value
            mTextureCoordIndex[i] = 99;
            mTextureTypes[i] = 0;
        }

        mActiveRenderTarget = 0;
        mCurrentContext = 0;
        mMainContext = 0;
        mGLInitialised = false;
        mTextureMipmapCount = 0;
        mMinFilter = FO_LINEAR;
        mMipFilter = FO_POINT;
        mCurrentVertexProgram = 0;
        mCurrentFragmentProgram = 0;
        mPolygonMode = GL_FILL;
    }

    GLES2RenderSystem::~GLES2RenderSystem()
    {
        shutdown();

		// Destroy render windows
        RenderTargetMap::iterator i;
        for (i = mRenderTargets.begin(); i != mRenderTargets.end(); ++i)
        {
            OGRE_DELETE i->second;
        }

        mRenderTargets.clear();
        OGRE_DELETE mGLSupport;
    }

    const String& GLES2RenderSystem::getName(void) const
    {
        static String strName("OpenGL ES 2.x Rendering Subsystem");
        return strName;
    }

    ConfigOptionMap& GLES2RenderSystem::getConfigOptions(void)
    {
        return mGLSupport->getConfigOptions();
    }

    void GLES2RenderSystem::setConfigOption(const String &name, const String &value)
    {
        mGLSupport->setConfigOption(name, value);
    }

    String GLES2RenderSystem::validateConfigOptions(void)
    {
		// XXX Return an error string if something is invalid
        return mGLSupport->validateConfig();
    }

    RenderWindow* GLES2RenderSystem::_initialise(bool autoCreateWindow,
                                                 const String& windowTitle)
    {
		mGLSupport->start();

        RenderWindow *autoWindow = mGLSupport->createWindow(autoCreateWindow,
                                                            this, windowTitle);
        RenderSystem::_initialise(autoCreateWindow, windowTitle);
        return autoWindow;
    }

    RenderSystemCapabilities* GLES2RenderSystem::createRenderSystemCapabilities() const
    {
        RenderSystemCapabilities* rsc = OGRE_NEW RenderSystemCapabilities();

        rsc->setCategoryRelevant(CAPS_CATEGORY_GL, true);
        rsc->setDriverVersion(mDriverVersion);

        const char* deviceName = (const char*)glGetString(GL_RENDERER);
		const char* vendorName = (const char*)glGetString(GL_VENDOR);        
        if (deviceName)
        {
            rsc->setDeviceName(deviceName);
        }

        rsc->setRenderSystemName(getName());

		
		// Determine vendor
		if (strstr(vendorName, "Imagination Technologies"))
			rsc->setVendor(GPU_IMAGINATION_TECHNOLOGIES);
		else if (strstr(vendorName, "Apple Computer, Inc."))
			rsc->setVendor(GPU_APPLE);  // iPhone Simulator
		else if (strstr(vendorName, "NVIDIA"))
			rsc->setVendor(GPU_NVIDIA);
        else
            rsc->setVendor(GPU_UNKNOWN);

        // Multitexturing support and set number of texture units
        GLint units;
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
        rsc->setNumTextureUnits(units);

        // Check for hardware stencil support and set bit depth
        GLint stencil;
        glGetIntegerv(GL_STENCIL_BITS, &stencil);
        GL_CHECK_ERROR;

        if(stencil)
        {
            rsc->setCapability(RSC_HWSTENCIL);
			rsc->setCapability(RSC_TWO_SIDED_STENCIL);
            rsc->setStencilBufferBitDepth(stencil);
        }

        // Scissor test is standard
        rsc->setCapability(RSC_SCISSOR_TEST);

        // Vertex Buffer Objects are always supported by OpenGL ES
        rsc->setCapability(RSC_VBO);

        // OpenGL ES - Check for these extensions too
        // For 2.0, http://www.khronos.org/registry/gles/api/2.0/gl2ext.h

        if (mGLSupport->checkExtension("GL_IMG_texture_compression_pvrtc") ||
            mGLSupport->checkExtension("GL_AMD_compressed_3DC_texture") ||
            mGLSupport->checkExtension("GL_AMD_compressed_ATC_texture") ||
            mGLSupport->checkExtension("GL_OES_compressed_ETC1_RGB8_texture") ||
            mGLSupport->checkExtension("GL_OES_compressed_paletted_texture"))
        {
            // TODO: Add support for compression types other than pvrtc
            rsc->setCapability(RSC_TEXTURE_COMPRESSION);

            if(mGLSupport->checkExtension("GL_IMG_texture_compression_pvrtc"))
                rsc->setCapability(RSC_TEXTURE_COMPRESSION_PVRTC);
        }

        if (mGLSupport->checkExtension("GL_EXT_texture_filter_anisotropic"))
            rsc->setCapability(RSC_ANISOTROPY);

        rsc->setCapability(RSC_FBO);
        rsc->setCapability(RSC_HWRENDER_TO_TEXTURE);
        rsc->setNumMultiRenderTargets(1);

        // Cube map
        rsc->setCapability(RSC_CUBEMAPPING);

        // Stencil wrapping
        rsc->setCapability(RSC_STENCIL_WRAP);

        // GL always shares vertex and fragment texture units (for now?)
        rsc->setVertexTextureUnitsShared(true);

        // Hardware support mipmapping
        rsc->setCapability(RSC_AUTOMIPMAP);

        // Blending support
        rsc->setCapability(RSC_BLENDING);
        rsc->setCapability(RSC_ADVANCED_BLEND_OPERATIONS);

        // DOT3 support is standard
        rsc->setCapability(RSC_DOT3);
        
        // Point size
        GLfloat psRange[2] = {0.0, 0.0};
        glGetFloatv(GL_ALIASED_POINT_SIZE_RANGE, psRange);
        GL_CHECK_ERROR;
        rsc->setMaxPointSize(psRange[1]);

        // Point sprites
        rsc->setCapability(RSC_POINT_SPRITES);
        rsc->setCapability(RSC_POINT_EXTENDED_PARAMETERS);
		
        // GLSL ES is always supported in GL ES 2
        rsc->addShaderProfile("glsles");
        LogManager::getSingleton().logMessage("GLSL ES support detected");
        
        // UBYTE4 always supported
        rsc->setCapability(RSC_VERTEX_FORMAT_UBYTE4);

        // Infinite far plane always supported
        rsc->setCapability(RSC_INFINITE_FAR_PLANE);

        // Vertex/Fragment Programs
        rsc->setCapability(RSC_VERTEX_PROGRAM);
        rsc->setCapability(RSC_FRAGMENT_PROGRAM);

        GLfloat floatConstantCount = 0;
        glGetFloatv(GL_MAX_VERTEX_UNIFORM_VECTORS, &floatConstantCount);
        rsc->setVertexProgramConstantFloatCount((Ogre::ushort)floatConstantCount);
        rsc->setVertexProgramConstantBoolCount((Ogre::ushort)floatConstantCount);
        rsc->setVertexProgramConstantIntCount((Ogre::ushort)floatConstantCount);

        // Fragment Program Properties
        floatConstantCount = 0;
        glGetFloatv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &floatConstantCount);
        rsc->setFragmentProgramConstantFloatCount((Ogre::ushort)floatConstantCount);
        rsc->setFragmentProgramConstantBoolCount((Ogre::ushort)floatConstantCount);
        rsc->setFragmentProgramConstantIntCount((Ogre::ushort)floatConstantCount);

        // Check for Float textures
//        rsc->setCapability(RSC_TEXTURE_FLOAT);

        // Alpha to coverage always 'supported' when MSAA is available
        // although card may ignore it if it doesn't specifically support A2C
        rsc->setCapability(RSC_ALPHA_TO_COVERAGE);
		
		// No point sprites, so no size
		rsc->setMaxPointSize(0.f);
        
        return rsc;
    }

    void GLES2RenderSystem::initialiseFromRenderSystemCapabilities(RenderSystemCapabilities* caps, RenderTarget* primary)
    {
        if(caps->getRenderSystemName() != getName())
        {
            OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS,
                        "Trying to initialize GLES2RenderSystem from RenderSystemCapabilities that do not support OpenGL ES",
                        "GLES2RenderSystem::initialiseFromRenderSystemCapabilities");
        }

        mGpuProgramManager = OGRE_NEW GLES2GpuProgramManager();

        mGLSLESProgramFactory = OGRE_NEW GLSLESProgramFactory();
        HighLevelGpuProgramManager::getSingleton().addFactory(mGLSLESProgramFactory);

        // Set texture the number of texture units
        mFixedFunctionTextureUnits = caps->getNumTextureUnits();

        // Use VBO's by default
        mHardwareBufferManager = OGRE_NEW GLES2HardwareBufferManager();

        ConfigOptionMap::iterator cfi = getConfigOptions().find("RTT Preferred Mode");
		// RTT Mode: 0 use whatever available, 1 use PBuffers, 2 force use copying
		int rttMode = 0;
		if (cfi != getConfigOptions().end())
		{
			if (cfi->second.currentValue == "PBuffer")
			{
				rttMode = 1;
			}
			else if (cfi->second.currentValue == "Copy")
			{
				rttMode = 2;
			}
		}

		// Check for framebuffer object extension
		if(caps->hasCapability(RSC_FBO) && rttMode < 1)
		{
            // Create FBO manager
            LogManager::getSingleton().logMessage("GL ES 2: Using FBOs for rendering to textures");
            mRTTManager = OGRE_NEW_FIX_FOR_WIN32 GLES2FBOManager();
            caps->setCapability(RSC_RTT_SEPARATE_DEPTHBUFFER);
        }
		else
		{
			// Check GLSupport for PBuffer support
			if(caps->hasCapability(RSC_PBUFFER) && rttMode < 2)
			{
				if(caps->hasCapability(RSC_HWRENDER_TO_TEXTURE))
				{
					// Use PBuffers
//					mRTTManager = new GLES2PBRTTManager(mGLSupport, primary);
					LogManager::getSingleton().logMessage("GL ES 2: Using PBuffers for rendering to textures");

					// TODO: Depth buffer sharing in pbuffer is left unsupported
				}
			}
			else
			{
				// No pbuffer support either -- fallback to simplest copying from framebuffer
				mRTTManager = new GLES2CopyingRTTManager();
				LogManager::getSingleton().logMessage("GL ES 2: Using framebuffer copy for rendering to textures (worst)");
				LogManager::getSingleton().logMessage("GL ES 2: Warning: RenderTexture size is restricted to size of framebuffer. If you are on Linux, consider using GLX instead of SDL.");

				//Copy method uses the main depth buffer but no other depth buffer
                caps->setCapability(RSC_RTT_MAIN_DEPTHBUFFER_ATTACHABLE);
				caps->setCapability(RSC_RTT_DEPTHBUFFER_RESOLUTION_LESSEQUAL);
			}

			// Downgrade number of simultaneous targets
			caps->setNumMultiRenderTargets(1);
		}

		Log* defaultLog = LogManager::getSingleton().getDefaultLog();
		if (defaultLog)
		{
			caps->log(defaultLog);
		}

        mTextureManager = OGRE_NEW GLES2TextureManager(*mGLSupport);

        if (RTShader::ShaderGenerator::initialize())
        {
            // Grab the shader generator pointer.
            mShaderGenerator = RTShader::ShaderGenerator::getSingletonPtr();

            // Set shader cache path.
            mShaderGenerator->setShaderCachePath(mGLSupport->getShaderCachePath());
            mShaderGenerator->setTargetLanguage("glsles");		

            mMaterialMgrListener = OGRE_NEW_FIX_FOR_WIN32 ShaderGeneratorTechniqueResolverListener(mShaderGenerator);				
            MaterialManager::getSingleton().addListener(mMaterialMgrListener);

            // Add the shader libs and cached resource location.
            ResourceGroupManager::getSingleton().addResourceLocation(mGLSupport->getShaderLibraryPath(), "FileSystem");
            ResourceGroupManager::getSingleton().addResourceLocation(mGLSupport->getShaderCachePath(), "FileSystem");
        }

        GL_CHECK_ERROR;
        mGLInitialised = true;
    }

    void GLES2RenderSystem::reinitialise(void)
    {
        this->shutdown();
        this->_initialise(true);
    }

    void GLES2RenderSystem::shutdown(void)
    {
        RenderSystem::shutdown();

        // Deleting the GLSL program factory
		if (mGLSLESProgramFactory)
		{
			// Remove from manager safely
			if (HighLevelGpuProgramManager::getSingletonPtr())
				HighLevelGpuProgramManager::getSingleton().removeFactory(mGLSLESProgramFactory);
			OGRE_DELETE mGLSLESProgramFactory;
			mGLSLESProgramFactory = 0;
		}

        // Restore default scheme.
        MaterialManager::getSingleton().setActiveScheme(MaterialManager::DEFAULT_SCHEME_NAME);

        // Unregister the material manager listener.
        if (mMaterialMgrListener != NULL)
        {			
            MaterialManager::getSingleton().removeListener(mMaterialMgrListener);
            OGRE_DELETE mMaterialMgrListener;
            mMaterialMgrListener = NULL;
        }

        // Finalize CRTShader system.
        if (mShaderGenerator != NULL)
        {
            RTShader::ShaderGenerator::finalize();
            mShaderGenerator = NULL;
        }

		// Deleting the GPU program manager and hardware buffer manager.  Has to be done before the mGLSupport->stop().
        OGRE_DELETE mGpuProgramManager;
        mGpuProgramManager = 0;

        OGRE_DELETE mHardwareBufferManager;
        mHardwareBufferManager = 0;

        OGRE_DELETE mRTTManager;
        mRTTManager = 0;

        mGLSupport->stop();

        OGRE_DELETE mTextureManager;
        mTextureManager = 0;

        mGLInitialised = 0;
    }

    void GLES2RenderSystem::setAmbientLight(float r, float g, float b)
    {
        // Not supported
    }

    void GLES2RenderSystem::setShadingType(ShadeOptions so)
    {
        // Not supported
    }

    void GLES2RenderSystem::setLightingEnabled(bool enabled)
    {
        // Not supported
    }

    RenderWindow* GLES2RenderSystem::_createRenderWindow(const String &name, unsigned int width, unsigned int height,
                                                        bool fullScreen, const NameValuePairList *miscParams)
    {
        if (mRenderTargets.find(name) != mRenderTargets.end())
        {
            OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS,
                        "NativeWindowType with name '" + name + "' already exists",
                        "GLES2RenderSystem::_createRenderWindow");
        }

		// Log a message
        StringStream ss;
        ss << "GLES2RenderSystem::_createRenderWindow \"" << name << "\", " <<
            width << "x" << height << " ";
        if (fullScreen)
            ss << "fullscreen ";
        else
            ss << "windowed ";

        if (miscParams)
        {
            ss << " miscParams: ";
            NameValuePairList::const_iterator it;
            for (it = miscParams->begin(); it != miscParams->end(); ++it)
            {
                ss << it->first << "=" << it->second << " ";
            }

            LogManager::getSingleton().logMessage(ss.str());
        }

		// Create the window
        RenderWindow* win = mGLSupport->newWindow(name, width, height, fullScreen, miscParams);
        attachRenderTarget((Ogre::RenderTarget&) *win);

        if (!mGLInitialised)
        {
            initialiseContext(win);
			
            StringVector tokens = StringUtil::split(mGLSupport->getGLVersion(), ".");
            if (!tokens.empty())
            {
                mDriverVersion.major = StringConverter::parseInt(tokens[0]);
                if (tokens.size() > 1)
                    mDriverVersion.minor = StringConverter::parseInt(tokens[1]);
                if (tokens.size() > 2)
                    mDriverVersion.release = StringConverter::parseInt(tokens[2]);
            }
            mDriverVersion.build = 0;
			
			// Initialise GL after the first window has been created
			// TODO: fire this from emulation options, and don't duplicate Real and Current capabilities
            mRealCapabilities = createRenderSystemCapabilities();

			// use real capabilities if custom capabilities are not available
            if (!mUseCustomCapabilities)
                mCurrentCapabilities = mRealCapabilities;

            initialiseFromRenderSystemCapabilities(mCurrentCapabilities, (RenderTarget *) win);

			// Initialise the main context
            _oneTimeContextInitialization();
            if (mCurrentContext)
                mCurrentContext->setInitialized();
        }

		if( win->getDepthBufferPool() != DepthBuffer::POOL_NO_DEPTH )
		{
			// Unlike D3D9, OGL doesn't allow sharing the main depth buffer, so keep them separate.
			// Only Copy does, but Copy means only one depth buffer...
			GLES2Context *windowContext;
			win->getCustomAttribute( "GLCONTEXT", &windowContext );
			GLES2DepthBuffer *depthBuffer = OGRE_NEW GLES2DepthBuffer( DepthBuffer::POOL_DEFAULT, this,
															windowContext, 0, 0,
															win->getWidth(), win->getHeight(),
															win->getFSAA(), 0, true );

			mDepthBufferPool[depthBuffer->getPoolId()].push_back( depthBuffer );

			win->attachDepthBuffer( depthBuffer );
		}

        return win;
    }

	//---------------------------------------------------------------------
	DepthBuffer* GLES2RenderSystem::_createDepthBufferFor( RenderTarget *renderTarget )
	{
		GLES2DepthBuffer *retVal = 0;

		// Only FBO & pbuffer support different depth buffers, so everything
		// else creates dummy (empty) containers
		// retVal = mRTTManager->_createDepthBufferFor( renderTarget );
		GLES2FrameBufferObject *fbo = 0;
        renderTarget->getCustomAttribute("FBO", &fbo);

		if( fbo )
		{
			// Presence of an FBO means the manager is an FBO Manager, that's why it's safe to downcast
			// Find best depth & stencil format suited for the RT's format
			GLuint depthFormat, stencilFormat;
			static_cast<GLES2FBOManager*>(mRTTManager)->getBestDepthStencil( fbo->getFormat(),
																		&depthFormat, &stencilFormat );

			GLES2RenderBuffer *depthBuffer = OGRE_NEW GLES2RenderBuffer( depthFormat, fbo->getWidth(),
																fbo->getHeight(), fbo->getFSAA() );

			GLES2RenderBuffer *stencilBuffer = depthBuffer;
			if( depthFormat != GL_DEPTH24_STENCIL8_OES && stencilBuffer != GL_NONE )
			{
				stencilBuffer = OGRE_NEW GLES2RenderBuffer( stencilFormat, fbo->getWidth(),
													fbo->getHeight(), fbo->getFSAA() );
			}

			// No "custom-quality" multisample for now in GL
			retVal = OGRE_NEW GLES2DepthBuffer( 0, this, mCurrentContext, depthBuffer, stencilBuffer,
										fbo->getWidth(), fbo->getHeight(), fbo->getFSAA(), 0, false );
		}

		return retVal;
	}
	//---------------------------------------------------------------------
	void GLES2RenderSystem::_getDepthStencilFormatFor( GLenum internalColourFormat, GLenum *depthFormat,
                                                      GLenum *stencilFormat )
	{
		mRTTManager->getBestDepthStencil( internalColourFormat, depthFormat, stencilFormat );
	}

    MultiRenderTarget* GLES2RenderSystem::createMultiRenderTarget(const String & name)
    {
        MultiRenderTarget *retval = mRTTManager->createMultiRenderTarget(name);
        attachRenderTarget(*retval);
        return retval;
    }

    void GLES2RenderSystem::destroyRenderWindow(RenderWindow* pWin)
    {
		// Find it to remove from list
        RenderTargetMap::iterator i = mRenderTargets.begin();

        while (i != mRenderTargets.end())
        {
            if (i->second == pWin)
            {
				GLES2Context *windowContext;
				pWin->getCustomAttribute("GLCONTEXT", &windowContext);

				// 1 Window <-> 1 Context, should be always true
				assert( windowContext );

				bool bFound = false;
				// Find the depth buffer from this window and remove it.
				DepthBufferMap::iterator itMap = mDepthBufferPool.begin();
				DepthBufferMap::iterator enMap = mDepthBufferPool.end();

				while( itMap != enMap && !bFound )
				{
					DepthBufferVec::iterator itor = itMap->second.begin();
					DepthBufferVec::iterator end  = itMap->second.end();

					while( itor != end )
					{
						// A DepthBuffer with no depth & stencil pointers is a dummy one,
						// look for the one that matches the same GL context
						GLES2DepthBuffer *depthBuffer = static_cast<GLES2DepthBuffer*>(*itor);
						GLES2Context *glContext = depthBuffer->getGLContext();

						if( glContext == windowContext &&
							depthBuffer->getDepthBuffer() || depthBuffer->getStencilBuffer() )
						{
							bFound = true;

							delete *itor;
							itMap->second.erase( itor );
							break;
						}
						++itor;
					}

					++itMap;
				}

                mRenderTargets.erase(i);
                OGRE_DELETE pWin;
                break;
            }
        }
    }

    String GLES2RenderSystem::getErrorDescription(long errorNumber) const
    {
        // TODO find a way to get error string
//        const GLubyte *errString = gluErrorString (errCode);
//        return (errString != 0) ? String((const char*) errString) : StringUtil::BLANK;

        return StringUtil::BLANK;
    }

    VertexElementType GLES2RenderSystem::getColourVertexElementType(void) const
    {
        return VET_COLOUR_ABGR;
    }

    void GLES2RenderSystem::setNormaliseNormals(bool normalise)
    {
        // Not supported
    }

    void GLES2RenderSystem::_useLights(const LightList& lights, unsigned short limit)
    {
        // Not supported
    }

    void GLES2RenderSystem::_setWorldMatrix(const Matrix4 &m)
    {
        mWorldMatrix = m;
    }

    void GLES2RenderSystem::_setViewMatrix(const Matrix4 &m)
    {
        mViewMatrix = m;

        // Also mark clip planes dirty
        if (!mClipPlanes.empty())
        {
            mClipPlanesDirty = true;
        }
    }

    void GLES2RenderSystem::_setProjectionMatrix(const Matrix4 &m)
    {
		// Nothing to do but mark clip planes dirty
        if (!mClipPlanes.empty())
            mClipPlanesDirty = true;
    }

    void GLES2RenderSystem::_setTexture(size_t stage, bool enabled, const TexturePtr &texPtr)
    {
		GLES2TexturePtr tex = texPtr;

		GLenum lastTextureType = mTextureTypes[stage];

		if (!activateGLTextureUnit(stage))
			return;

		if (enabled)
		{
			if (!tex.isNull())
			{
				// Note used
				tex->touch();
				mTextureTypes[stage] = tex->getGLES2TextureTarget();

                // Store the number of mipmaps
                mTextureMipmapCount = tex->getNumMipmaps();
			}
			else
				// Assume 2D
				mTextureTypes[stage] = GL_TEXTURE_2D;

			if(lastTextureType != mTextureTypes[stage] && lastTextureType != 0)
			{
				if (stage < mFixedFunctionTextureUnits)
				{
					glDisable( lastTextureType );
				}
			}

			if (stage < mFixedFunctionTextureUnits)
			{
				glEnable( mTextureTypes[stage] );
			}

			if(!tex.isNull())
				glBindTexture( mTextureTypes[stage], tex->getGLID() );
			else
				glBindTexture( mTextureTypes[stage], static_cast<GLES2TextureManager*>(mTextureManager)->getWarningTextureID() );
		}
		else
		{
			if (stage < mFixedFunctionTextureUnits)
			{
				if (lastTextureType != 0)
				{
					glDisable( mTextureTypes[stage] );
				}
			}
			// Bind zero texture
			glBindTexture(GL_TEXTURE_2D, 0); 
		}

		activateGLTextureUnit(0);
    }

    void GLES2RenderSystem::_setTextureCoordSet(size_t stage, size_t index)
    {
		mTextureCoordIndex[stage] = index;
    }

    void GLES2RenderSystem::_setTextureCoordCalculation(size_t stage,
                                                       TexCoordCalcMethod m,
                                                       const Frustum* frustum)
    {
        // Not supported
    }

    void GLES2RenderSystem::_setTextureBlendMode(size_t stage, const LayerBlendModeEx& bm)
    {
        // Not supported
    }

    GLint GLES2RenderSystem::getTextureAddressingMode(TextureUnitState::TextureAddressingMode tam) const
    {
        switch (tam)
        {
            case TextureUnitState::TAM_CLAMP:
            case TextureUnitState::TAM_BORDER:
                return GL_CLAMP_TO_EDGE;
            case TextureUnitState::TAM_MIRROR:
                return GL_MIRRORED_REPEAT;
            case TextureUnitState::TAM_WRAP:
            default:
                return GL_REPEAT;
        }
    }

    void GLES2RenderSystem::_setTextureAddressingMode(size_t stage, const TextureUnitState::UVWAddressingMode& uvw)
    {
		if (!activateGLTextureUnit(stage))
			return;
		glTexParameteri( mTextureTypes[stage], GL_TEXTURE_WRAP_S, getTextureAddressingMode(uvw.u));
        GL_CHECK_ERROR;
		glTexParameteri( mTextureTypes[stage], GL_TEXTURE_WRAP_T, getTextureAddressingMode(uvw.v));
        GL_CHECK_ERROR;
        activateGLTextureUnit(0);
    }

    void GLES2RenderSystem::_setTextureBorderColour(size_t stage, const ColourValue& colour)
    {
        // Not supported
    }

    void GLES2RenderSystem::_setTextureMipmapBias(size_t unit, float bias)
    {
        // Not supported
    }

    void GLES2RenderSystem::_setTextureMatrix(size_t stage, const Matrix4& xform)
    {
        // Not supported
    }

    GLenum GLES2RenderSystem::getBlendMode(SceneBlendFactor ogreBlend) const
    {
        switch (ogreBlend)
        {
            case SBF_ONE:
                return GL_ONE;
            case SBF_ZERO:
                return GL_ZERO;
            case SBF_DEST_COLOUR:
                return GL_DST_COLOR;
            case SBF_SOURCE_COLOUR:
                return GL_SRC_COLOR;
            case SBF_ONE_MINUS_DEST_COLOUR:
                return GL_ONE_MINUS_DST_COLOR;
            case SBF_ONE_MINUS_SOURCE_COLOUR:
                return GL_ONE_MINUS_SRC_COLOR;
            case SBF_DEST_ALPHA:
                return GL_DST_ALPHA;
            case SBF_SOURCE_ALPHA:
                return GL_SRC_ALPHA;
            case SBF_ONE_MINUS_DEST_ALPHA:
                return GL_ONE_MINUS_DST_ALPHA;
            case SBF_ONE_MINUS_SOURCE_ALPHA:
                return GL_ONE_MINUS_SRC_ALPHA;
        };

        // To keep compiler happy
        return GL_ONE;
    }

	void GLES2RenderSystem::_setSceneBlending(SceneBlendFactor sourceFactor, SceneBlendFactor destFactor, SceneBlendOperation op)
	{
        GL_CHECK_ERROR;
		GLenum sourceBlend = getBlendMode(sourceFactor);
		GLenum destBlend = getBlendMode(destFactor);
		if(sourceFactor == SBF_ONE && destFactor == SBF_ZERO)
		{
			glDisable(GL_BLEND);
			GL_CHECK_ERROR;
		}
		else
		{
			glEnable(GL_BLEND);
			GL_CHECK_ERROR;
			glBlendFunc(sourceBlend, destBlend);
			GL_CHECK_ERROR;
		}
        
        GLint func = GL_FUNC_ADD;
		switch(op)
		{
		case SBO_ADD:
			func = GL_FUNC_ADD;
			break;
		case SBO_SUBTRACT:
			func = GL_FUNC_SUBTRACT;
			break;
		case SBO_REVERSE_SUBTRACT:
			func = GL_FUNC_REVERSE_SUBTRACT;
			break;
		case SBO_MIN:
#if GL_EXT_blend_minmax
            func = GL_MIN_EXT;
#endif
            break;
		case SBO_MAX:
#if GL_EXT_blend_minmax
            func = GL_MAX_EXT;
#endif
			break;
		}

        glBlendEquation(func);
        GL_CHECK_ERROR;
	}

	void GLES2RenderSystem::_setSeparateSceneBlending(
        SceneBlendFactor sourceFactor, SceneBlendFactor destFactor,
        SceneBlendFactor sourceFactorAlpha, SceneBlendFactor destFactorAlpha,
        SceneBlendOperation op, SceneBlendOperation alphaOp )
	{
        GLenum sourceBlend = getBlendMode(sourceFactor);
        GLenum destBlend = getBlendMode(destFactor);
        GLenum sourceBlendAlpha = getBlendMode(sourceFactorAlpha);
        GLenum destBlendAlpha = getBlendMode(destFactorAlpha);
        
        if(sourceFactor == SBF_ONE && destFactor == SBF_ZERO && 
           sourceFactorAlpha == SBF_ONE && destFactorAlpha == SBF_ZERO)
        {
            glDisable(GL_BLEND);
            GL_CHECK_ERROR;
        }
        else
        {
            glEnable(GL_BLEND);
            GL_CHECK_ERROR;
            glBlendFuncSeparate(sourceBlend, destBlend, sourceBlendAlpha, destBlendAlpha);
            GL_CHECK_ERROR;
        }
        
        GLint func = GL_FUNC_ADD, alphaFunc = GL_FUNC_ADD;
        
        switch(op)
        {
            case SBO_ADD:
                func = GL_FUNC_ADD;
                break;
            case SBO_SUBTRACT:
                func = GL_FUNC_SUBTRACT;
                break;
            case SBO_REVERSE_SUBTRACT:
                func = GL_FUNC_REVERSE_SUBTRACT;
                break;
            case SBO_MIN:
#if defined(GL_EXT_blend_minmax)
                func = GL_MIN_EXT;
#endif
                break;
            case SBO_MAX:
#if defined(GL_EXT_blend_minmax)
                func = GL_MAX_EXT;
#endif
                break;
        }
        
        switch(alphaOp)
        {
            case SBO_ADD:
                alphaFunc = GL_FUNC_ADD;
                break;
            case SBO_SUBTRACT:
                alphaFunc = GL_FUNC_SUBTRACT;
                break;
            case SBO_REVERSE_SUBTRACT:
                alphaFunc = GL_FUNC_REVERSE_SUBTRACT;
                break;
            case SBO_MIN:
#if defined(GL_EXT_blend_minmax)
                alphaFunc = GL_MIN_EXT;
#endif
                break;
            case SBO_MAX:
#if defined(GL_EXT_blend_minmax)
                alphaFunc = GL_MAX_EXT;
#endif
                break;
        }
        
        glBlendEquationSeparate(func, alphaFunc);
        GL_CHECK_ERROR;
	}

    void GLES2RenderSystem::_setAlphaRejectSettings(CompareFunction func, unsigned char value, bool alphaToCoverage)
    {
		bool a2c = false;
		static bool lasta2c = false;

        if(func != CMPF_ALWAYS_PASS)
		{
			a2c = alphaToCoverage;
        }

		if (a2c != lasta2c && getCapabilities()->hasCapability(RSC_ALPHA_TO_COVERAGE))
		{
			if (a2c)
				glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
			else
				glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
            GL_CHECK_ERROR;

			lasta2c = a2c;
		}
	}

    void GLES2RenderSystem::_setViewport(Viewport *vp)
    {
		// Check if viewport is different
        if (vp != mActiveViewport || vp->_isUpdated())
        {
            RenderTarget* target;

            target = vp->getTarget();
            _setRenderTarget(target);
            mActiveViewport = vp;

            GLsizei x, y, w, h;

			// Calculate the "lower-left" corner of the viewport
            w = vp->getActualWidth();
            h = vp->getActualHeight();

            x = vp->getActualLeft();
            y = vp->getActualTop();

            if (!target->requiresTextureFlipping())
            {
                // Convert "upper-left" corner to "lower-left"
                y = target->getHeight() - h - y;
            }
            glViewport(x, y, w, h);
            GL_CHECK_ERROR;

			// Configure the viewport clipping
            glScissor(x, y, w, h);
            GL_CHECK_ERROR;

            vp->_clearUpdatedFlag();
        }
    }

    void GLES2RenderSystem::_beginFrame(void)
    {
        if (!mActiveViewport)
            OGRE_EXCEPT(Exception::ERR_INVALID_STATE,
                        "Cannot begin frame - no viewport selected.",
                        "GLES2RenderSystem::_beginFrame");

        glEnable(GL_SCISSOR_TEST);
        GL_CHECK_ERROR;
    }

    void GLES2RenderSystem::_endFrame(void)
    {
        // Deactivate the viewport clipping.
        glDisable(GL_SCISSOR_TEST);
        GL_CHECK_ERROR;

		// unbind GPU programs at end of frame
		// this is mostly to avoid holding bound programs that might get deleted
		// outside via the resource manager
		unbindGpuProgram(GPT_VERTEX_PROGRAM);
		unbindGpuProgram(GPT_FRAGMENT_PROGRAM);
    }

    void GLES2RenderSystem::_setCullingMode(CullingMode mode)
    {
        mCullingMode = mode;
        // NB: Because two-sided stencil API dependence of the front face, we must
        // use the same 'winding' for the front face everywhere. As the OGRE default
        // culling mode is clockwise, we also treat anticlockwise winding as front
        // face for consistently. On the assumption that, we can't change the front
        // face by glFrontFace anywhere.

        GLenum cullMode;

        switch( mode )
        {
            case CULL_NONE:
                glDisable(GL_CULL_FACE);
                GL_CHECK_ERROR;
                return;

            default:
            case CULL_CLOCKWISE:
                if (mActiveRenderTarget &&
                    ((mActiveRenderTarget->requiresTextureFlipping() && !mInvertVertexWinding) ||
                     (!mActiveRenderTarget->requiresTextureFlipping() && mInvertVertexWinding)))
                {
                    cullMode = GL_FRONT;
                }
                else
                {
                    cullMode = GL_BACK;
                }
                break;
            case CULL_ANTICLOCKWISE:
                if (mActiveRenderTarget && 
                    ((mActiveRenderTarget->requiresTextureFlipping() && !mInvertVertexWinding) ||
                    (!mActiveRenderTarget->requiresTextureFlipping() && mInvertVertexWinding)))
                {
                    cullMode = GL_BACK;
                }
                else
                {
                    cullMode = GL_FRONT;
                }
                break;
        }

        glEnable(GL_CULL_FACE);
        GL_CHECK_ERROR;
        glCullFace(cullMode);
        GL_CHECK_ERROR;
    }

    void GLES2RenderSystem::_setDepthBufferParams(bool depthTest, bool depthWrite, CompareFunction depthFunction)
    {
        _setDepthBufferCheckEnabled(depthTest);
        _setDepthBufferWriteEnabled(depthWrite);
        _setDepthBufferFunction(depthFunction);
    }

    void GLES2RenderSystem::_setDepthBufferCheckEnabled(bool enabled)
    {
        if (enabled)
        {
            glClearDepthf(1.0f);
            GL_CHECK_ERROR;
            glEnable(GL_DEPTH_TEST);
            GL_CHECK_ERROR;
        }
        else
        {
            glDisable(GL_DEPTH_TEST);
            GL_CHECK_ERROR;
        }
    }

    void GLES2RenderSystem::_setDepthBufferWriteEnabled(bool enabled)
    {
        GLboolean flag = enabled ? GL_TRUE : GL_FALSE;
        glDepthMask(flag);
        GL_CHECK_ERROR;

        // Store for reference in _beginFrame
        mDepthWrite = enabled;
    }

    void GLES2RenderSystem::_setDepthBufferFunction(CompareFunction func)
    {
        glDepthFunc(convertCompareFunction(func));
        GL_CHECK_ERROR;
    }

    void GLES2RenderSystem::_setDepthBias(float constantBias, float slopeScaleBias)
    {
        if (constantBias != 0 || slopeScaleBias != 0)
        {
            glEnable(GL_POLYGON_OFFSET_FILL);
            GL_CHECK_ERROR;
            glPolygonOffset(-slopeScaleBias, -constantBias);
            GL_CHECK_ERROR;
        }
        else
        {
            glDisable(GL_POLYGON_OFFSET_FILL);
            GL_CHECK_ERROR;
        }
    }

    void GLES2RenderSystem::_setColourBufferWriteEnabled(bool red, bool green, bool blue, bool alpha)
    {
        glColorMask(red, green, blue, alpha);
        GL_CHECK_ERROR;

        // record this
        mColourWrite[0] = red;
        mColourWrite[1] = blue;
        mColourWrite[2] = green;
        mColourWrite[3] = alpha;
    }

    void GLES2RenderSystem::_setFog(FogMode mode, const ColourValue& colour, Real density, Real start, Real end)
    {
    }

    void GLES2RenderSystem::_convertProjectionMatrix(const Matrix4& matrix,
                                                  Matrix4& dest,
                                                  bool forGpuProgram)
    {
		// no any conversion request for OpenGL
        dest = matrix;
    }

    void GLES2RenderSystem::_makeProjectionMatrix(const Radian& fovy, Real aspect,
                                               Real nearPlane, Real farPlane,
                                               Matrix4& dest, bool forGpuProgram)
    {
        Radian thetaY(fovy / 2.0f);
        Real tanThetaY = Math::Tan(thetaY);

		// Calc matrix elements
        Real w = (1.0f / tanThetaY) / aspect;
        Real h = 1.0f / tanThetaY;
        Real q, qn;
        if (farPlane == 0)
        {
			// Infinite far plane
            q = Frustum::INFINITE_FAR_PLANE_ADJUST - 1;
            qn = nearPlane * (Frustum::INFINITE_FAR_PLANE_ADJUST - 2);
        }
        else
        {
            q = -(farPlane + nearPlane) / (farPlane - nearPlane);
            qn = -2 * (farPlane * nearPlane) / (farPlane - nearPlane);
        }

		// NB This creates Z in range [-1,1]
		//
		// [ w   0   0   0  ]
		// [ 0   h   0   0  ]
		// [ 0   0   q   qn ]
		// [ 0   0   -1  0  ]

        dest = Matrix4::ZERO;
        dest[0][0] = w;
        dest[1][1] = h;
        dest[2][2] = q;
        dest[2][3] = qn;
        dest[3][2] = -1;
    }

    void GLES2RenderSystem::_makeProjectionMatrix(Real left, Real right,
                                               Real bottom, Real top,
                                               Real nearPlane, Real farPlane,
                                               Matrix4& dest, bool forGpuProgram)
    {
        Real width = right - left;
        Real height = top - bottom;
        Real q, qn;
        if (farPlane == 0)
        {
			// Infinite far plane
            q = Frustum::INFINITE_FAR_PLANE_ADJUST - 1;
            qn = nearPlane * (Frustum::INFINITE_FAR_PLANE_ADJUST - 2);
        }
        else
        {
            q = -(farPlane + nearPlane) / (farPlane - nearPlane);
            qn = -2 * (farPlane * nearPlane) / (farPlane - nearPlane);
        }

        dest = Matrix4::ZERO;
        dest[0][0] = 2 * nearPlane / width;
        dest[0][2] = (right+left) / width;
        dest[1][1] = 2 * nearPlane / height;
        dest[1][2] = (top+bottom) / height;
        dest[2][2] = q;
        dest[2][3] = qn;
        dest[3][2] = -1;
    }

    void GLES2RenderSystem::_makeOrthoMatrix(const Radian& fovy, Real aspect,
                                          Real nearPlane, Real farPlane,
                                          Matrix4& dest, bool forGpuProgram)
    {
        Radian thetaY(fovy / 2.0f);
        Real tanThetaY = Math::Tan(thetaY);

        // Real thetaX = thetaY * aspect;
        Real tanThetaX = tanThetaY * aspect; // Math::Tan(thetaX);
        Real half_w = tanThetaX * nearPlane;
        Real half_h = tanThetaY * nearPlane;
        Real iw = 1.0 / half_w;
        Real ih = 1.0 / half_h;
        Real q;
        if (farPlane == 0)
        {
            q = 0;
        }
        else
        {
            q = 2.0 / (farPlane - nearPlane);
        }
        dest = Matrix4::ZERO;
        dest[0][0] = iw;
        dest[1][1] = ih;
        dest[2][2] = -q;
        dest[2][3] = -(farPlane + nearPlane) / (farPlane - nearPlane);
        dest[3][3] = 1;
    }

    void GLES2RenderSystem::_applyObliqueDepthProjection(Matrix4& matrix,
                                                      const Plane& plane,
                                                      bool forGpuProgram)
    {
		// Thanks to Eric Lenyel for posting this calculation at www.terathon.com
        
		// Calculate the clip-space corner point opposite the clipping plane
		// as (sgn(clipPlane.x), sgn(clipPlane.y), 1, 1) and
		// transform it into camera space by multiplying it
		// by the inverse of the projection matrix

        Vector4 q;
        q.x = (Math::Sign(plane.normal.x) + matrix[0][2]) / matrix[0][0];
        q.y = (Math::Sign(plane.normal.y) + matrix[1][2]) / matrix[1][1];
        q.z = -1.0F;
        q.w = (1.0F + matrix[2][2]) / matrix[2][3];

        // Calculate the scaled plane vector
        Vector4 clipPlane4d(plane.normal.x, plane.normal.y, plane.normal.z, plane.d);
        Vector4 c = clipPlane4d * (2.0F / (clipPlane4d.dotProduct(q)));

        // Replace the third row of the projection matrix
        matrix[2][0] = c.x;
        matrix[2][1] = c.y;
        matrix[2][2] = c.z + 1.0F;
        matrix[2][3] = c.w; 
    }

    void GLES2RenderSystem::_setPolygonMode(PolygonMode level)
    {
        switch(level)
        {
        case PM_POINTS:
            mPolygonMode = GL_POINTS;
            break;
        case PM_WIREFRAME:
            mPolygonMode = GL_LINE_STRIP;
            break;
        default:
        case PM_SOLID:
            mPolygonMode = GL_FILL;
            break;
        }
    }

    void GLES2RenderSystem::setStencilCheckEnabled(bool enabled)
    {
        if (enabled)
        {
            glEnable(GL_STENCIL_TEST);
        }
        else
        {
            glDisable(GL_STENCIL_TEST);
        }
        GL_CHECK_ERROR;
    }

    void GLES2RenderSystem::setStencilBufferParams(CompareFunction func,
                                                uint32 refValue, uint32 mask,
                                                StencilOperation stencilFailOp,
                                                StencilOperation depthFailOp,
                                                StencilOperation passOp,
                                                bool twoSidedOperation)
    {
		bool flip;
		mStencilMask = mask;

		if (twoSidedOperation)
		{
			if (!mCurrentCapabilities->hasCapability(RSC_TWO_SIDED_STENCIL))
				OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS, "2-sided stencils are not supported",
                            "GLES2RenderSystem::setStencilBufferParams");
            
			// NB: We should always treat CCW as front face for consistent with default
			// culling mode. Therefore, we must take care with two-sided stencil settings.
			flip = (mInvertVertexWinding && !mActiveRenderTarget->requiresTextureFlipping()) ||
            (!mInvertVertexWinding && mActiveRenderTarget->requiresTextureFlipping());
            // Back
            glStencilMaskSeparate(GL_BACK, mask);
            GL_CHECK_ERROR;
            glStencilFuncSeparate(GL_BACK, convertCompareFunction(func), refValue, mask);
            GL_CHECK_ERROR;
            glStencilOpSeparate(GL_BACK, 
                                convertStencilOp(stencilFailOp, !flip), 
                                convertStencilOp(depthFailOp, !flip), 
                                convertStencilOp(passOp, !flip));

            GL_CHECK_ERROR;
            // Front
            glStencilMaskSeparate(GL_FRONT, mask);
            GL_CHECK_ERROR;
            glStencilFuncSeparate(GL_FRONT, convertCompareFunction(func), refValue, mask);
            GL_CHECK_ERROR;
            glStencilOpSeparate(GL_FRONT, 
                                convertStencilOp(stencilFailOp, flip),
                                convertStencilOp(depthFailOp, flip), 
                                convertStencilOp(passOp, flip));
            GL_CHECK_ERROR;
		}
		else
		{
			flip = false;
			glStencilMask(mask);
            GL_CHECK_ERROR;
			glStencilFunc(convertCompareFunction(func), refValue, mask);
            GL_CHECK_ERROR;
			glStencilOp(
                        convertStencilOp(stencilFailOp, flip),
                        convertStencilOp(depthFailOp, flip), 
                        convertStencilOp(passOp, flip));
            GL_CHECK_ERROR;
		}
    }

    GLuint GLES2RenderSystem::getCombinedMinMipFilter(void) const
    {
        switch(mMinFilter)
        {
            case FO_ANISOTROPIC:
            case FO_LINEAR:
                switch (mMipFilter)
                {
                    case FO_ANISOTROPIC:
                    case FO_LINEAR:
                        // linear min, linear mip
                        return GL_LINEAR_MIPMAP_LINEAR;
                    case FO_POINT:
                        // linear min, point mip
//                        return GL_LINEAR_MIPMAP_NEAREST;
                    case FO_NONE:
                        // linear min, no mip
                        return GL_LINEAR;
                }
                break;
            case FO_POINT:
            case FO_NONE:
                switch (mMipFilter)
                {
                    case FO_ANISOTROPIC:
                    case FO_LINEAR:
                        // nearest min, linear mip
                        return GL_NEAREST_MIPMAP_LINEAR;
                    case FO_POINT:
                        // nearest min, point mip
                        return GL_NEAREST_MIPMAP_NEAREST;
                    case FO_NONE:
                        // nearest min, no mip
                        return GL_NEAREST;
                }
                break;
        }

        // should never get here
        return 0;
    }

    void GLES2RenderSystem::_setTextureUnitFiltering(size_t unit, FilterType ftype, FilterOptions fo)
    {
		if (!activateGLTextureUnit(unit))
			return;

        switch (ftype)
        {
            case FT_MIN:
                if(mTextureMipmapCount == 0)
                {
                    mMinFilter = FO_NONE;
                }
                else
                {
                    mMinFilter = fo;
                }

                // Combine with existing mip filter
                glTexParameteri(mTextureTypes[unit],
                                GL_TEXTURE_MIN_FILTER,
                                getCombinedMinMipFilter());
                GL_CHECK_ERROR;
                break;

            case FT_MAG:
                switch (fo)
                {
                    case FO_ANISOTROPIC: // GL treats linear and aniso the same
                    case FO_LINEAR:
                        glTexParameteri(mTextureTypes[unit],
                                        GL_TEXTURE_MAG_FILTER,
                                        GL_LINEAR);
                        GL_CHECK_ERROR;
                        break;
                    case FO_POINT:
                    case FO_NONE:
                        glTexParameteri(mTextureTypes[unit],
                                        GL_TEXTURE_MAG_FILTER,
                                        GL_NEAREST);
                        GL_CHECK_ERROR;
                        break;
                }
                break;
            case FT_MIP:
                if(mTextureMipmapCount == 0)
                {
                    mMipFilter = FO_NONE;
                }
                else
                {
                    mMipFilter = fo;
                }

                // Combine with existing min filter
                glTexParameteri(mTextureTypes[unit],
                                GL_TEXTURE_MIN_FILTER,
                                getCombinedMinMipFilter());

                GL_CHECK_ERROR;
                break;
        }

		activateGLTextureUnit(0);
    }

    GLfloat GLES2RenderSystem::_getCurrentAnisotropy(size_t unit)
	{
		GLfloat curAniso = 0;
		glGetTexParameterfv(mTextureTypes[unit], 
                            GL_TEXTURE_MAX_ANISOTROPY_EXT, &curAniso);
        GL_CHECK_ERROR;
		return curAniso ? curAniso : 1;
	}
    
    void GLES2RenderSystem::_setTextureLayerAnisotropy(size_t unit, unsigned int maxAnisotropy)
    {
		if (!mCurrentCapabilities->hasCapability(RSC_ANISOTROPY))
			return;

		if (!activateGLTextureUnit(unit))
			return;
        
		GLfloat largest_supported_anisotropy = 0;
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &largest_supported_anisotropy);
        GL_CHECK_ERROR;
		if (maxAnisotropy > largest_supported_anisotropy)
			maxAnisotropy = largest_supported_anisotropy ? 
			static_cast<uint>(largest_supported_anisotropy) : 1;
		if (_getCurrentAnisotropy(unit) != maxAnisotropy)
			glTexParameterf(mTextureTypes[unit], GL_TEXTURE_MAX_ANISOTROPY_EXT, maxAnisotropy);

		activateGLTextureUnit(0);
    }

    void GLES2RenderSystem::_render(const RenderOperation& op)
    {		
        GL_CHECK_ERROR;
        // Call super class
        RenderSystem::_render(op);
        // Add a render state for per pixel lighting
        Ogre::RTShader::RenderState* renderState =
            mShaderGenerator->getRenderState(Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME);
        Ogre::RTShader::SubRenderState* perPerPixelLightModel =
            mShaderGenerator->createSubRenderState("SGX_PerPixelLighting");
        renderState->addTemplateSubRenderState(perPerPixelLightModel);

        void* pBufferData = 0;
		bool multitexturing = (getCapabilities()->getNumTextureUnits() > 1);

        const VertexDeclaration::VertexElementList& decl =
            op.vertexData->vertexDeclaration->getElements();
        VertexDeclaration::VertexElementList::const_iterator elem, elemEnd;
        elemEnd = decl.end();
        vector<GLuint>::type attribsBound;

        for (elem = decl.begin(); elem != elemEnd; ++elem)
        {
            if (!op.vertexData->vertexBufferBinding->isBufferBound(elem->getSource()))
                continue; // skip unbound elements

            HardwareVertexBufferSharedPtr vertexBuffer =
                op.vertexData->vertexBufferBinding->getBuffer(elem->getSource());
            _bindGLBuffer(GL_ARRAY_BUFFER,
                static_cast<const GLES2HardwareVertexBuffer*>(vertexBuffer.get())->getGLBufferId());
            GL_CHECK_ERROR;
            pBufferData = VBO_BUFFER_OFFSET(elem->getOffset());

            if (op.vertexData->vertexStart)
            {
                pBufferData = static_cast<char*>(pBufferData) + op.vertexData->vertexStart * vertexBuffer->getVertexSize();
            }

            unsigned int i = 0;
            VertexElementSemantic sem = elem->getSemantic();
            
            bool isCustomAttrib = false;
 			if (mCurrentVertexProgram)
 				isCustomAttrib = mCurrentVertexProgram->isAttributeValid(sem, elem->getIndex());

            unsigned short typeCount = VertexElement::getTypeCount(elem->getType());
            GLboolean normalised = GL_FALSE;

 			// Custom attribute support
 			// tangents, binormals, blendweights etc always via this route
 			// builtins may be done this way too
 			if (isCustomAttrib)
 			{
 				GLint attrib = mCurrentVertexProgram->getAttributeIndex(sem, elem->getIndex());
                switch(elem->getType())
                {
                case VET_COLOUR:
                case VET_COLOUR_ABGR:
                case VET_COLOUR_ARGB:
                    // Because GL takes these as a sequence of single unsigned bytes, count needs to be 4
                    // VertexElement::getTypeCount treats them as 1 (RGBA)
                    // Also need to normalise the fixed-point data
                    typeCount = 4;
                    normalised = GL_TRUE;
                    break;
                default:
                    break;
                };
 				glVertexAttribPointer(
 					attrib,
 					typeCount, 
  					GLES2HardwareBufferManager::getGLType(elem->getType()), 
 					normalised, 
  					static_cast<GLsizei>(vertexBuffer->getVertexSize()), 
  					pBufferData);
 				glEnableVertexAttribArray(attrib);
 
 				attribsBound.push_back(attrib);
            }
            else
            {
                switch (sem)
                {
                    case VES_POSITION:
                        glVertexAttribPointer(GLES2GpuProgram::getFixedAttributeIndex(VES_POSITION, 0),
                                              typeCount,
                                              GLES2HardwareBufferManager::getGLType(elem->getType()),
                                              normalised,
                                              static_cast<GLsizei>(vertexBuffer->getVertexSize()),
                                              pBufferData);
                        GL_CHECK_ERROR;
                        glEnableVertexAttribArray(GLES2GpuProgram::getFixedAttributeIndex(VES_POSITION, 0));
                        GL_CHECK_ERROR;
                        break;
                    case VES_NORMAL:
                        glVertexAttribPointer(GLES2GpuProgram::getFixedAttributeIndex(VES_NORMAL, 0),
                                              typeCount,
                                              GLES2HardwareBufferManager::getGLType(elem->getType()),
                                              normalised,
                                              static_cast<GLsizei>(vertexBuffer->getVertexSize()),
                                              pBufferData);
                        GL_CHECK_ERROR;
                        glEnableVertexAttribArray(GLES2GpuProgram::getFixedAttributeIndex(VES_NORMAL, 0));
                        GL_CHECK_ERROR;
                        break;
                    case VES_DIFFUSE:
                        glVertexAttribPointer(GLES2GpuProgram::getFixedAttributeIndex(VES_DIFFUSE, 0), 
                                              typeCount,
                                              GLES2HardwareBufferManager::getGLType(elem->getType()),
                                              normalised,
                                              static_cast<GLsizei>(vertexBuffer->getVertexSize()),
                                              pBufferData);
                        GL_CHECK_ERROR;
                        glEnableVertexAttribArray(GLES2GpuProgram::getFixedAttributeIndex(VES_DIFFUSE, 0));
                        GL_CHECK_ERROR;
                        break;
                    case VES_SPECULAR:
                        glVertexAttribPointer(GLES2GpuProgram::getFixedAttributeIndex(VES_SPECULAR, 0), 
                                              typeCount,
                                              GLES2HardwareBufferManager::getGLType(elem->getType()),
                                              normalised,
                                              static_cast<GLsizei>(vertexBuffer->getVertexSize()),
                                              pBufferData);
                        GL_CHECK_ERROR;
                        glEnableVertexAttribArray(GLES2GpuProgram::getFixedAttributeIndex(VES_SPECULAR, 0));
                        GL_CHECK_ERROR;
                        break;
                    case VES_TEXTURE_COORDINATES:
                        if (mCurrentVertexProgram)
                        {
                            // Programmable pipeline - direct UV assignment
                            activateGLTextureUnit(GL_TEXTURE0 + elem->getIndex());
                            glVertexAttribPointer(GLES2GpuProgram::getFixedAttributeIndex(VES_TEXTURE_COORDINATES, elem->getIndex()),
                                                  typeCount,
                                                  GLES2HardwareBufferManager::getGLType(elem->getType()),
                                                  normalised,
                                                  static_cast<GLsizei>(vertexBuffer->getVertexSize()),
                                                  pBufferData);
                            GL_CHECK_ERROR;
                            glEnableVertexAttribArray(GLES2GpuProgram::getFixedAttributeIndex(VES_TEXTURE_COORDINATES, elem->getIndex()));
                            GL_CHECK_ERROR;
                        }
                        else
                        {
                            // Fixed function matching to units based on tex_coord_set
                            for (i = 0; i < mDisabledTexUnitsFrom; i++)
                            {
                                // Only set this texture unit's texcoord pointer if it
                                // is supposed to be using this element's index
                                if (mTextureCoordIndex[i] == elem->getIndex() && i < mFixedFunctionTextureUnits)
                                {
                                    if (multitexturing)
                                        activateGLTextureUnit(GL_TEXTURE0 + i);
                                    GL_CHECK_ERROR;
                                    glVertexAttribPointer(GLES2GpuProgram::getFixedAttributeIndex(VES_TEXTURE_COORDINATES, i),
                                                          typeCount,
                                                          GLES2HardwareBufferManager::getGLType(elem->getType()),
                                                          normalised,
                                                          static_cast<GLsizei>(vertexBuffer->getVertexSize()),
                                                          pBufferData);
                                    GL_CHECK_ERROR;
                                    glEnableVertexAttribArray(GLES2GpuProgram::getFixedAttributeIndex(VES_TEXTURE_COORDINATES, i));
                                    GL_CHECK_ERROR;
                                }
                            }
                        }
                        break;
                    default:
                        break;
                };
            }
        }

		if (multitexturing)
            activateGLTextureUnit(GL_TEXTURE0);
        GL_CHECK_ERROR;

        // Find the correct type to render
        GLint primType;
        switch (op.operationType)
        {
            case RenderOperation::OT_POINT_LIST:
                primType = GL_POINTS;
                break;
            case RenderOperation::OT_LINE_LIST:
                primType = GL_LINES;
                break;
            case RenderOperation::OT_LINE_STRIP:
                primType = GL_LINE_STRIP;
                break;
            default:
            case RenderOperation::OT_TRIANGLE_LIST:
                primType = GL_TRIANGLES;
                break;
            case RenderOperation::OT_TRIANGLE_STRIP:
                primType = GL_TRIANGLE_STRIP;
                break;
            case RenderOperation::OT_TRIANGLE_FAN:
                primType = GL_TRIANGLE_FAN;
                break;
        }

        if (op.useIndexes)
        {
            _bindGLBuffer(GL_ELEMENT_ARRAY_BUFFER,
                         static_cast<GLES2HardwareIndexBuffer*>(op.indexData->indexBuffer.get())->getGLBufferId());

            GL_CHECK_ERROR;
            pBufferData = VBO_BUFFER_OFFSET(op.indexData->indexStart *
                                            op.indexData->indexBuffer->getIndexSize());

            GLenum indexType = (op.indexData->indexBuffer->getType() == HardwareIndexBuffer::IT_16BIT) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;

            do
            {
                // Update derived depth bias
                if (mDerivedDepthBias && mCurrentPassIterationNum > 0)
                {
                    _setDepthBias(mDerivedDepthBiasBase +
                                  mDerivedDepthBiasMultiplier * mCurrentPassIterationNum,
                                  mDerivedDepthBiasSlopeScale);
                }
				GL_CHECK_ERROR;
                glDrawElements((_getPolygonMode() == GL_FILL) ? primType : _getPolygonMode(), op.indexData->indexCount, indexType, pBufferData);
                GL_CHECK_ERROR;
            } while (updatePassIterationRenderState());
        }
        else
        {
            do
            {
                // Update derived depth bias
                if (mDerivedDepthBias && mCurrentPassIterationNum > 0)
                {
                    _setDepthBias(mDerivedDepthBiasBase +
                                  mDerivedDepthBiasMultiplier * mCurrentPassIterationNum,
                                  mDerivedDepthBiasSlopeScale);
                }
                glDrawArrays((_getPolygonMode() == GL_FILL) ? primType : _getPolygonMode(), 0, op.vertexData->vertexCount);
                GL_CHECK_ERROR;
            } while (updatePassIterationRenderState());
        }

 		// Unbind all attributes
		for (vector<GLuint>::type::iterator ai = attribsBound.begin(); ai != attribsBound.end(); ++ai)
 		{
 			glDisableVertexAttribArray(*ai);
  		}
    }

    void GLES2RenderSystem::setScissorTest(bool enabled, size_t left,
                                        size_t top, size_t right,
                                        size_t bottom)
    {
        // If request texture flipping, use "upper-left", otherwise use "lower-left"
        bool flipping = mActiveRenderTarget->requiresTextureFlipping();
        //  GL measures from the bottom, not the top
        size_t targetHeight = mActiveRenderTarget->getHeight();
        // Calculate the "lower-left" corner of the viewport
        GLsizei w, h, x, y;

        if (enabled)
        {
            glEnable(GL_SCISSOR_TEST);
            GL_CHECK_ERROR;
            // NB GL uses width / height rather than right / bottom
            x = left;
            if (flipping)
                y = top;
            else
                y = targetHeight - bottom;
            w = right - left;
            h = bottom - top;
            glScissor(x, y, w, h);
            GL_CHECK_ERROR;
        }
        else
        {
            glDisable(GL_SCISSOR_TEST);
            GL_CHECK_ERROR;
            // GL requires you to reset the scissor when disabling
            w = mActiveViewport->getActualWidth();
            h = mActiveViewport->getActualHeight();
            x = mActiveViewport->getActualLeft();
            if (flipping)
                y = mActiveViewport->getActualTop();
            else
                y = targetHeight - mActiveViewport->getActualTop() - h;
            glScissor(x, y, w, h);
            GL_CHECK_ERROR;
        }
    }

    void GLES2RenderSystem::clearFrameBuffer(unsigned int buffers,
                                            const ColourValue& colour,
                                            Real depth, unsigned short stencil)
    {
        bool colourMask = !mColourWrite[0] || !mColourWrite[1] ||
                          !mColourWrite[2] || !mColourWrite[3];

        GLbitfield flags = 0;
        if (buffers & FBT_COLOUR)
        {
            flags |= GL_COLOR_BUFFER_BIT;
			// Enable buffer for writing if it isn't
            if (colourMask)
            {
                glColorMask(true, true, true, true);
                GL_CHECK_ERROR;
            }
            glClearColor(colour.r, colour.g, colour.b, colour.a);

            GL_CHECK_ERROR;
        }
        if (buffers & FBT_DEPTH)
        {
            flags |= GL_DEPTH_BUFFER_BIT;
			// Enable buffer for writing if it isn't
            if (!mDepthWrite)
            {
                glDepthMask(GL_TRUE);
                GL_CHECK_ERROR;
            }
            glClearDepthf(depth);
            GL_CHECK_ERROR;
        }
        if (buffers & FBT_STENCIL)
        {
            flags |= GL_STENCIL_BUFFER_BIT;
			// Enable buffer for writing if it isn't
            glStencilMask(0xFFFFFFFF);
            GL_CHECK_ERROR;
            glClearStencil(stencil);
            GL_CHECK_ERROR;
        }

		// Should be enable scissor test due the clear region is
		// relied on scissor box bounds.
        GLboolean scissorTestEnabled = glIsEnabled(GL_SCISSOR_TEST);
        GL_CHECK_ERROR;
        if (!scissorTestEnabled)
        {
            glEnable(GL_SCISSOR_TEST);
            GL_CHECK_ERROR;
        }

		// Sets the scissor box as same as viewport
        GLint viewport[4], scissor[4];
        glGetIntegerv(GL_VIEWPORT, viewport);
        GL_CHECK_ERROR;
        glGetIntegerv(GL_SCISSOR_BOX, scissor);
        GL_CHECK_ERROR;
        bool scissorBoxDifference =
            viewport[0] != scissor[0] || viewport[1] != scissor[1] ||
            viewport[2] != scissor[2] || viewport[3] != scissor[3];
        if (scissorBoxDifference)
        {
            glScissor(viewport[0], viewport[1], viewport[2], viewport[3]);
            GL_CHECK_ERROR;
        }

		// Clear buffers
        glClear(flags);
        GL_CHECK_ERROR;

        // Restore scissor box
        if (scissorBoxDifference)
        {
            glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
            GL_CHECK_ERROR;
        }

        // Restore scissor test
        if (!scissorTestEnabled)
        {
            glDisable(GL_SCISSOR_TEST);
            GL_CHECK_ERROR;
        }

        // Reset buffer write state
        if (!mDepthWrite && (buffers & FBT_DEPTH))
        {
            glDepthMask(GL_FALSE);
            GL_CHECK_ERROR;
        }

        if (colourMask && (buffers & FBT_COLOUR))
        {
            glColorMask(mColourWrite[0], mColourWrite[1], mColourWrite[2], mColourWrite[3]);
            GL_CHECK_ERROR;
        }

        if (buffers & FBT_STENCIL)
        {
            glStencilMask(mStencilMask);
            GL_CHECK_ERROR;
        }
    }

    HardwareOcclusionQuery* GLES2RenderSystem::createHardwareOcclusionQuery(void)
    {
        // Not supported
        return 0;
    }

    Real GLES2RenderSystem::getHorizontalTexelOffset(void)
    {
		// No offset in GL
        return 0.0;
    }

    Real GLES2RenderSystem::getVerticalTexelOffset(void)
    {
		// No offset in GL
        return 0.0;
    }

    Real GLES2RenderSystem::getMinimumDepthInputValue(void)
    {
		// Range [-1.0f, 1.0f]
        return -1.0f;
    }

    Real GLES2RenderSystem::getMaximumDepthInputValue(void)
    {
		// Range [-1.0f, 1.0f]
        return 1.0f;
    }

    void GLES2RenderSystem::_switchContext(GLES2Context *context)
    {
		// Unbind GPU programs and rebind to new context later, because
		// scene manager treat render system as ONE 'context' ONLY, and it
		// cached the GPU programs using state.
		if (mCurrentVertexProgram)
			mCurrentVertexProgram->unbindProgram();
		if (mCurrentFragmentProgram)
			mCurrentFragmentProgram->unbindProgram();
        
		// Disable textures
		_disableTextureUnitsFrom(0);

        // It's ready for switching
        if(mCurrentContext)
            mCurrentContext->endCurrent();
        mCurrentContext = context;
        mCurrentContext->setCurrent();

        // Check if the context has already done one-time initialisation
        if (!mCurrentContext->getInitialized())
        {
            _oneTimeContextInitialization();
            mCurrentContext->setInitialized();
        }

        // Rebind GPU programs to new context
		if (mCurrentVertexProgram)
			mCurrentVertexProgram->bindProgram();
		if (mCurrentFragmentProgram)
			mCurrentFragmentProgram->bindProgram();
        
        // Must reset depth/colour write mask to according with user desired, otherwise,
        // clearFrameBuffer would be wrong because the value we are recorded may be
        // difference with the really state stored in GL context.
        glDepthMask(mDepthWrite);
        GL_CHECK_ERROR;
        glColorMask(mColourWrite[0], mColourWrite[1], mColourWrite[2], mColourWrite[3]);
        GL_CHECK_ERROR;
        glStencilMask(mStencilMask);
        GL_CHECK_ERROR;
    }

    void GLES2RenderSystem::_unregisterContext(GLES2Context *context)
    {
        if (mCurrentContext == context)
        {
			// Change the context to something else so that a valid context
			// remains active. When this is the main context being unregistered,
			// we set the main context to 0.
            if (mCurrentContext != mMainContext)
            {
                _switchContext(mMainContext);
            }
            else
            {
				/// No contexts remain
                mCurrentContext->endCurrent();
                mCurrentContext = 0;
                mMainContext = 0;
            }
        }
    }

    void GLES2RenderSystem::_oneTimeContextInitialization()
    {
		glDisable(GL_DITHER);
    }

    void GLES2RenderSystem::initialiseContext(RenderWindow* primary)
    {
		// Set main and current context
        mMainContext = 0;
        primary->getCustomAttribute("GLCONTEXT", &mMainContext);
        mCurrentContext = mMainContext;

		// Set primary context as active
        if (mCurrentContext)
            mCurrentContext->setCurrent();

		// Setup GLSupport
        mGLSupport->initialiseExtensions();

        LogManager::getSingleton().logMessage("**************************************");
        LogManager::getSingleton().logMessage("*** OpenGL ES 2.x Renderer Started ***");
        LogManager::getSingleton().logMessage("**************************************");
    }

    void GLES2RenderSystem::_setRenderTarget(RenderTarget *target)
    {
        // Unbind frame buffer object
        if(mActiveRenderTarget)
            mRTTManager->unbind(mActiveRenderTarget);

        mActiveRenderTarget = target;

		// Switch context if different from current one
        GLES2Context *newContext = 0;
        target->getCustomAttribute("GLCONTEXT", &newContext);
        if (newContext && mCurrentContext != newContext)
        {
            _switchContext(newContext);
        }

		// Check the FBO's depth buffer status
		GLES2DepthBuffer *depthBuffer = static_cast<GLES2DepthBuffer*>(target->getDepthBuffer());

		if( target->getDepthBufferPool() != DepthBuffer::POOL_NO_DEPTH &&
			(!depthBuffer || depthBuffer->getGLContext() != mCurrentContext ) )
		{
			// Depth is automatically managed and there is no depth buffer attached to this RT
			// or the Current context doesn't match the one this Depth buffer was created with
			setDepthBufferFor( target );
		}

		// Bind frame buffer object
        mRTTManager->bind(target);
    }

    GLint GLES2RenderSystem::convertCompareFunction(CompareFunction func) const
    {
        switch(func)
        {
            case CMPF_ALWAYS_FAIL:
                return GL_NEVER;
            case CMPF_ALWAYS_PASS:
                return GL_ALWAYS;
            case CMPF_LESS:
                return GL_LESS;
            case CMPF_LESS_EQUAL:
                return GL_LEQUAL;
            case CMPF_EQUAL:
                return GL_EQUAL;
            case CMPF_NOT_EQUAL:
                return GL_NOTEQUAL;
            case CMPF_GREATER_EQUAL:
                return GL_GEQUAL;
            case CMPF_GREATER:
                return GL_GREATER;
        };
        // To keep compiler happy
        return GL_ALWAYS;
    }

    GLint GLES2RenderSystem::convertStencilOp(StencilOperation op, bool invert) const
	{
		switch(op)
		{
		case SOP_KEEP:
			return GL_KEEP;
		case SOP_ZERO:
			return GL_ZERO;
		case SOP_REPLACE:
			return GL_REPLACE;
        case SOP_INCREMENT:
			return invert ? GL_DECR : GL_INCR;
		case SOP_DECREMENT:
			return invert ? GL_INCR : GL_DECR;
		case SOP_INCREMENT_WRAP:
			return invert ? GL_DECR_WRAP : GL_INCR_WRAP;
		case SOP_DECREMENT_WRAP:
			return invert ? GL_INCR_WRAP : GL_DECR_WRAP;
		case SOP_INVERT:
			return GL_INVERT;
		};
		// to keep compiler happy
		return SOP_KEEP;
	}

    //---------------------------------------------------------------------
    void GLES2RenderSystem::bindGpuProgram(GpuProgram* prg)
    {
		GLES2GpuProgram* glprg = static_cast<GLES2GpuProgram*>(prg);
        
		// Unbind previous gpu program first.
		//
		// Note:
		//  1. Even if both previous and current are the same object, we can't
		//     bypass re-bind completely since the object itself may be modified.
		//     But we can bypass unbind based on the assumption that object
		//     internally GL program type shouldn't be changed after it has
		//     been created. The behavior of bind to a GL program type twice
		//     should be same as unbind and rebind that GL program type, even
		//     for different objects.
		//  2. We also assumed that the program's type (vertex or fragment) should
		//     not be changed during it's in using. If not, the following switch
		//     statement will confuse GL state completely, and we can't fix it
		//     here. To fix this case, we must coding the program implementation
		//     itself, if type is changing (during load/unload, etc), and it's in use,
		//     unbind and notify render system to correct for its state.
		//
		switch (glprg->getType())
		{
            case GPT_VERTEX_PROGRAM:
                if (mCurrentVertexProgram != glprg)
                {
                    if (mCurrentVertexProgram)
                        mCurrentVertexProgram->unbindProgram();
                    mCurrentVertexProgram = glprg;
                }
                break;
                
            case GPT_FRAGMENT_PROGRAM:
                if (mCurrentFragmentProgram != glprg)
                {
                    if (mCurrentFragmentProgram)
                        mCurrentFragmentProgram->unbindProgram();
                    mCurrentFragmentProgram = glprg;
                }
                break;
            case GPT_GEOMETRY_PROGRAM:
            default:
                break;
		}
        
		// Bind the program
		glprg->bindProgram();

		RenderSystem::bindGpuProgram(prg);
    }

    void GLES2RenderSystem::unbindGpuProgram(GpuProgramType gptype)
    {
		if (gptype == GPT_VERTEX_PROGRAM && mCurrentVertexProgram)
		{
			mActiveVertexGpuProgramParameters.setNull();
			mCurrentVertexProgram->unbindProgram();
			mCurrentVertexProgram = 0;
		}
		else if (gptype == GPT_FRAGMENT_PROGRAM && mCurrentFragmentProgram)
		{
			mActiveFragmentGpuProgramParameters.setNull();
			mCurrentFragmentProgram->unbindProgram();
			mCurrentFragmentProgram = 0;
		}
		RenderSystem::unbindGpuProgram(gptype);
    }

    void GLES2RenderSystem::bindGpuProgramParameters(GpuProgramType gptype, GpuProgramParametersSharedPtr params, uint16 mask)
    {
		if (mask & (uint16)GPV_GLOBAL)
		{
			// Just copy
			params->_copySharedParams();
		}
        
		switch (gptype)
		{
            case GPT_VERTEX_PROGRAM:
                mActiveVertexGpuProgramParameters = params;
                mCurrentVertexProgram->bindProgramParameters(params, mask);
                break;
            case GPT_FRAGMENT_PROGRAM:
                mActiveFragmentGpuProgramParameters = params;
                mCurrentFragmentProgram->bindProgramParameters(params, mask);
                break;
            case GPT_GEOMETRY_PROGRAM:
            default:
                break;
		}
    }

    void GLES2RenderSystem::bindGpuProgramPassIterationParameters(GpuProgramType gptype)
    {
		switch (gptype)
		{
            case GPT_VERTEX_PROGRAM:
                mCurrentVertexProgram->bindProgramPassIterationParameters(mActiveVertexGpuProgramParameters);
                break;
            case GPT_FRAGMENT_PROGRAM:
                mCurrentFragmentProgram->bindProgramPassIterationParameters(mActiveFragmentGpuProgramParameters);
                break;
            case GPT_GEOMETRY_PROGRAM:
            default:
                break;
		}
    }

	unsigned int GLES2RenderSystem::getDisplayMonitorCount() const
	{
		return 1;
	}

    bool GLES2RenderSystem::activateGLTextureUnit(size_t unit)
	{
		if (mActiveTextureUnit != unit)
		{
			if (unit < getCapabilities()->getNumTextureUnits())
			{
				glActiveTexture(GL_TEXTURE0 + unit);
                GL_CHECK_ERROR;
				mActiveTextureUnit = unit;
				return true;
			}
			else if (!unit)
			{
				// always ok to use the first unit
				return true;
			}
			else
			{
				return false;
			}
		}
		else
		{
			return true;
		}
	}

    void GLES2RenderSystem::_bindGLBuffer(GLenum target, GLuint buffer)
    {
        BindBufferMap::iterator i = mActiveBufferMap.find(target);
        if (i == mActiveBufferMap.end())
        {
            // Haven't cached this state yet.  Insert it into the map
            mActiveBufferMap.insert(BindBufferMap::value_type(target, buffer));

            // Update GL
            glBindBuffer(target, buffer);
            GL_CHECK_ERROR;
        }
        else
        {
            // Update the cached value if needed
            if((*i).second != buffer)
            {
                (*i).second = buffer;

                // Update GL
                glBindBuffer(target, buffer);
                GL_CHECK_ERROR;
            }
        }
    }

    /** This class demonstrates basic usage of the RTShader system.
    It subclasses the material manager listener class and when a target scheme callback
    is invoked with the shader generator scheme it tries to create an equivalent shader
    based technique based on the default technique of the given material.
    */
	ShaderGeneratorTechniqueResolverListener::ShaderGeneratorTechniqueResolverListener(RTShader::ShaderGenerator* pShaderGenerator)
	{
		mShaderGenerator = pShaderGenerator;			
	}

	/** This is the hook point where shader based technique will be created.
	It will be called whenever the material manager won't find appropriate technique
	that satisfy the target scheme name. If the scheme name is out target RT Shader System
	scheme name we will try to create shader generated technique for it. 
	*/
	Technique* ShaderGeneratorTechniqueResolverListener::handleSchemeNotFound(unsigned short schemeIndex, 
		const String& schemeName, Material* originalMaterial, unsigned short lodIndex, 
		const Renderable* rend)
	{
		Ogre::Technique* generatedTech = NULL;

		// Case this is the default shader generator scheme.
		if (schemeName == Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME)
		{
			bool techniqueCreated;

			// Create shader generated technique for this material.
			techniqueCreated = mShaderGenerator->createShaderBasedTechnique(
                                                                            originalMaterial->getName(), 
                                                                            Ogre::MaterialManager::DEFAULT_SCHEME_NAME, 
                                                                            schemeName);

			// Case technique registration succeeded.
			if (techniqueCreated)
			{				
				// Force creating the shaders for the generated technique.
				mShaderGenerator->validateMaterial(schemeName, originalMaterial->getName());
				
				// Grab the generated technique.
				Ogre::Material::TechniqueIterator itTech = originalMaterial->getTechniqueIterator();

				while (itTech.hasMoreElements())
				{
					Ogre::Technique* curTech = itTech.getNext();

					if (curTech->getSchemeName() == schemeName)
					{
						generatedTech = curTech;
						break;
					}
				}				
			}
		}

		return generatedTech;
	}

}
