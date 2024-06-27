#include <GL/glew.h>
#include <SDL.h>
#include <getopt.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <GL/gl.h>
#include <GL/glu.h>

#include "ku_gl_floatbuffer.c"
#include "ku_gl_shader.c"
#include "mt_log.c"
#include "mt_math_3d.c"
#include "mt_matrix_4d.c"
#include "mt_time.c"
#include "mt_vector.c"
#include "mt_vector_2d.c"
#include "mt_vector_3d.c"

#ifdef EMSCRIPTEN
    #include <emscripten.h>
    #include <emscripten/html5.h>
#endif

#define WTH 800.0
#define HTH 1000.0

char  quit  = 0;
float scale = 1.0;

int32_t width  = 1000;
int32_t height = 750;

SDL_Window*   window;
SDL_GLContext context;

ku_floatbuffer_t* floatbuffer;
matrix4array_t    projection = {0};
float             angle      = 0.0;
glsha_t           sha;
uint32_t          start_time;
uint32_t          frames = 0;

m4_t pers;

typedef struct _cube_t cube_t;
struct _cube_t
{
    v3_t     tlf;  // top left front coord
    v3_t     brb;  // bottom right back coord
    float    size; // side size
    uint32_t color;
    cube_t*  nodes[8];
};

/* creates a cube structure on heap */

void cube_delete(void* p)
{
    cube_t* cube = p;
    for (int i = 0; i < 8; i++)
    {
	if (cube->nodes[i] != NULL)
	{
	    REL(cube->nodes[i]);
	}
    }
}

void cube_describe(void* p, int level)
{
    cube_t* cube = p;
    cube_t  c    = *cube;

    printf("\nCube TLF %f %f %f BRB %f %f %f SIZE %f COLOR %08x", c.tlf.x, c.tlf.y, c.tlf.z, c.brb.x, c.brb.y, c.brb.z, c.size, c.color);
    for (int i = 0; i < 8; i++)
    {
	if (c.nodes[i])
	    printf("Y");
	else
	    printf("N");
    }
}

cube_t* cube_create(uint32_t color, v3_t tlf, v3_t brb)
{
    cube_t* cube = CAL(sizeof(cube_t), cube_delete, cube_describe);

    cube->color = color;
    cube->size  = brb.x - tlf.x;
    cube->tlf   = tlf;
    cube->brb   = brb;

    return cube;
}

/* inserts new cube for a point creating the intermediate octree */

void cube_insert(cube_t* cube, v3_t point, uint32_t color)
{
    if (cube->size > 2.0)
    {
	if (cube->tlf.x <= point.x && point.x < cube->brb.x &&
	    cube->tlf.y <= point.y && point.y < cube->brb.y &&
	    cube->tlf.z <= point.z && point.z < cube->brb.z)
	{
	    /*B   4 5  */
	    /*    6 7  */
	    /*F 0 1    */
	    /*  2 3    */
	    /* do speed tests on static const vs simple vars */
	    static const int focts[2] = {2, 3};
	    static const int bocts[4] = {4, 5, 6, 7};

	    static const float xsizes[8] = {0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0};
	    static const float ysizes[8] = {0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 1.0, 1.0};
	    static const float zsizes[8] = {0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0};

	    int octet    = 0;
	    float halfsize  = cube->size / 2.0;

	    if (cube->tlf.x + halfsize < point.x) octet = 1;
	    if (cube->tlf.y + halfsize < point.y) octet = focts[octet];
	    if (cube->tlf.z + halfsize < point.z) octet = bocts[octet];

	    if (cube->nodes[octet] == NULL)
	    {
		float x = cube->tlf.x + xsizes[octet] * halfsize;
		float y = cube->tlf.y + ysizes[octet] * halfsize;
		float z = cube->tlf.z + zsizes[octet] * halfsize;

		cube->nodes[octet] = cube_create(
		    color,
		    (v3_t){x, y, z},
		    (v3_t){x + halfsize, y + halfsize, z + halfsize});

		printf("inserting into cube %.2f %.2f %.2f %.2f %.2f %.2f %.2f at octet %i\n", cube->tlf.x, cube->tlf.y, cube->tlf.z, cube->brb.x, cube->brb.y, cube->brb.z, cube->size, octet);
	    }

	    cube_insert(cube->nodes[octet], point, color);

	    /* current color will be that last existing subnode's color
	       TODO : should  be the average of node colors */

	    for (int i = 0; i < 8; i++)
	    {
		if (cube->nodes[i] != NULL)
		{
		    cube->color = cube->nodes[i]->color;
		}
	    }
	}
    }
}

/* get cubes intersected by a line with given size
   multiple cubes can be returned if first cube is transparent
   in case of refraction modify vector a little */

void cube_trace(cube_t* root, v3_t base, v3_t vector, float size)
{
}

cube_t* basecube;

void collect_visible_cubes(cube_t* cube)
{
    int scr_ww = 6; // screen window width
    int scr_wh = 4; // screen window height

    float cam_ww = 100.0; // camera window width
    float cam_wh = 80.0;  // camera window height

    v3_t cam_fp = (v3_t){0.0, 0.0, 200.0}; // camera focus point
    v3_t cam_tp = (v3_t){0.0, 0.0, 0.0};   // camera target point

    v3_t cam_v = v3_sub(cam_fp, cam_tp); // camera vector
    v3_t ver_v = {0.0, 1.0, 0.0};        // vertical vector

    v3_t scr_hv = v3_resize(v3_cross(cam_v, ver_v), cam_ww);  // screen horizontal vector
    v3_t scr_vv = v3_resize(v3_cross(cam_v, scr_hv), cam_wh); // screen vertical vector

    v3_t sl  = v3_add(cam_tp, v3_resize(scr_hv, cam_ww / 2.0)); // screen left
    v3_t slt = v3_add(sl, v3_resize(scr_vv, cam_wh / 2.0));     // screen left top

    mt_log_debug("screen left top %f %f %f", slt.x, slt.y, slt.z);

    for (int y = 0; y < scr_wh; y++)
    {
	float vr = (cam_wh / scr_wh) * (float) y;
	for (int x = 0; x < scr_ww; x++)
	{
	    float hr = (cam_ww / scr_ww) * (float) x;

	    v3_t cp = v3_add(slt, v3_resize(scr_hv, -hr)); // current point
	    cp      = v3_add(cp, v3_resize(scr_vv, -vr));

	    printf("%i:%i - %.3f %.3f %.3f\n", y, x, cp.x, cp.y, cp.z);

	    v3_t csv = v3_sub(cp, cam_fp); // current screen vector

	    // get color of closest voxel at given detail

	    cube_trace(cube, cp, csv, 50.0);
	}
    }
}

void GLAPIENTRY
MessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
{
    mt_log_debug("GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n", (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""), type, severity, message);
}

void main_init()
{
    srand((unsigned int) time(NULL));

    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(MessageCallback, 0);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE);

    // program

    char* vsh =
	"#version 100\n"
	"attribute vec3 position;"
	"uniform mat4 projection;"
	"void main ( )"
	"{"
	"    gl_PointSize = 2.0;"
	"    gl_Position = projection * vec4(position,1.0);"
	"}";

    char* fsh =
	"#version 100\n"
	"void main( )"
	"{"
	" gl_FragColor = vec4(1.0,1.0,1.0,1.0);"
	"}";

    sha = ku_gl_shader_create(
	vsh,
	fsh,
	1,
	((const char*[]){"position"}),
	1,
	((const char*[]){"projection"}));

    glUseProgram(sha.name);

    // vertex buffer

    GLuint vbo;

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    floatbuffer = ku_floatbuffer_new();

    // perspective

    float screenx      = 1920;
    float screeny      = 1080;
    float scale        = 1.0;
    float camera_fov_y = M_PI / 3.0;
    float camera_eye_z = (screeny / 2.0) / (tanf(camera_fov_y / 2.0));

    float min = camera_eye_z - (HTH / 2.0) * scale;
    float max = camera_eye_z + (HTH / 2.0) * scale;
    if (min < 10.0)
	min = 10.0;

    // m4_t pers = m4_defaultperspective(camera_fov_y, screenx / screeny, min, max);
    // m4_t pers = m4_defaultortho(0.0, screenx, 0.0, screeny, -10, 10);
    pers              = m4_defaultperspective(camera_fov_y, screenx / screeny, 0.1, 500);
    projection.matrix = pers;

    basecube = cube_create(
	0,
	(v3_t){0.0, 0.0, 0.0},
	(v3_t){100.0, 100.0, 100.0});

    cube_insert(basecube, (v3_t){10.0, 10.0, 10.0}, 0xFFFFFFFF);

    glUniformMatrix4fv(sha.uni_loc[0], 1, 0, projection.array);

    collect_visible_cubes(basecube);

    // upload vertex data

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, 0);

    for (int i = 0; i < 2; i++)
    {
	float data[3];
	data[0] = -100.0 + (rand() % 200000) / 1000.0;
	data[1] = -100.0 + (rand() % 200000) / 1000.0;
	data[2] = -400.0 + (rand() % 200000) / 1000.0;

	ku_floatbuffer_add(floatbuffer, data, 3);
    };

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * floatbuffer->pos, floatbuffer->data, GL_DYNAMIC_DRAW);

    // draw

    glViewport(0, 0, screenx, screeny);

    mt_log_debug("main init");
}

void main_free()
{
    REL(basecube);
}

char drag = 0;

int main_loop(double time, void* userdata)
{
    SDL_Event event;

    while (SDL_PollEvent(&event) != 0)
    {
	if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP || event.type == SDL_MOUSEMOTION)
	{
	    int x = 0, y = 0;
	    SDL_GetMouseState(&x, &y);

	    v2_t dimensions = {.x = x * scale, .y = y * scale};

	    if (event.type == SDL_MOUSEBUTTONDOWN)
	    {
		drag = 1;
	    }
	    else if (event.type == SDL_MOUSEBUTTONUP)
	    {
		drag = 0;
	    }
	    else if (event.type == SDL_MOUSEMOTION && drag == 1)
	    {
	    }
	}
	else if (event.type == SDL_QUIT)
	{
	    quit = 1;
	}
	else if (event.type == SDL_WINDOWEVENT)
	{
	    if (event.window.event == SDL_WINDOWEVENT_RESIZED)
	    {
		width  = event.window.data1;
		height = event.window.data2;

		v2_t dimensions = {.x = width * scale, .y = height * scale};
	    }
	}
	else if (event.type == SDL_KEYUP)
	{
	    switch (event.key.keysym.sym)
	    {
		case SDLK_f:
		    break;

		case SDLK_ESCAPE:
		    break;
	    }
	}
	else if (event.type == SDL_APP_WILLENTERFOREGROUND)
	{
	}
    }

    // update simulation

    glClearColor(0.2, 0.0, 0.0, 0.6);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m4_t trans100_matrix  = m4_defaulttranslation(0.0, 0.0, 300.0);
    m4_t transm100_matrix = m4_defaulttranslation(0.0, 0.0, -300.0);

    m4_t angle_matrix = m4_defaultrotation(0.0, angle, 0.0);

    m4_t obj_matrix = trans100_matrix;
    obj_matrix      = m4_multiply(angle_matrix, obj_matrix);
    obj_matrix      = m4_multiply(transm100_matrix, obj_matrix);
    obj_matrix      = m4_multiply(pers, obj_matrix);

    angle += 0.001;

    projection.matrix = obj_matrix;

    glUniformMatrix4fv(sha.uni_loc[0], 1, 0, projection.array);

    glDrawArrays(GL_POINTS, 0, floatbuffer->pos / 3);

    SDL_GL_SwapWindow(window);

    if (SDL_GetTicks() > start_time + 1000)
    {
	mt_log_debug("fps : %u", frames);
	frames     = 0;
	start_time = SDL_GetTicks();
    }

    frames++;

    return 1;
}

int main(int argc, char* argv[])
{
    mt_log_use_colors(isatty(STDERR_FILENO));
    mt_log_level_info();
    mt_time(NULL);

    printf("VoxBender v" VOXBENDER_VERSION " by Milan Toth ( www.milgra.com )\n");

    const char* usage =
	"Usage: cortex [options]\n"
	"\n"
	"  -h, --help                          Show help message and quit.\n"
	"  -v                                  Increase verbosity of messages, defaults to errors and warnings only.\n"
	"\n";

    const struct option long_options[] = {
	{"help", no_argument, NULL, 'h'},
	{"verbose", no_argument, NULL, 'v'}};

    int option       = 0;
    int option_index = 0;

    while ((option = getopt_long(argc, argv, "vh", long_options, &option_index)) != -1)
    {
	switch (option)
	{
	    case '?': printf("parsing option %c value: %s\n", option, optarg); break;
	    case 'v': mt_log_inc_verbosity(); break;
	    default: fprintf(stderr, "%s", usage); return EXIT_FAILURE;
	}
    }

    // enable high dpi

    SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");

    // init sdl

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0)
    {
	// setup opengl version

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

	/* window size should be full screen on phones, scaled down on desktops */

	SDL_DisplayMode displaymode;
	SDL_GetCurrentDisplayMode(0, &displaymode);

	if (displaymode.w < 800 || displaymode.h < 400)
	{
	    width  = displaymode.w;
	    height = displaymode.h;
	}
	else
	{
	    width  = displaymode.w * 0.8;
	    height = displaymode.h * 0.8;
	}

	// create window

	window = SDL_CreateWindow(
	    "VoxBender",
	    SDL_WINDOWPOS_UNDEFINED,
	    SDL_WINDOWPOS_UNDEFINED,
	    width,
	    height,
	    SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);

	if (window != NULL)
	{
	    // create context

	    context = SDL_GL_CreateContext(window);

	    if (context != NULL)
	    {
		GLint GlewInitResult = glewInit();
		if (GLEW_OK != GlewInitResult)
		    mt_log_error("%s", glewGetErrorString(GlewInitResult));

		// calculate scaling

		int nw;
		int nh;

		SDL_GL_GetDrawableSize(window, &nw, &nh);

		scale = nw / width;

		// try to set up vsync

		if (SDL_GL_SetSwapInterval(0) < 0)
		    mt_log_error("SDL swap interval error %s", SDL_GetError());

		main_init();

#ifdef EMSCRIPTEN
		// setup the main thread for the browser and release thread with return
		emscripten_request_animation_frame_loop(main_loop, 0);
		return 0;
#else
		// infinite loop til quit
		while (!quit)
		{
		    main_loop(0, NULL);
		}
#endif

		main_free();

		// cleanup

		SDL_GL_DeleteContext(context);
	    }
	    else
		mt_log_error("SDL context creation error %s", SDL_GetError());

	    // cleanup

	    SDL_DestroyWindow(window);
	}
	else
	    mt_log_error("SDL window creation error %s", SDL_GetError());

	// cleanup

	SDL_Quit();
    }
    else
	mt_log_error("SDL init error %s", SDL_GetError());

    return 0;
}
