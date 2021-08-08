
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

// MQTT-C
#include <mqtt.h>
#include "MQTT-C/examples/templates/posix_sockets.h"

// json-parser
#include "json.h"

// TermGL
#include "TermGL/src/termgl.h"
#include "TermGL/src/termgl3d.h"
#include "TermGL/src/termgl_vecmath.h"

/**
 * @brief The function will be called whenever a PUBLISH message is received.
 */
void consumer_callback(void** state, struct mqtt_response_publish *published);

/**
 * @brief The client's refresher. This function triggers back-end routines to 
 *        handle ingress/egress traffic to the broker.
 * 
 * @note All this function needs to do is call \ref __mqtt_recv and 
 *       \ref __mqtt_send every so often. I've picked 100 ms meaning that 
 *       client ingress/egress traffic will be handled every 100 ms.
 */
void* client_refresher(void* client);

typedef struct runtime_state {
    TGL* tgl;
    TGLTriangle* particle_obj_triangles;
    int n_particle_obj_triangles;
} runtime_state;

void render_particles(runtime_state* tgl, json_value* json_parsed);

/**
 * @brief Safelty closes the \p sockfd and cancels the \p client_daemon before \c exit. 
 */
void exit_example(int status, int sockfd, pthread_t *client_daemon);

int main(int argc, const char *argv[]) 
{
    const char* addr;
    const char* port;
    const char* topic;

    /* get address (argv[1] if present) */
    if (argc > 1) addr = argv[1];
    else addr = "127.0.0.1";

    /* get port number (argv[2] if present) */
    if (argc > 2) port = argv[2];
    else  port = "1701";

    /* get the topic name to publish */
    if (argc > 3) topic = argv[3];
    else topic = "bowerick/message/generator";

    /* open the non-blocking TCP socket (connecting to the broker) */
    int sockfd = open_nb_socket(addr, port);
    if (sockfd == -1) {
        perror("Failed to open socket: ");
        exit_example(EXIT_FAILURE, sockfd, NULL);
    }

    printf("Initializing TermGL...\n");
    TGL *tgl = tgl_init(80, 60, &gradient_min);
	tgl_enable(tgl, TGL_DOUBLE_CHARS);
	tgl3d_init(tgl);
	tgl_enable(tgl, TGL_CULL_FACE);
	tgl3d_cull_face(tgl, TGL_BACK | TGL_CCW);
	tgl_enable(tgl, TGL_Z_BUFFER);

    tgl3d_camera(tgl, 1.5f, 0.f, 5.f);
    TGLTransform *camera_t = tgl3d_get_transform(tgl);
	tgl3d_transform_scale(camera_t, 1.0f, 1.0f, 1.0f);
	tgl3d_transform_rotate(camera_t, 3.14f, 0.f, 0.f);
    tgl3d_transform_translate(camera_t, 0.f, 0.f, 2.f);
	tgl3d_transform_update(camera_t);

    TGLTriangle* particle_obj_triangles;
    int n_particle_obj_triangles = 8;
    particle_obj_triangles = malloc(sizeof(TGLTriangle) * n_particle_obj_triangles);
    int idx = 0;
    for (int x = -1; x <= 1; x += 2) {
        for (int y = -1; y <= 1; y += 2) {
            for (int z = -1; z <= 1; z += 2) {
                particle_obj_triangles[idx].vertices[0][0] = (float) x;
                particle_obj_triangles[idx].vertices[0][1] = 0.f;
                particle_obj_triangles[idx].vertices[0][2] = 0.f;
                particle_obj_triangles[idx].vertices[1][0] = 0.f;
                particle_obj_triangles[idx].vertices[1][1] = (float) y;
                particle_obj_triangles[idx].vertices[1][2] = 0.f;
                particle_obj_triangles[idx].vertices[2][0] = 0.f;
                particle_obj_triangles[idx].vertices[2][1] = 0.f;
                particle_obj_triangles[idx].vertices[2][2] = (float) z;
                idx++;
            }
        }
    }

    TGLTransform transform;
    tgl3d_transform_translate(&transform, 0.f, 0.f, 0.f);
    tgl3d_transform_scale(&transform, 0.2f, 0.2f, 0.2f);
    tgl3d_transform_rotate(&transform, 0.f, 0.f, 0.f);
    tgl3d_transform_update(&transform);
    for (int j=0; j < n_particle_obj_triangles; j++) {
        TGLTriangle temp;
        tgl3d_transform_apply(&transform, particle_obj_triangles[j].vertices, temp.vertices);

        memcpy(particle_obj_triangles[j].vertices, temp.vertices, sizeof(float[3][3]));
		memset(particle_obj_triangles[j].intensity, 255, 3);
    }

    runtime_state *state;
    state = malloc(sizeof(runtime_state));
    state->tgl = tgl;
    state->particle_obj_triangles = particle_obj_triangles;
    state->n_particle_obj_triangles = n_particle_obj_triangles;

    fprintf(stdout, "Connecting to: %s %s %s\n", addr, port, topic);
    /* setup a client */
    struct mqtt_client client = { .publish_response_callback_state=state };
    uint8_t sendbuf[2048]; /* sendbuf should be large enough to hold multiple whole mqtt messages */
    uint8_t recvbuf[1024000]; /* recvbuf should be large enough any whole mqtt message expected to be received */
    mqtt_init(&client, sockfd, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf), consumer_callback);
    /* Create an anonymous session */
    const char* client_id = NULL;
    /* Ensure we have a clean session */
    uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;
    /* Send connection request to the broker. */
    mqtt_connect(&client, client_id, NULL, NULL, 0, NULL, NULL, connect_flags, 400);

    /* check that we don't have any errors */
    if (client.error != MQTT_OK) {
        fprintf(stderr, "error: %s\n", mqtt_error_str(client.error));
        exit_example(EXIT_FAILURE, sockfd, NULL);
    }

    /* start a thread to refresh the client (handle egress and ingree client traffic) */
    pthread_t client_daemon;
    if(pthread_create(&client_daemon, NULL, client_refresher, &client)) {
        fprintf(stderr, "Failed to start client daemon.\n");
        exit_example(EXIT_FAILURE, sockfd, NULL);
    }

    /* subscribe */
    mqtt_subscribe(&client, topic, 0);

    /* start publishing the time */
    printf("%s listening for '%s' messages.\n", argv[0], topic);
    printf("Press CTRL-D to exit.\n\n");
    
    /* block */
    while(fgetc(stdin) != EOF); 
    
    /* disconnect */
    printf("\n%s disconnecting from %s\n", argv[0], addr);
    sleep(1);

    /* exit */ 
    exit_example(EXIT_SUCCESS, sockfd, &client_daemon);
}

void exit_example(int status, int sockfd, pthread_t *client_daemon)
{
    if (sockfd != -1) close(sockfd);
    if (client_daemon != NULL) pthread_cancel(*client_daemon);
    exit(status);
}

void consumer_callback(void** state, struct mqtt_response_publish *published) 
{
    char* data;
    char* err_buf;
    json_char* json_source;
    json_value* json_parsed;
    json_settings settings = { 0 };

    data = (char *) malloc(published->application_message_size + 1);
    memcpy(data, published->application_message, published->application_message_size);
    data[published->application_message_size] = '\0';
    json_source = (json_char*) data;

    err_buf = malloc(json_error_max);
    json_parsed = json_parse_ex(&settings, json_source, strlen(data), err_buf);

    if (json_parsed != NULL) {
        render_particles((runtime_state*) *state, json_parsed);
        json_value_free(json_parsed);
    } else {
        printf("Failed to parse JSON:\n");
        printf("%s\n", err_buf);
    }

    free(data);
    free(err_buf);
}

void* client_refresher(void* client)
{
    while(1) 
    {
        mqtt_sync((struct mqtt_client*) client);
        usleep(10000U);
    }
    return NULL;
}

void intermediate_shader(TGLTriangle *trig, void *data)
{
	TGLVec3 light_direction = {1.f, 1.f, 1.f};
	TGLTriangle *in = data;
	TGLVec3 ab, ac, cp;
	tgl_sub3v(in->vertices[1], in->vertices[0], ab);
	tgl_sub3v(in->vertices[2], in->vertices[0], ac);
	tgl_cross(ab, ac, cp);
	float dp = tgl_dot3(cp, light_direction);
	float light_mul;
	if (signbit(dp))
		light_mul = 0.15f;
	else
		light_mul = acosf(dp / (tgl_mag3(cp) * tgl_mag3(light_direction))) / -3.14159f + 1.f;
	trig->intensity[0] *= light_mul;
	trig->intensity[1] *= light_mul;
	trig->intensity[2] *= light_mul;
}

void render_particles(runtime_state* state, json_value* json_parsed) {
    float x = 0.0;
    float y = 0.0;
    float z = 0.0;
    float color_r = 1.0;
    float color_g = 1.0;
    float color_b = 1.0;
    float rotation_x = 0.0;
    float rotation_y = 0.0;
    float rotation_z = 0.0;
    float scale_x = 0.0;
    float scale_y = 0.0;
    float scale_z = 0.0;
    int n_particles = json_parsed->u.object.length;
    TGLTransform transform;
    TGL* tgl = state->tgl;
    TGLTriangle* particle_obj_triangles = state->particle_obj_triangles;
    int n_particle_obj_triangles = state->n_particle_obj_triangles;

    for (int i=0; i < n_particles; i++) {
        json_value* particle = json_parsed->u.array.values[i];

        for (unsigned int idx = 0; idx < particle->u.object.length; idx++) {
            json_object_entry val = particle->u.object.values[idx];

            if (!strcmp(val.name, "x")) x = (float) val.value->u.dbl;
            if (!strcmp(val.name, "y")) y = (float) val.value->u.dbl;
            if (!strcmp(val.name, "z")) z = (float) val.value->u.dbl;
            if (!strcmp(val.name, "color_r")) color_r = (float) val.value->u.dbl;
            if (!strcmp(val.name, "color_g")) color_g = (float) val.value->u.dbl;
            if (!strcmp(val.name, "color_b")) color_b = (float) val.value->u.dbl;
            if (!strcmp(val.name, "rotation_x")) rotation_x = (float) val.value->u.dbl;
            if (!strcmp(val.name, "rotation_y")) rotation_y = (float) val.value->u.dbl;
            if (!strcmp(val.name, "rotation_z")) rotation_z = (float) val.value->u.dbl;
            if (!strcmp(val.name, "scale_x")) scale_x = (float) val.value->u.dbl;
            if (!strcmp(val.name, "scale_y")) scale_y = (float) val.value->u.dbl;
            if (!strcmp(val.name, "scale_z")) scale_z = (float) val.value->u.dbl;
        }

        int color = TGL_WHITE;
        float color_threshold = 0.3f;
        if (color_r > color_threshold && color_g < color_threshold && color_b < color_threshold) color = TGL_RED;
        else if (color_r < color_threshold && color_g > color_threshold && color_b < color_threshold) color = TGL_GREEN;
        else if (color_r < color_threshold && color_g < color_threshold && color_b > color_threshold) color = TGL_BLUE;
        else if (color_r > color_threshold && color_g < color_threshold && color_b > color_threshold) color = TGL_PURPLE;
        else if (color_r < color_threshold && color_g > color_threshold && color_b > color_threshold) color = TGL_CYAN;
        else if (color_r > color_threshold && color_g > color_threshold && color_b < color_threshold) color = TGL_YELLOW;
        else if (color_r < color_threshold && color_g < color_threshold && color_b < color_threshold) color = TGL_BLACK;

        for (int j=0; j < n_particle_obj_triangles; j++) {
            tgl3d_transform_translate(&transform, x, y, z);
            tgl3d_transform_scale(&transform, scale_x, scale_y, scale_z);
            tgl3d_transform_rotate(&transform, rotation_x, rotation_y, rotation_z);
            tgl3d_transform_update(&transform);

            TGLTriangle temp;
            tgl3d_transform_apply(&transform, particle_obj_triangles[j].vertices, temp.vertices);
            memset(temp.intensity, 255, 3);

            tgl3d_shader(tgl, &temp, color, true, &temp, &intermediate_shader);
        }
    }

    tgl_flush(tgl);
    tgl_clear(tgl, TGL_FRAME_BUFFER | TGL_Z_BUFFER);
}
