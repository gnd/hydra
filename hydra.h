#include <time.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <jerror.h>
#include <assert.h>
#include <jpeglib.h>
#include <stdbool.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <curl/curl.h>
#include <GLFW/glfw3.h>

#define USE_TERMIOS
#define GL_GLEXT_PROTOTYPES
#define STATIC_ASSERT(cond) { extern int __static_assert[(cond) ? 1 : -1]; }
#define ARRAY_SIZEOF(arr) (sizeof(arr) / sizeof(arr[0]))

typedef struct Hydra_ Hydra;

typedef struct JpegMemory_s {
								CURL *curl_handle;
								unsigned char *memory;
								char *size_string;
								size_t size;
								size_t jpeg_size;
								int save;
								bool header_found;
} JpegMemory_t;

typedef struct JpegDec_s {
								unsigned long x;
								unsigned long y;
								unsigned short int bpp;           // bits per pixels
								unsigned char* data;              // the data of the image
								unsigned long size;               // length of the file
								int channels;                     // the channels of the image 3 = RGA 4 = RGBA
} JpegDec_t;

typedef enum {
								LAYOUT_PRIMARY_FULLSCREEN,
								LAYOUT_PRIMARY_RESOLUTION,
								LAYOUT_SECONDARY_RESOLUTION,
								LAYOUT_SECONDARY_FULLSCREEN
} LAYOUT;

struct MemoryStruct {
								char *memory;
								size_t size;
};

struct Hydra_ {
								CURL *curl_handle;
								pthread_t thread;
								bool thread_running;
								int is_fullscreen;
								int use_sony;
								int freeze_frame;
								double time_origin;
								unsigned int frame;
								int show_render_time;

								// the GL part
								GLFWwindow* window;
								struct {
									int x;
									int y;
								} viewport;
								LAYOUT layout;
								GLuint textures[1];
								GLuint array_buffer_fullscene_quad;
								GLuint vertex_coord;
								GLuint fragment_shader;
								GLuint vertex_shader;
								GLuint program;
								GLuint resolution;
								GLuint sony_uniform;
								GLuint sony_texture_name;
								GLuint sony_texture_unit;
								int texture_width;
								int texture_height;
								int bytes_per_pixel;                                                                                       // int 2 for YUV422
						 		GLint internal_format;
								GLenum pixelformat;
};

size_t Hydra_InstanceSize(void);
int Hydra_Construct(Hydra *hy);
void Hydra_Destruct(Hydra *hy);
int Hydra_ParseArgs(Hydra *hy, int argc, const char *argv[]);
int Hydra_Main(Hydra *hy);
void Hydra_Usage(void);

void SetupViewport(Hydra *hy);
void SetLayout(Hydra *hy, LAYOUT layout, int width, int height);
void Render(Hydra *hy);
void GetSourceSize(Hydra *hy, int *out_width, int *out_height);

static size_t SonyCallback(void *contents, size_t size, size_t nmemb, void *userp);
void SaveJPEG(JpegMemory_t *mem);
void LoadJPEG(const unsigned char * imgdata, JpegDec_t* jpeg_dec, size_t jpeg_size);
