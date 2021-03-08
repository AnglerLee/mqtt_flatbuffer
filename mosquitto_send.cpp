/*
  mqtt_send
  Compile with:
  cc -I/usr/local/include -L/usr/local/lib -o mqtt_send mqtt_send.c -lmosquitto
  flatbuffer Compile:
   c++ -std=c++11 -Iflatbuffers/include -o text_flexbuf text_flexbuf.cpp
*/

#include <stdio.h>
#include <stdlib.h>
#if defined(_WINDOWS)
# include <windows.h>
#define sleep(x) Sleep((x)*1000)
#define strdup _strdup
#else
#include <getopt.h>
#include <unistd.h>
#include <sys/time.h>
#endif
#include <mosquitto.h>
#include <flatbuffers/flexbuffers.h>


#define DEFAULT_MQTT_HOST "127.0.0.1"
#define DEFAULT_MQTT_PORT 1883
#define DEFAULT_MQTT_KEEPALIVE 60
#define DEFAULT_MQTT_TOPIC "EXAMPLE_TOPIC"

#define BUF_LENGTH 65536

void usage(char *argv0)
{
	fprintf(stderr,
		"Usage: %s [-h host] [-p port]\n", argv0);
	exit(1);
}


void connect_callback(struct mosquitto *mosq, void *obj, int result) {
	printf("connect callback, rc=%d\n", result);
}

void message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg) {
	printf("message '%.*s' for topic '%s'\n", msg->payloadlen, (char *)msg->payload, msg->topic);
	//mosquitto_topic_matches_sub("/devices/test/+", msg->topic, &match);
}


int main(int argc, char **argv)
{
	int rc;
	char *mqtt_host = strdup(DEFAULT_MQTT_HOST);
	char *mqtt_topic = strdup(DEFAULT_MQTT_TOPIC);
	int mqtt_port = DEFAULT_MQTT_PORT;
	int mqtt_keepalive = DEFAULT_MQTT_KEEPALIVE;

	int mdelay = 0;
	bool clean_session = true;

	flexbuffers::Builder fbb;

	/* Parse options */
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-h"))
		{
			if (i == argc - 1) {
				fprintf(stderr, "Error: -h argument given but no host specified.");
				return 1;
			}
			else {
				free(mqtt_host);
				mqtt_host = strdup(argv[i + 1]);
			}
			i++;
		}
		else if (!strcmp(argv[i], "-p"))
		{
			if (i == argc - 1) {
				fprintf(stderr, "Error: -p argument given but no port specified.");
				return 1;
			}
			else {
				mqtt_port = atoi(argv[i + 1]);
			}
			i++;
		}
		else
		{
			usage(argv[0]);
		}

	}
	
	struct mosquitto *mosq = NULL;
	mosquitto_lib_init();
	mosq = mosquitto_new(NULL, clean_session, NULL);
	if (!mosq) {
		fprintf(stderr, "Could not create new mosquitto struct\n");
		exit(1);
	}

	if (mosquitto_connect(mosq, mqtt_host, mqtt_port, mqtt_keepalive)) {
		fprintf(stderr, "Unable to connect mosquitto.\n");
		exit(1);
	}

#ifndef _WINDOWS
	int loop = mosquitto_loop_start(mosq);
	if (loop != MOSQ_ERR_SUCCESS) {
		fprintf(stderr, "Unable to start mosquitto loop: %s\n", mosquitto_strerror(loop));
		exit(1);
	}

#endif

	struct timeval tv;
	char buf[BUF_LENGTH];
	std::vector<uint8_t> flex_buf;
	
	do {
		rc = mosquitto_loop(mosq, -1, 1);

		scanf_s("%s", buf, BUF_LENGTH);
		if (!strcmp(buf, "exit")) break;


#ifndef _WINDOWS
		gettimeofday(&tv, NULL);
		double timestamp = tv.tv_sec + 1e-6*tv.tv_usec;
#else
		uint64_t ticks = GetTickCount64();
		double timestamp = (double)ticks;
#endif

		fbb.Clear();
		fbb.Map([&]() {
			fbb.Double("time", timestamp);
			fbb.String("text", buf);
		});
		fbb.Finish();				
		
		flex_buf = fbb.GetBuffer();

		rc = mosquitto_publish(mosq, NULL, mqtt_topic, flex_buf.size(), flex_buf.data(), 0, 0);
		if (rc != MOSQ_ERR_SUCCESS) {
			fprintf(stderr, "Error publishing: %s\n", mosquitto_strerror(rc));
		}
		
	} while (rc == MOSQ_ERR_SUCCESS);

	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();
	free(mqtt_host);
	free(mqtt_topic);

	return 0;
}
