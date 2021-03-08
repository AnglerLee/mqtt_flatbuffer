/*
  mqtt_recv
  Compile with:
  cc -I/usr/local/include -L/usr/local/lib -o mqtt_recv mqtt_recv.c -lmosquitto
  flatbuffer Compile2:
  c++ -std=c++11 -I flatbuffers/include -o flexbuf_out flexbuf_out.cpp flatbuffers/src/util.cpp
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
#endif
#include <signal.h>

#include <mosquitto.h>
#include <flatbuffers/flexbuffers.h>

#define IN_BUF_LENGTH 65536

#define DEFAULT_MQTT_HOST "127.0.0.1"
#define DEFAULT_MQTT_PORT 1883
#define DEFAULT_MQTT_KEEPALIVE 60
#define DEFAULT_MQTT_TOPIC "EXAMPLE_TOPIC"

static bool run = true;

void usage(char *argv0)
{
	fprintf(stderr,
		"Usage: %s [-h host] [-p port]\n", argv0);
	exit(1);
}

void signal_handler(int s) {
	run = false;
}

void connect_callback(struct mosquitto *mosq, void *obj, int result) {
	printf("connect callback, rc=%d\n", result);
}

void message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg) {
	
	if (msg->payloadlen == 0) return;

	fprintf(stdout, "topic '%s': message %d bytes\n", msg->topic, msg->payloadlen);
	
	//fprintf(stderr, "message : '%s'\n", (char *)msg->payload);

	unsigned char in_buf[IN_BUF_LENGTH];
	memcpy_s(in_buf, IN_BUF_LENGTH, msg->payload, msg->payloadlen);
	std::vector<uint8_t> buf(in_buf, in_buf + msg->payloadlen);
	
	auto map = flexbuffers::GetRoot(buf).AsMap();
	fprintf(stdout, "Map size: %zu\n", map.size());

	auto keys = map.Keys();
	auto values = map.Values();

	for (int i = 0; i < keys.size(); i++) {
		auto key = keys[i];
		auto val = values[i];
		
		fprintf(stderr, "Key[%d]: %s : %s\n", i, key.AsString().c_str(),
			val.ToString().c_str());
	}

	
	//write(fileno(stdout), (char *)msg->payload, msg->payloadlen);
	//mosquitto_topic_matches_sub("/devices/test/+", msg->topic, &match);
}

int main(int argc, char **argv)
{
	char *mqtt_host = strdup(DEFAULT_MQTT_HOST);
	char *mqtt_topic = strdup(DEFAULT_MQTT_TOPIC);
	int mqtt_port = DEFAULT_MQTT_PORT;
	int mqtt_keepalive = DEFAULT_MQTT_KEEPALIVE;


	int mdelay = 0;
	bool clean_session = true;

	/* Parse options */
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-h"))
		{
			if (i == argc - 1) {
				fprintf(stderr, "Error: -h argument given but no host specified.");
				return 1;
			}
			else {
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


	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	struct mosquitto *mosq = NULL;
	mosquitto_lib_init();
	mosq = mosquitto_new(NULL, clean_session, NULL);
	if (!mosq) {
		fprintf(stderr, "Could not create new mosquitto struct\n");

		exit(1);
	}

	mosquitto_connect_callback_set(mosq, connect_callback);
	mosquitto_message_callback_set(mosq, message_callback);

	if (mosquitto_connect(mosq, mqtt_host, mqtt_port, mqtt_keepalive)) {
		fprintf(stderr, "Unable to connect mosquitto.\n");

		exit(1);
	}

	mosquitto_subscribe(mosq, NULL, mqtt_topic, 0);

	while (run) {
		int loop = mosquitto_loop(mosq, -1, 1);
		if (loop) {
			fprintf(stderr, "mosquitto connection error!\n");
			sleep(1);
			mosquitto_reconnect(mosq);
		}
	}

	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();
	free(mqtt_host);
	free(mqtt_topic);


	return 0;
}