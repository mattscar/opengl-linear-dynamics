#define VERTEX_SHADER "animate_sphere.vert"
#define FRAGMENT_SHADER "animate_sphere.frag"

#define INIT_POSITION 0.5f
#define INIT_VELOCITY 0.8f
#define ACCELERATION -0.4f

// OpenGL headers
#define GL3_PROTOTYPES
#include <GL3/gl3.h>
#define __gl_h_
#include <GL/freeglut.h>
#include <GL/glx.h>

// OpenGL Math Library headers
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp> 
#include <glm/gtc/type_ptr.hpp>

// Read from COLLADA files
#include "colladainterface.h"

#include <climits>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

struct LightParameters {
  glm::vec4 diffuse_intensity;
  glm::vec4 ambient_intensity;
  glm::vec4 light_direction;
};

glm::vec3 color = glm::vec3(0.0f, 0.0f, 1.0f);

// OpenGL variables
glm::mat4 modelview_matrix;       // The modelview matrix
glm::mat4 mvp_matrix;             // The combined modelview-projection matrix
glm::mat4 mvp_inverse;            // Inverse of the MVP matrix
std::vector<ColGeom> geom_vec;    // Vector containing COLLADA meshes
GLuint *vaos, *vbos, *ibos;       // OpenGL buffer objects
GLuint ubo;                       // OpenGL uniform buffer object
GLint color_location;             // Index of the color uniform
GLint mvp_location;               // Index of the modelview-projection uniform
GLint delta_location;             // Index of the delta uniform
float half_height, half_width;    // Window dimensions divided in half
unsigned num_objects;             // Number of meshes in the vector
size_t num_triangles;             // Number of triangles in the rendering

// Timing and physics
int start_time, previous_time;    // Timing used to determine motion
glm::vec3 init_position = glm::vec3(0.0f, INIT_POSITION, 0.0f);
glm::vec3 init_velocity = glm::vec3(INIT_VELOCITY, INIT_VELOCITY, 0.0f);
glm::vec3 acceleration = glm::vec3(0.0f, ACCELERATION, 0.0f);

// Read a character buffer from a file
std::string read_file(const char* filename) {

  // Open the file
  std::ifstream ifs(filename, std::ifstream::in);
  if(!ifs.good()) {
    std::cerr << "Couldn't find the source file " << filename << std::endl;
    exit(1);
  }
  
  // Read file text into string and close stream
  std::string str((std::istreambuf_iterator<char>(ifs)), 
                   std::istreambuf_iterator<char>());
  ifs.close();
  return str;
}

// Compile the shader
void compile_shader(GLint shader) {

  GLint success;
  GLsizei log_size;
  char *log;

  glCompileShader(shader);
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_size);
    log = new char[log_size+1];
    log[log_size] = '\0';
    glGetShaderInfoLog(shader, log_size+1, NULL, log);
    std::cout << log;
    delete(log);
    exit(1);
  }
}

// Create, compile, and deploy shaders
GLuint init_shaders(void) {

  GLuint vs, fs, prog;
  std::string vs_source, fs_source;
  const char *vs_chars, *fs_chars;
  GLint vs_length, fs_length;

  // Create shader descriptors
  vs = glCreateShader(GL_VERTEX_SHADER);
  fs = glCreateShader(GL_FRAGMENT_SHADER);   

  // Read shader text from files
  vs_source = read_file(VERTEX_SHADER);
  fs_source = read_file(FRAGMENT_SHADER);

  // Set shader source code
  vs_chars = vs_source.c_str();
  fs_chars = fs_source.c_str();
  vs_length = (GLint)vs_source.length();
  fs_length = (GLint)fs_source.length();
  glShaderSource(vs, 1, &vs_chars, &vs_length);
  glShaderSource(fs, 1, &fs_chars, &fs_length);

  // Compile shaders and chreate program
  compile_shader(vs);
  compile_shader(fs);
  prog = glCreateProgram();

  // Bind attributes
  glBindAttribLocation(prog, 0, "in_coords");
  glBindAttribLocation(prog, 1, "in_normals");

  // Attach shaders
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);

  glLinkProgram(prog);
  glUseProgram(prog);

  return prog;
}

// Create and initialize vertex array objects (VAOs)
// and vertex buffer objects (VBOs)
void init_buffers(GLuint program) {
  
  int loc;

  // Create a VAO for each geometry
  vaos = new GLuint[num_objects];
  glGenVertexArrays(num_objects, vaos);

  // Create two VBOs for each geometry
  vbos = new GLuint[2 * num_objects];
  glGenBuffers(2 * num_objects, vbos);

  // Create an IBO for each geometry
  ibos = new GLuint[num_objects];
  glGenBuffers(num_objects, ibos);

  // Configure VBOs to hold positions and normals for each geometry
  for(unsigned int i=0; i<num_objects; i++) {

    glBindVertexArray(vaos[i]);

    // Set vertex coordinate data
    glBindBuffer(GL_ARRAY_BUFFER, vbos[2*i]);
    glBufferData(GL_ARRAY_BUFFER, geom_vec[i].map["POSITION"].size, 
                 geom_vec[i].map["POSITION"].data, GL_STATIC_DRAW);
    loc = glGetAttribLocation(program, "in_coords");
    glVertexAttribPointer(loc, geom_vec[i].map["POSITION"].stride, 
                          geom_vec[i].map["POSITION"].type, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);

    // Set normal vector data
    glBindBuffer(GL_ARRAY_BUFFER, vbos[2*i+1]);
    glBufferData(GL_ARRAY_BUFFER, geom_vec[i].map["NORMAL"].size, 
                 geom_vec[i].map["NORMAL"].data, GL_STATIC_DRAW);
    loc = glGetAttribLocation(program, "in_normals");
    glVertexAttribPointer(loc, geom_vec[i].map["NORMAL"].stride, 
                          geom_vec[i].map["NORMAL"].type, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(1);

    // Set index data
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibos[i]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, 
                 geom_vec[i].index_count * sizeof(unsigned short), 
                 geom_vec[i].indices, GL_STATIC_DRAW);
  }
}

// Initialize uniform data
void init_uniforms(GLuint program) {

  GLuint program_index, ubo_index;
  struct LightParameters params;
  glm::mat4 trans_matrix, proj_matrix;

  // Configure the modelview matrix
  mvp_location = glGetUniformLocation(program, "mvp");
  modelview_matrix = glm::translate(glm::mat4(1.0f), glm::vec3(-2, -2, -5));

  // Set the initial position
  delta_location = glGetUniformLocation(program, "delta");
  glUniform3fv(delta_location, 1, &(init_position[0])); 

  // Set the color
  color_location = glGetUniformLocation(program, "color");
  glUniform3fv(color_location, 1, &(color[0]));

  // Initialize lighting data in uniform buffer object
  params.diffuse_intensity = glm::vec4(0.4f, 0.4f, 0.4f, 1.0f);
  params.ambient_intensity = glm::vec4(0.3f, 0.3f, 0.3f, 1.0f);
  params.light_direction = glm::vec4(0.0f, -1.0f, 0.5f, 1.0f);

  // Set the uniform buffer object
  glUseProgram(program);
  glGenBuffers(1, &ubo);
  glBindBuffer(GL_UNIFORM_BUFFER, ubo);
  glBufferData(GL_UNIFORM_BUFFER, 3*sizeof(glm::vec4), &params, GL_STREAM_DRAW);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
  glUseProgram(program);

  // Match the UBO to the uniform block
  glUseProgram(program);
  ubo_index = 0;
  program_index = glGetUniformBlockIndex(program, "LightParameters");
  glUniformBlockBinding(program, program_index, ubo_index);
  glBindBufferRange(GL_UNIFORM_BUFFER, ubo_index, ubo, 0, 3*sizeof(glm::vec4));
  glUseProgram(program);
}

// Initialize the OpenGL Rendering
void init_gl(int argc, char* argv[]) {

  // Initialize the main window
  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
  glutInitWindowSize(300, 300);
  glutCreateWindow("Animate Sphere");
  glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

  // Configure culling 
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);

  // Enable depth testing
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDepthFunc(GL_LEQUAL);
  glDepthRange(0.0f, 1.0f);

  // Initialize shaders and buffers
  GLuint program = init_shaders();
  init_buffers(program);
  init_uniforms(program);
}

// Respond to paint events
void display(void) {
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  //glBindVertexArray(vaos[0]);

  glDrawElements(geom_vec[0].primitive, geom_vec[0].index_count, 
                 GL_UNSIGNED_SHORT, 0);

  //glBindVertexArray(0);
  glutSwapBuffers();
}

// Respond to reshape events
void reshape(int w, int h) {

  // Set window dimensions
  half_width = (float)w/2; half_height = (float)h/2;

  // Update the matrix
  mvp_matrix = glm::ortho(-2.5f, 2.5f, -2.5f, 2.5f, 3.5f, 20.0f) * modelview_matrix;
  glUniformMatrix4fv(mvp_location, 1, GL_FALSE, glm::value_ptr(mvp_matrix[0]));

  // Compute the matrix inverse
  mvp_inverse = glm::inverse(mvp_matrix);

  // Set the viewport
  glViewport(0, 0, (GLsizei)w, (GLsizei)h);
}  

// Compute delta_r
void update_vertices() {

  int current_time;
  float delta_t;
  glm::vec3 delta_r;

  // Measure the elapsed time
	current_time = glutGet(GLUT_ELAPSED_TIME);
	delta_t = (current_time - start_time)/1000.0f;

  // Compute change in position
  delta_r = init_position + delta_t * init_velocity + 0.5f 
            * delta_t * delta_t * acceleration;

  // Set the uniform that identifies the location change
  if(delta_r.y > 0)
    glUniform3fv(delta_location, 1, &(delta_r[0]));

  // Notify application that window needs to be repainted
	glutPostRedisplay();
}

// Deallocate memory
void deallocate() {

  // Deallocate mesh data
  ColladaInterface::freeGeometries(&geom_vec);

  // Deallocate OpenGL objects
  glDeleteBuffers(num_objects, ibos);
  glDeleteBuffers(2 * num_objects, vbos);
  glDeleteBuffers(num_objects, vaos);
  glDeleteBuffers(1, &ubo);
  delete(ibos);
  delete(vbos);
  delete(vaos);
}

int main(int argc, char* argv[]) {

  // Initialize COLLADA geometries
  ColladaInterface::readGeometries(&geom_vec, "sphere.dae");
  num_objects = geom_vec.size();

  // Start OpenGL processing
  init_gl(argc, argv);

  // Set callback functions
  glutDisplayFunc(display);
  glutReshapeFunc(reshape);

  // Configure timing
  glutIdleFunc(update_vertices);
  start_time = glutGet(GLUT_ELAPSED_TIME);

  // Configure deallocation callback
  atexit(deallocate);

  // Start processing loop
  glutMainLoop();

  return 0;
}
