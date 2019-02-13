#define GL_GLEXT_PROTOTYPES
#include <GLFW/glfw3.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include "hydra.h"

// some globals
JpegMemory_t mem;
JpegDec_t jpeg_dec;
int c_pressed      = 0;
int s_pressed      = 0;
int t_pressed      = 0;
int space_pressed  = 0;
int picture_number = 0;
pthread_mutex_t video_mutex;
double sony_fetch_time = 0;
double sony_fetch_time_temp = 0;
bool measuring_sony = false;
bool show_render_time = false;

#ifdef NDEBUG
# define CHECK_GL()
#else
# define CHECK_GL()    CheckGLError(__FILE__, __LINE__, __func__)
#endif

#ifndef NDEBUG
static void CheckGLError(const char *file, int line, const char *func)
{
    GLenum e;
    static const struct
    {
        const char *str;
        GLenum     code;
    }
    tbl[] =
    {
        { "GL_INVALID_ENUM",      0x0500 },
        { "GL_INVALID_VALUE",     0x0501 },
        { "GL_INVALID_OPERATION", 0x0502 },
        { "GL_OUT_OF_MEMORY",     0x0505 }
    };

    e = glGetError();
    if (e != 0)
    {
        int i;
        printf("\nfrom %s(%d): function %s\n", file, line, func);
        for (i = 0; i < (int)ARRAY_SIZEOF(tbl); i++)
        {
            if (e == tbl[i].code)
            {
                printf("  OpenGL|ES raise: code 0x%04x (%s)\n", e, tbl[i].str);
                return;
            }
        }
        printf("  OpenGL|ES raise: code 0x%04x (?)\n", e);
    }
}
#endif

void handleError(const char *message, int _exitStatus)
{
    fprintf(stderr, "%s", message);
    exit(_exitStatus);
}


static double GetCurrentTimeInMilliSecond(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}


static size_t SonyCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t       realsize = size * nmemb;
    JpegMemory_t *mem     = (JpegMemory_t *)userp;

    if (show_render_time && !(measuring_sony))
    {
        sony_fetch_time_temp = GetCurrentTimeInMilliSecond();
        measuring_sony = true;
    }

    mem->memory = realloc(mem->memory, mem->size + realsize + 1);
    if (mem->memory == NULL)
    {
        printf("Not enough memory (realloc returned NULL)\n");
        return 0;
    }

    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    // did we read the payload header ?
    if (mem->size == 136)
    {
        sprintf(mem->size_string, "%02hhx%02hhx%02hhx", (unsigned char)mem->memory[12], (unsigned char)mem->memory[13], (unsigned char)mem->memory[14]);
        mem->jpeg_size    = (int)strtol(mem->size_string, NULL, 16);
        mem->header_found = true;
    }

    // read the jpeg data
    if ((mem->size >= 136 + mem->jpeg_size) && mem->header_found)
    {
        // Lock mem and read data
        pthread_mutex_lock(&video_mutex);
        LoadJPEG(&mem->memory[136], &jpeg_dec, mem->jpeg_size);
        pthread_mutex_unlock(&video_mutex);

        // if we want to save the data, do it now
        if (mem->save)
        {
            SaveJPEG(mem);
        }

        // if we measure time taken to fetch the image data
        if (show_render_time)
        {
            sony_fetch_time = GetCurrentTimeInMilliSecond() - sony_fetch_time_temp;
            measuring_sony = false;
        }

        // start loading another image
        mem->size = 0;
        return realsize;
    }
    return realsize;
}


void LoadJPEG(const unsigned char *imgdata, JpegDec_t *jpeg_dec, size_t jpeg_size)
{
    struct jpeg_decompress_struct info;
    struct jpeg_error_mgr         err;

    info.err = jpeg_std_error(&err);
    jpeg_create_decompress(&info);
    jpeg_mem_src(&info, imgdata, jpeg_size);
    jpeg_read_header(&info, TRUE);
    info.do_fancy_upsampling = FALSE;
    // Free previous jpeg_dec memory (if any)
    free(jpeg_dec->data);

    jpeg_start_decompress(&info);
    jpeg_dec->x        = info.output_width;
    jpeg_dec->y        = info.output_height;
    jpeg_dec->channels = info.num_components;

    jpeg_dec->bpp  = jpeg_dec->channels * 8;
    jpeg_dec->size = jpeg_dec->x * jpeg_dec->y * 3;
    jpeg_dec->data = malloc(jpeg_dec->size);
    unsigned char *p1      = jpeg_dec->data;
    unsigned char **p2     = &p1;
    int           numlines = 0;
    while (info.output_scanline < info.output_height)
    {
        numlines = jpeg_read_scanlines(&info, p2, 1);
        *p2     += numlines * 3 * info.output_width;
    }
    jpeg_finish_decompress(&info);
}


void SaveJPEG(JpegMemory_t *mem)
{
    //TODO make this safe by checking if the format is ok (%0Nd)
    //TODO malloc filename according to the N in format
    char filename[500];
    sprintf(filename, mem->savename, picture_number);
    printf("Saving %s\n", filename);

    FILE *f = fopen(filename, "w");
    if (f == NULL)
    {
        printf("Error opening file!\n");
        exit(1);
    }
    fwrite(&mem->memory[136], mem->jpeg_size, 1, f);
    fclose(f);

    picture_number++;
}


void *getJpegData(void *memory) {
    JpegMemory_t *mem = (JpegMemory_t *)memory;

    curl_easy_setopt(mem->curl_handle, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(mem->curl_handle, CURLOPT_WRITEFUNCTION, SonyCallback);
    curl_easy_setopt(mem->curl_handle, CURLOPT_WRITEDATA, mem);
    curl_easy_perform(mem->curl_handle);
    curl_easy_cleanup(mem->curl_handle);
    curl_global_cleanup();

    // make gcc happy
    return 0;
}


void ShowUsage(void)
{
    printf("usage: hydra [options]\n");
    printf("\n");
    printf("Display options:\n");
    printf("    --primary-fs                            create a fullscreen window on primary monitor\n");
    printf("    --primary-res [WidthxHeight]            create a width x height window on primary monitor (default: 800x600)\n");
    printf("    --secondary-fs                          create a fullscreen window on secondary monitor\n");
    printf("    --secondary-res [WidthxHeight]          create a width x height window on secondary monitor\n");
    printf("\n");
    printf("Saving options:\n");
    printf("    --save-dir dir                          directory where to save frames\n");
    printf("    --save-file filename                    filename to save frames in the form: name_%%0d.jpeg\n");
    printf("                                            %%0d stands for number of digits, eg. my_%%06d.jpeg\n");
    printf("                                            will be saved as my_000001.jpeg, my_000002.jpeg, etc..\n");
    printf("\n");
}


size_t Hydra_InstanceSize(void)
{
    return sizeof(Hydra);
}


int Hydra_Construct(Hydra *hy)
{
    // TODO make this into a runtime parameter
    char* lv = getenv("CAM_LV");
    const char* padd = "/liveview.JPG?!1234!http-get:*:image/jpeg:*!!!!!";
    char* flv = malloc(strlen(lv) + strlen(padd) + 1);
    strcpy(flv, lv);
    strcat(flv, padd);

    hy->use_sony         = 1;
    hy->freeze_frame     = 0;
    hy->viewport.x = 0;
    hy->viewport.y = 0;
    hy->array_buffer_fullscene_quad = 0;
    hy->vertex_coord      = 0;
    hy->fragment_shader   = 0;
    hy->vertex_shader     = 0;
    hy->program           = 0;
    hy->resolution        = 0;
    hy->sony_uniform      = 0;
    hy->sony_texture_name = 0;
    hy->sony_texture_unit = 0;
    hy->texture_width   = 640;
    hy->texture_height  = 360;
    hy->bytes_per_pixel = 2;                                                                                       // int 2 for YUV422
    hy->internal_format = (GLint)GL_RGB;
    hy->pixelformat     = (GLenum)GL_RGB;
    hy->dirpath         = NULL;
    hy->filename        = NULL;

    // take care of shared memory
    memset(&jpeg_dec, 0, sizeof(jpeg_dec));
    jpeg_dec.x        = 0;
    jpeg_dec.y        = 0;
    jpeg_dec.bpp      = 0;
    jpeg_dec.data     = NULL;
    jpeg_dec.size     = 0;
    jpeg_dec.channels = 0;

    memset(&mem, 0, sizeof(mem));
    mem.memory       = malloc(1);
    mem.size         = 0;
    mem.header_found = false;
    mem.size_string  = malloc(6);
    mem.jpeg_size    = 0;
    mem.save         = 0;
    mem.savename     = NULL;

    pthread_mutex_init(&video_mutex, NULL);

    // setup libcurl
    curl_global_init(CURL_GLOBAL_ALL);
    mem.curl_handle  = curl_easy_init();
    curl_easy_setopt(mem.curl_handle, CURLOPT_URL, flv);

    return 0;
}


static void PrintShaderLog(const char *message, GLuint shader)
{
    GLchar build_log[512];

    glGetShaderInfoLog(shader, sizeof(build_log), NULL, build_log);
    printf("%s %d: %s\n", message, shader, build_log);
}


static void PrintProgramLog(const char *message, GLuint program)
{
    GLchar build_log[512];

    glGetProgramInfoLog(program, sizeof(build_log), NULL, build_log);
    printf("%s %d: %s\n", message, program, build_log);
}


void SetLayout(Hydra *hy, LAYOUT layout, int width, int height) // flatten
{
    hy->layout     = layout;
    hy->viewport.x = width;
    hy->viewport.y = height;
}

void SetSaveDir(Hydra *hy, char *dirpath)
{
    hy->dirpath = malloc(strlen(dirpath) + 1);
    strcpy(hy->dirpath, dirpath);
}

void SetSaveFile(Hydra *hy, char *filename)
{
    hy->filename = malloc(strlen(filename) + 1);
    strcpy(hy->filename, filename);
}

void SetSaveDestination(Hydra *hy)
{
    if (hy->dirpath == NULL) {
        if (hy->filename == NULL) {
            // using default filename
            mem.savename = malloc(strlen("sony_%05d.jpeg") + 1);
            strcpy(mem.savename, "sony_%05d.jpeg");
        } else {
            mem.savename = malloc(strlen(hy->filename) + 1);
            strcpy(mem.savename, hy->filename);
        }
    } else {
        if (hy->filename == NULL) {
            // using default filename
            mem.savename = malloc(strlen(hy->dirpath) + strlen("sony_%05d.jpeg") + 2);
            strcpy(mem.savename, hy->dirpath);
            strcat(mem.savename, "/");
            strcat(mem.savename, "sony_%05d.jpeg");
        } else {
            mem.savename = malloc(strlen(hy->dirpath) + strlen(hy->filename) + 2);
            strcpy(mem.savename, hy->dirpath);
            strcat(mem.savename, "/");
            strcat(mem.savename, hy->filename);
        }
    }
    printf("Files will be saved to: %s\n", mem.savename);
}

float fixDpiScale(GLFWwindow *window)
{
    int window_width, window_height, framebuffer_width, framebuffer_height;

    glfwGetWindowSize(window, &window_width, &window_height);
    glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
    return framebuffer_width / window_width;
}


void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    (void)scancode;
    (void)mods;
    if ((key == GLFW_KEY_SPACE) && (action == GLFW_PRESS))
    {
        space_pressed = 1;
    }
    if (((key == GLFW_KEY_Q) || (key == GLFW_KEY_ESCAPE)) && (action == GLFW_PRESS))
    {
        glfwSetWindowShouldClose(window, GL_TRUE);
    }
    if ((key == GLFW_KEY_C) && (action == GLFW_PRESS))
    {
        c_pressed = 1;
    }
    if ((key == GLFW_KEY_S) && (action == GLFW_PRESS))
    {
        s_pressed = 1;
    }
    if ((key == GLFW_KEY_T) && (action == GLFW_PRESS))
    {
        t_pressed = 1;
    }
}


void resize_callback(GLFWwindow *window, int _w, int _h)
{
    float dpiScale = fixDpiScale(window);
    float inter_w  = (float)_w * dpiScale;
    float inter_h  = (float)_h * dpiScale;

    glViewport(0, 0, (float)inter_w, (float)inter_h);
}


void close_callback(GLFWwindow *window)
{
    glfwSetWindowShouldClose(window, GL_TRUE);
}


void GetSourceSize(Hydra *hy, int *width, int *height)
{
    glfwGetWindowSize(hy->window, width, height);
}


void SetupViewport(Hydra *hy)
{
    int i, count, xpos, ypos;

    GLFWmonitor **monitors = glfwGetMonitors(&count);

    for (i = 0; i < count; i++)
    {
        const GLFWvidmode *mode = glfwGetVideoMode(monitors[i]);
        fprintf(stderr, "Monitor[%i]: %i x %i @ %i hz\n", i, mode->width, mode->height, mode->refreshRate);
    }

    xpos = 0;
    ypos = 0;

    switch (hy->layout)
    {
    case LAYOUT_PRIMARY_RESOLUTION:
        printf("Using primary monitor\n");
        break;

    case LAYOUT_PRIMARY_FULLSCREEN:
        printf("Using primary monitor in fullscreen mode\n");
        const GLFWvidmode *mode_primary = glfwGetVideoMode(monitors[0]);
        hy->viewport.x = mode_primary->width;
        hy->viewport.y = mode_primary->height;
        glfwWindowHint(GLFW_DECORATED, GL_FALSE);
        glfwWindowHint(GLFW_FOCUSED, GL_TRUE);
        break;

    case LAYOUT_SECONDARY_FULLSCREEN:
        if (count > 1)
        {
            printf("Using secondary monitor in fullscreen mode\n");
            glfwGetMonitorPos(monitors[1], &xpos, &ypos);
            printf("Placing render window at monitor[1] position: %d x %d \n", xpos, ypos);
            const GLFWvidmode *mode_secondary = glfwGetVideoMode(monitors[1]);
            hy->viewport.x = mode_secondary->width;
            hy->viewport.y = mode_secondary->height;
            glfwWindowHint(GLFW_DECORATED, GL_FALSE);
            glfwWindowHint(GLFW_FOCUSED, GL_TRUE);
        }
        else
        {
            printf("Cant detect any secondary monitors\n");
            exit(0);
        }
        break;

    case LAYOUT_SECONDARY_RESOLUTION:
        if (count > 1)
        {
            printf("Using secondary monitor\n");
            glfwGetMonitorPos(monitors[1], &xpos, &ypos);
            printf("Placing render window at monitor[1] position: %d x %d \n", xpos, ypos);
        }
        else
        {
            printf("Cant detect any secondary monitors\n");
            exit(0);
        }
        break;
    }
    // Create a window
    hy->window = glfwCreateWindow(hy->viewport.x, hy->viewport.y, "hydra", NULL, NULL);
    if (!hy->window)
    {
        glfwTerminate();
        handleError("GLFW create window failed", -1);
    }
    // Fix DPI
    hy->viewport.x *= fixDpiScale(hy->window);
    hy->viewport.y *= fixDpiScale(hy->window);
    glfwSetWindowSize(hy->window, hy->viewport.x, hy->viewport.y);
    glfwSetWindowPos(hy->window, 0, 0);
    glViewport(0.0, 0.0, hy->viewport.x, hy->viewport.y);
    glfwMakeContextCurrent(hy->window);
    glfwSetKeyCallback(hy->window, key_callback);
    glfwSetWindowSizeCallback(hy->window, resize_callback);
    glfwSetWindowCloseCallback(hy->window, close_callback);
    glfwSwapInterval(1);
    CHECK_GL();
}


static int Hydra_SetupShaders(Hydra *hy)
{
    GLint param, vc_location;

    static const GLfloat fullscene_quad[] =
    {
        -1.0, -1.0, 1.0, 1.0,
        1.0,  -1.0, 1.0, 1.0,
        1.0,  1.0,  1.0, 1.0,
        -1.0, 1.0,  1.0, 1.0
    };

    static const GLchar *vertex_shader_source =
        "attribute vec4 vertex_coord;"
        "void main(void) { gl_Position = vertex_coord; }";

    static const GLchar *fragment_shader_source =
        "uniform vec2 resolution;"
        "uniform sampler2D sony;"
        "void main(void) {"
        "vec2 p = vec2( gl_FragCoord.x / resolution.x, 1.0 - gl_FragCoord.y / resolution.y);"
        "gl_FragColor = vec4(1.,0.,0.,1.);"
        "gl_FragColor = texture2D(sony, p);"
        "}";

    SetupViewport(hy);

    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glDisable(GL_SAMPLE_COVERAGE);

    glGenBuffers(1, &hy->array_buffer_fullscene_quad);
    glBindBuffer(GL_ARRAY_BUFFER, hy->array_buffer_fullscene_quad);
    glBufferData(GL_ARRAY_BUFFER, sizeof(fullscene_quad),
                 fullscene_quad, GL_STATIC_DRAW);
    CHECK_GL();

    hy->vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(hy->vertex_shader, 1, &vertex_shader_source, NULL);
    glCompileShader(hy->vertex_shader);
    glGetShaderiv(hy->vertex_shader, GL_COMPILE_STATUS, &param);
    if (param != GL_TRUE)
    {
        PrintShaderLog("vertex_shader", hy->vertex_shader);
        assert(0);
        return 1;
    }
    CHECK_GL();

    hy->fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(hy->fragment_shader, 1, &fragment_shader_source, NULL);
    glCompileShader(hy->fragment_shader);
    glGetShaderiv(hy->fragment_shader, GL_COMPILE_STATUS, &param);
    if (param != GL_TRUE)
    {
        PrintShaderLog("fragment_shader", hy->fragment_shader);
        return 2;
    }
    CHECK_GL();

    hy->program = glCreateProgram();
    glAttachShader(hy->program, hy->vertex_shader);
    glAttachShader(hy->program, hy->fragment_shader);
    glLinkProgram(hy->program);
    glGetProgramiv(hy->program, GL_LINK_STATUS, &param);
    if (param != GL_TRUE)
    {
        PrintProgramLog("program", hy->program);
        return 3;
    }
    glUseProgram(hy->program);
    CHECK_GL();

    hy->resolution   = glGetUniformLocation(hy->program, "resolution");
    hy->sony_uniform = glGetUniformLocation(hy->program, "sony");
    vc_location      = glGetAttribLocation(hy->program, "vertex_coord");
    glEnableVertexAttribArray(vc_location);
    glVertexAttribPointer(vc_location,
                          4,
                          GL_FLOAT,
                          GL_FALSE,
                          16,
                          NULL);
    CHECK_GL();

    if (hy->use_sony)
    {
        glGenTextures(1, hy->textures);
        hy->sony_texture_name = hy->textures[0];
        glBindTexture(GL_TEXTURE_2D, hy->sony_texture_name);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     hy->internal_format,
                     hy->texture_width, hy->texture_height,
                     0,
                     hy->pixelformat,
                     GL_UNSIGNED_BYTE,
                     NULL);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
    }
    CHECK_GL();
    return 0;
}


int Hydra_ParseArgs(Hydra *hy, int argc, const char *argv[])
{
    int i;
    int width, height;
    char dirpath[255], filename[255];

    for (i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--primary-fs"))
        {
            SetLayout(hy, LAYOUT_PRIMARY_FULLSCREEN, 0, 0);
            continue;
        }
        if (!strcmp(argv[i], "--primary-res"))
        {
            if (++i >= argc)
            {
                ShowUsage();
            }
            if (sscanf(argv[i], "%dx%d", &width, &height) < 2)
            {
                ShowUsage();
            }
            SetLayout(hy, LAYOUT_PRIMARY_RESOLUTION, width, height);
            continue;
        }
        if (!strcmp(argv[i], "--secondary-fs"))
        {
            SetLayout(hy, LAYOUT_SECONDARY_FULLSCREEN, 0, 0);
            continue;
        }
        if (!strcmp(argv[i], "--secondary-res"))
        {
            if (++i >= argc)
            {
                ShowUsage();
            }
            if (sscanf(argv[i], "%dx%d", &width, &height) < 2)
            {
                ShowUsage();
            }
            SetLayout(hy, LAYOUT_SECONDARY_RESOLUTION, width, height);
            continue;
        }
        if (!strcmp(argv[i], "--save-dir"))
        {
            if (++i >= argc)
            {
                ShowUsage();
            }
            if (sscanf(argv[i], "%s", dirpath) < 1)
            {
                ShowUsage();
            }

            // remove trailing slash
            if (dirpath[strlen(dirpath)-1] == '/') {
                dirpath[strlen(dirpath)-1] = '\0';
            }

            // check if dir exists
            struct stat stats;
            stat(dirpath, &stats);
            if (!S_ISDIR(stats.st_mode)) {
                printf("Directory %s doesnt exist. Exiting\n", dirpath);
                exit(1);
            }

            SetSaveDir(hy, dirpath);
            continue;
        }
        if (!strcmp(argv[i], "--save-file"))
        {
            if (++i >= argc)
            {
                ShowUsage();
            }
            if (sscanf(argv[i], "%s", filename) < 1)
            {
                ShowUsage();
            }
            SetSaveFile(hy, filename);
            continue;
        }
    }

    // Determine the final saving path + filename
    SetSaveDestination(hy);

    return 0;
}


void SetUniforms(Hydra *hy)
{
    int width, height;

    GetSourceSize(hy, &width, &height);
    glUseProgram(hy->program);
    glUniform2f(hy->resolution, (double)width, (double)height);
    CHECK_GL();
}


static void Hydra_Render(Hydra *hy)
{
    double render_time;

    if (show_render_time)
    {
        render_time = GetCurrentTimeInMilliSecond();
    }

    glUseProgram(hy->program);
    if (hy->use_sony)
    {

        // TODO solve freeze frame (dont overwrite memory)
        if (!hy->freeze_frame)
        {
        }

        glUniform1i(hy->sony_uniform, hy->sony_texture_unit);
        glActiveTexture(GL_TEXTURE0 + hy->sony_texture_unit);
        glBindTexture(GL_TEXTURE_2D, hy->sony_texture_name);

        pthread_mutex_lock(&video_mutex);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     hy->internal_format,
                     hy->texture_width, hy->texture_height,
                     0,
                     hy->pixelformat,
                     GL_UNSIGNED_BYTE,
                     jpeg_dec.data);
        pthread_mutex_unlock(&video_mutex);
    }
    else
    {
        // black screen
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glBindBuffer(GL_ARRAY_BUFFER, hy->array_buffer_fullscene_quad);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glFlush();
    CHECK_GL();
    glfwSwapBuffers(hy->window);
    if (show_render_time)
    {
        render_time = GetCurrentTimeInMilliSecond() - render_time;
        printf("Render time: %.1f ms (%.0f fps). Sony time:  %.1f ms (%.0f fps)  \r", render_time, 1000.0 / render_time, sony_fetch_time, 1000.0 / sony_fetch_time);
    }
}


void ProcessKeys(Hydra *hy)
{
    if (c_pressed)
    {
        hy->use_sony ^= 1;
        if (hy->use_sony == 1)
        {
            printf("Cam on.\n");
        }
        else
        {
            printf("Cam off.\n");
        }
        c_pressed = 0;
    }
    if (s_pressed)
    {
        mem.save ^= 1;
        if (mem.save == 1)
        {
            printf("Saving enabled.\n");
        }
        else
        {
            printf("Saving disabled.\n");
        }
        s_pressed = 0;
    }
    if (t_pressed)
    {
        show_render_time ^= 1;
        t_pressed             = 0;
    }
    if (space_pressed)
    {
        hy->freeze_frame ^= 1;
        if (hy->freeze_frame == 1)
        {
            printf("Freezing frame.\n");
        }
        else
        {
            printf("Unfreezing frame.\n");
        }
        space_pressed = 0;
    }
}


static int Hydra_Update(Hydra *hy)
{
    glfwPollEvents();
    ProcessKeys(hy);
    SetUniforms(hy);
    Hydra_Render(hy);
    return 0;
}


static void PrintHelp(void)
{
    printf("Key:\n");
    printf("  spacebar        		freeze frame\n");
    printf("  c                     sony on/off\n");
    printf("  s                     save jpeg on/off\n");
    printf("  t                     FPS printing\n");
    printf("  q                     exit\n");
}


static void Hydra_MainLoop(Hydra *hy)
{
    while (!glfwWindowShouldClose(hy->window))
    {
        switch (getchar())
        {
        // Freezes the current frame
        case ' ':
            hy->freeze_frame ^= 1;
            if (hy->freeze_frame == 1)
            {
                printf("Freezing frame.\n");
            }
            else
            {
                printf("Unfreezing frame.\n");
            }
            break;

        // Quits Hydra
        case 'Q':
        case 'q':
        case VEOF:  // Ctrl+d
        case VINTR: // Ctrl+c
        case 0x7f:  // Ctrl+c
        case 0x03:  // Ctrl+c
        case 0x1b:  // ESC
            printf("\nexit\n");
            goto goal;

        // Sony on / off
        case 'C':
        case 'c':
            hy->use_sony ^= 1;
            if (hy->use_sony == 1)
            {
                printf("Cam on.\n");
            }
            else
            {
                printf("Cam off.\n");
            }
            break;

        // Save JPEG on / off
        case 'S':
        case 's':
            mem.save ^= 1;
            if (mem.save == 1)
            {
                printf("Saving enabled.\n");
            }
            else
            {
                printf("Saving disabled.\n");
            }
            break;

        // Shows render time
        case 'T':
        case 't':
            show_render_time ^= 1;
            break;

        case '?':
            PrintHelp();

        default:
            break;
        }
        if (Hydra_Update(hy))
        {
            break;
        }
    }
    goal:
      return;
}


int Hydra_Main(Hydra *hy)
{
    pthread_create(&hy->thread, NULL, &getJpegData, &mem);
    Hydra_SetupShaders(hy);
    Hydra_MainLoop(hy);
    return EXIT_SUCCESS;
}


void Hydra_Destruct(Hydra *hy)
{
    printf("\nExiting.\n");
    hy->use_sony = 0;
    glDeleteProgram(hy->program);
    hy->program = 0;
    glDeleteShader(hy->fragment_shader);
    hy->fragment_shader = 0;
    glDeleteShader(hy->vertex_shader);
    hy->vertex_shader = 0;
    glDeleteBuffers(1, &hy->array_buffer_fullscene_quad);
    hy->array_buffer_fullscene_quad = 0;
    glDeleteTextures(1, &hy->sony_texture_name);
    hy->sony_texture_name = 0;
    free(hy->dirpath);
    free(hy->filename);
    curl_easy_cleanup(hy->curl_handle);
    curl_global_cleanup();
    free(mem.memory);
    free(mem.size_string);
    free(mem.savename);
    glfwDestroyWindow(hy->window);
    glfwTerminate();
}


int main(int argc, char *argv[])
{
    int ret;

    fcntl(0, F_SETFL, O_NONBLOCK);
    if (!glfwInit())
    {
        handleError("GLFW init failed", -1);
    }
    ret = EXIT_SUCCESS;
    if (argc == 1)
    {
        ShowUsage();
    }
    else
    {
        Hydra *hy;
        hy = malloc(Hydra_InstanceSize());
        Hydra_Construct(hy);
        if (Hydra_ParseArgs(hy, argc, (const char **)argv) == 0)
        {
            ret = Hydra_Main(hy);
        }
        Hydra_Destruct(hy);
        free(hy);
    }
    return ret;
}
