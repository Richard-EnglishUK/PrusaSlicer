#include "libslic3r/libslic3r.h"
#include "OpenGLManager.hpp"

#include "GUI.hpp"
#include "I18N.hpp"
#include "3DScene.hpp"

#include "libslic3r/Platform.hpp"

#include <GL/glew.h>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/log/trivial.hpp>

#include <wx/glcanvas.h>
#include <wx/msgdlg.h>

#ifdef __APPLE__
// Part of hack to remove crash when closing the application on OSX 10.9.5 when building against newer wxWidgets
#include <wx/platinfo.h>

#include "../Utils/MacDarkMode.hpp"
#endif // __APPLE__

namespace Slic3r {
namespace GUI {

// A safe wrapper around glGetString to report a "N/A" string in case glGetString returns nullptr.
inline std::string gl_get_string_safe(GLenum param, const std::string& default_value)
{
    const char* value = (const char*)::glGetString(param);
    return std::string((value != nullptr) ? value : default_value);
}

const std::string& OpenGLManager::GLInfo::get_version() const
{
    if (!m_detected)
        detect();

    return m_version;
}

const std::string& OpenGLManager::GLInfo::get_glsl_version() const
{
    if (!m_detected)
        detect();

    return m_glsl_version;
}

const std::string& OpenGLManager::GLInfo::get_vendor() const
{
    if (!m_detected)
        detect();

    return m_vendor;
}

const std::string& OpenGLManager::GLInfo::get_renderer() const
{
    if (!m_detected)
        detect();

    return m_renderer;
}

int OpenGLManager::GLInfo::get_max_tex_size() const
{
    if (!m_detected)
        detect();

    // clamp to avoid the texture generation become too slow and use too much GPU memory
#ifdef __APPLE__
    // and use smaller texture for non retina systems
    return (Slic3r::GUI::mac_max_scaling_factor() > 1.0) ? std::min(m_max_tex_size, 8192) : std::min(m_max_tex_size / 2, 4096);
#else
    // and use smaller texture for older OpenGL versions
    return is_version_greater_or_equal_to(3, 0) ? std::min(m_max_tex_size, 8192) : std::min(m_max_tex_size / 2, 4096);
#endif // __APPLE__
}

float OpenGLManager::GLInfo::get_max_anisotropy() const
{
    if (!m_detected)
        detect();

    return m_max_anisotropy;
}

void OpenGLManager::GLInfo::detect() const
{
    m_version = gl_get_string_safe(GL_VERSION, "N/A");
    m_glsl_version = gl_get_string_safe(GL_SHADING_LANGUAGE_VERSION, "N/A");
    m_vendor = gl_get_string_safe(GL_VENDOR, "N/A");
    m_renderer = gl_get_string_safe(GL_RENDERER, "N/A");

    glsafe(::glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_max_tex_size));

    m_max_tex_size /= 2;

    if (Slic3r::total_physical_memory() / (1024 * 1024 * 1024) < 6)
        m_max_tex_size /= 2;

    if (GLEW_EXT_texture_filter_anisotropic)
        glsafe(::glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &m_max_anisotropy));

    m_detected = true;
}

static bool version_greater_or_equal_to(const std::string& version, unsigned int major, unsigned int minor)
{
    if (version == "N/A")
        return false;

    std::vector<std::string> tokens;
    boost::split(tokens, version, boost::is_any_of(" "), boost::token_compress_on);

    if (tokens.empty())
        return false;

    std::vector<std::string> numbers;
    boost::split(numbers, tokens[0], boost::is_any_of("."), boost::token_compress_on);

    unsigned int gl_major = 0;
    unsigned int gl_minor = 0;

    if (numbers.size() > 0)
        gl_major = ::atoi(numbers[0].c_str());

    if (numbers.size() > 1)
        gl_minor = ::atoi(numbers[1].c_str());

    if (gl_major < major)
        return false;
    else if (gl_major > major)
        return true;
    else
        return gl_minor >= minor;
}

bool OpenGLManager::GLInfo::is_version_greater_or_equal_to(unsigned int major, unsigned int minor) const
{
    if (!m_detected)
        detect();

    return version_greater_or_equal_to(m_version, major, minor);
}

bool OpenGLManager::GLInfo::is_glsl_version_greater_or_equal_to(unsigned int major, unsigned int minor) const
{
    if (!m_detected)
        detect();

    return version_greater_or_equal_to(m_glsl_version, major, minor);
}

std::string OpenGLManager::GLInfo::to_string(bool format_as_html, bool extensions) const
{
    if (!m_detected)
        detect();

    std::stringstream out;

    std::string h2_start = format_as_html ? "<b>" : "";
    std::string h2_end = format_as_html ? "</b>" : "";
    std::string b_start = format_as_html ? "<b>" : "";
    std::string b_end = format_as_html ? "</b>" : "";
    std::string line_end = format_as_html ? "<br>" : "\n";

    out << h2_start << "OpenGL installation" << h2_end << line_end;
    out << b_start << "GL version:   " << b_end << m_version << line_end;
    out << b_start << "Vendor:       " << b_end << m_vendor << line_end;
    out << b_start << "Renderer:     " << b_end << m_renderer << line_end;
    out << b_start << "GLSL version: " << b_end << m_glsl_version << line_end;

    if (extensions)
    {
        std::vector<std::string> extensions_list;
        std::string extensions_str = gl_get_string_safe(GL_EXTENSIONS, "");
        boost::split(extensions_list, extensions_str, boost::is_any_of(" "), boost::token_compress_off);

        if (!extensions_list.empty())
        {
            out << h2_start << "Installed extensions:" << h2_end << line_end;

            std::sort(extensions_list.begin(), extensions_list.end());
            for (const std::string& ext : extensions_list)
            {
                out << ext << line_end;
            }
        }
    }

    return out.str();
}

OpenGLManager::GLInfo OpenGLManager::s_gl_info;
bool OpenGLManager::s_compressed_textures_supported = false;
OpenGLManager::EMultisampleState OpenGLManager::s_multisample = OpenGLManager::EMultisampleState::Unknown;
OpenGLManager::EFramebufferType OpenGLManager::s_framebuffers_type = OpenGLManager::EFramebufferType::Unknown;

#ifdef __APPLE__ 
// Part of hack to remove crash when closing the application on OSX 10.9.5 when building against newer wxWidgets
OpenGLManager::OSInfo OpenGLManager::s_os_info;
#endif // __APPLE__ 

OpenGLManager::~OpenGLManager()
{
    m_shaders_manager.shutdown();

#ifdef __APPLE__ 
    // This is an ugly hack needed to solve the crash happening when closing the application on OSX 10.9.5 with newer wxWidgets
    // The crash is triggered inside wxGLContext destructor
    if (s_os_info.major != 10 || s_os_info.minor != 9 || s_os_info.micro != 5)
    {
#endif //__APPLE__
        if (m_context != nullptr)
            delete m_context;
#ifdef __APPLE__ 
    }
#endif //__APPLE__
}

bool OpenGLManager::init_gl()
{
    if (!m_gl_initialized) {
        if (glewInit() != GLEW_OK) {
            BOOST_LOG_TRIVIAL(error) << "Unable to init glew library";
            return false;
        }
        m_gl_initialized = true;
        if (GLEW_EXT_texture_compression_s3tc)
            s_compressed_textures_supported = true;
        else
            s_compressed_textures_supported = false;

        if (GLEW_ARB_framebuffer_object)
            s_framebuffers_type = EFramebufferType::Arb;
        else if (GLEW_EXT_framebuffer_object)
            s_framebuffers_type = EFramebufferType::Ext;
        else
            s_framebuffers_type = EFramebufferType::Unknown;

        bool valid_version = s_gl_info.is_version_greater_or_equal_to(2, 0);
        if (!valid_version) {
            // Complain about the OpenGL version.
            wxString message = from_u8((boost::format(
                _utf8(L("PrusaSlicer requires OpenGL 2.0 capable graphics driver to run correctly, \n"
                    "while OpenGL version %s, render %s, vendor %s was detected."))) % s_gl_info.get_version() % s_gl_info.get_renderer() % s_gl_info.get_vendor()).str());
            message += "\n";
        	message += _L("You may need to update your graphics card driver.");
#ifdef _WIN32
            message += "\n";
        	message += _L("As a workaround, you may run PrusaSlicer with a software rendered 3D graphics by running prusa-slicer.exe with the --sw_renderer parameter.");
#endif
        	wxMessageBox(message, wxString("PrusaSlicer - ") + _L("Unsupported OpenGL version"), wxOK | wxICON_ERROR);
        }

        if (valid_version) {
            // load shaders
            auto [result, error] = m_shaders_manager.init();
            if (!result) {
                wxString message = from_u8((boost::format(
                    _utf8(L("Unable to load the following shaders:\n%s"))) % error).str());
                wxMessageBox(message, wxString("PrusaSlicer - ") + _L("Error loading shaders"), wxOK | wxICON_ERROR);
            }
        }
    }

    return true;
}

wxGLContext* OpenGLManager::init_glcontext(wxGLCanvas& canvas)
{
    if (m_context == nullptr) {
        m_context = new wxGLContext(&canvas);

#ifdef __APPLE__ 
        // Part of hack to remove crash when closing the application on OSX 10.9.5 when building against newer wxWidgets
        s_os_info.major = wxPlatformInfo::Get().GetOSMajorVersion();
        s_os_info.minor = wxPlatformInfo::Get().GetOSMinorVersion();
        s_os_info.micro = wxPlatformInfo::Get().GetOSMicroVersion();
#endif //__APPLE__
    }
    return m_context;
}

wxGLCanvas* OpenGLManager::create_wxglcanvas(wxWindow& parent)
{
    int attribList[] = { 
    	WX_GL_RGBA,
    	WX_GL_DOUBLEBUFFER,
    	// RGB channels each should be allocated with 8 bit depth. One should almost certainly get these bit depths by default.
      	WX_GL_MIN_RED, 			8,
      	WX_GL_MIN_GREEN, 		8,
      	WX_GL_MIN_BLUE, 		8,
      	// Requesting an 8 bit alpha channel. Interestingly, the NVIDIA drivers would most likely work with some alpha plane, but glReadPixels would not return
      	// the alpha channel on NVIDIA if not requested when the GL context is created.
      	WX_GL_MIN_ALPHA, 		8,
    	WX_GL_DEPTH_SIZE, 		24,
    	WX_GL_SAMPLE_BUFFERS, 	GL_TRUE,
    	WX_GL_SAMPLES, 			4,
    	0
    };

    if (s_multisample == EMultisampleState::Unknown)
    {
        detect_multisample(attribList);
//        // debug output
//        std::cout << "Multisample " << (can_multisample() ? "enabled" : "disabled") << std::endl;
    }

    if (! can_multisample())
        attribList[12] = 0;

    return new wxGLCanvas(&parent, wxID_ANY, attribList, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS);
}

void OpenGLManager::detect_multisample(int* attribList)
{
    int wxVersion = wxMAJOR_VERSION * 10000 + wxMINOR_VERSION * 100 + wxRELEASE_NUMBER;
    bool enable_multisample = wxVersion >= 30003;
    s_multisample = 
        enable_multisample &&
        // Disable multi-sampling on ChromeOS, as the OpenGL virtualization swaps Red/Blue channels with multi-sampling enabled,
        // at least on some platforms.
        platform_flavor() != PlatformFlavor::LinuxOnChromium &&
        wxGLCanvas::IsDisplaySupported(attribList)
        ? EMultisampleState::Enabled : EMultisampleState::Disabled;
    // Alternative method: it was working on previous version of wxWidgets but not with the latest, at least on Windows
    // s_multisample = enable_multisample && wxGLCanvas::IsExtensionSupported("WGL_ARB_multisample");
}

} // namespace GUI
} // namespace Slic3r
