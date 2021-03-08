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
#include <mqtt_protocol.h>
#include <flatbuffers/flexbuffers.h>


#define UNUSED(A) (void)(A)

#define STATUS_CONNECTING 0
#define STATUS_CONNACK_RECVD 1
#define STATUS_WAITING 2
#define STATUS_DISCONNECTING 3
#define STATUS_DISCONNECTED 4
#define STATUS_NOHOPE 5

#define DEFAULT_MQTT_HOST "127.0.0.1"
#define DEFAULT_MQTT_PORT 1883
#define DEFAULT_MQTT_KEEPALIVE 60
#define DEFAULT_MQTT_TOPIC "EXAMPLE_TOPIC"

#define BUF_LENGTH 65536


struct mosq_config {
	char *id;
	int protocol_version;
	int keepalive;
	char *host;
	int port;
	int qos;
	bool retain;
	char *topic; /* pub, rr */
	bool clean_session;
	char *message; /* pub, rr */
	int msglen; /* pub, rr */
	int repeat_count; /* pub */
	struct timeval repeat_delay; /* pub */
	mosquitto_property *connect_props;
	mosquitto_property *publish_props;
	mosquitto_property *subscribe_props;
	mosquitto_property *unsubscribe_props;
	mosquitto_property *disconnect_props;
};

struct mosq_config cfg;

static int publish_count = 0;
static bool ready_for_repeat = false;
static volatile int status = STATUS_CONNECTING;


void usage(char *argv0)
{
	fprintf(stderr,
		"Usage: %s [-h host] [-p port]\n", argv0);
	exit(1);
}

void err_printf(const struct mosq_config *cfg, const char *fmt, ...)
{
	va_list va;
		
	va_start(va, fmt);
	vfprintf(stderr, fmt, va);
	va_end(va);
}

#ifdef _WINDOWS
static uint64_t next_publish_tv;

static void set_repeat_time(void)
{
	uint64_t ticks = GetTickCount64();
	next_publish_tv = ticks + cfg.repeat_delay.tv_sec * 1000 + cfg.repeat_delay.tv_usec / 1000;
}

static int check_repeat_time(void)
{
	uint64_t ticks = GetTickCount64();

	if (ticks > next_publish_tv) {
		return 1;
	}
	else {
		return 0;
	}
}
#else

static struct timeval next_publish_tv;

static void set_repeat_time(void)
{
	gettimeofday(&next_publish_tv, NULL);
	next_publish_tv.tv_sec += cfg.repeat_delay.tv_sec;
	next_publish_tv.tv_usec += cfg.repeat_delay.tv_usec;

	next_publish_tv.tv_sec += next_publish_tv.tv_usec / 1000000;
	next_publish_tv.tv_usec = next_publish_tv.tv_usec % 1000000;
}

static int check_repeat_time(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);

	if (tv.tv_sec > next_publish_tv.tv_sec) {
		return 1;
	}
	else if (tv.tv_sec == next_publish_tv.tv_sec
		&& tv.tv_usec > next_publish_tv.tv_usec) {

		return 1;
	}
	return 0;
}
#endif

void my_publish_callback(struct mosquitto *mosq, void *obj, int mid, int reason_code, const mosquitto_property *properties)
{
	char *reason_string = NULL;
	UNUSED(obj);
	UNUSED(properties);

	if (reason_code > 127) {
		fprintf(stderr, "Warning: Publish %d failed: %s.\n", mid, mosquitto_reason_string(reason_code));
		mosquitto_property_read_string(properties, MQTT_PROP_REASON_STRING, &reason_string, false);
		if (reason_string) {
			fprintf(stderr, "%s\n", reason_string);
			free(reason_string);
		}
	}
	publish_count++;

	if (publish_count < cfg.repeat_count) {
		ready_for_repeat = true;
		set_repeat_time();
	}
}

void my_connect_callback(struct mosquitto *mosq, void *obj, int result, int flags, const mosquitto_property *properties)
{
	int rc = MOSQ_ERR_SUCCESS;

	UNUSED(obj);
	UNUSED(flags);
	UNUSED(properties);
		
	if (!result) {
		
		rc = mosquitto_publish_v5(mosq, NULL, cfg.topic, 0, NULL, cfg.qos, cfg.retain, cfg.publish_props);
		
		if (rc) {
			switch (rc) {
			case MOSQ_ERR_INVAL:
				fprintf(stderr, "Error: Invalid input. Does your topic contain '+' or '#'?\n");
				break;
			case MOSQ_ERR_NOMEM:
				fprintf(stderr, "Error: Out of memory when trying to publish message.\n");
				break;
			case MOSQ_ERR_NO_CONN:
				fprintf(stderr, "Error: Client not connected when trying to publish.\n");
				break;
			case MOSQ_ERR_PROTOCOL:
				fprintf(stderr, "Error: Protocol error when communicating with broker.\n");
				break;
			case MOSQ_ERR_PAYLOAD_SIZE:
				fprintf(stderr, "Error: Message payload is too large.\n");
				break;
			case MOSQ_ERR_QOS_NOT_SUPPORTED:
				fprintf(stderr, "Error: Message QoS not supported on broker, try a lower QoS.\n");
				break;
			}
			mosquitto_disconnect_v5(mosq, 0, cfg.disconnect_props);
		}
	}
	else {
		if (result) {
			if (cfg.protocol_version == MQTT_PROTOCOL_V5) {
				if (result == MQTT_RC_UNSUPPORTED_PROTOCOL_VERSION) {
					fprintf(stderr, "Connection error: %s. Try connecting to an MQTT v5 broker, or use MQTT v3.x mode.\n", mosquitto_reason_string(result));
				}
				else {
					fprintf(stderr, "Connection error: %s\n", mosquitto_reason_string(result));
				}
			}
			else {
				fprintf(stderr, "Connection error: %s\n", mosquitto_connack_string(result));
			}

			// let the loop know that this is an unrecoverable connection
			status = STATUS_NOHOPE;

		}
	}
}

void my_disconnect_callback(struct mosquitto *mosq, void *obj, int rc, const mosquitto_property *properties)
{
	UNUSED(mosq);
	UNUSED(obj);
	UNUSED(rc);
	UNUSED(properties);

	if (rc == 0) {
		status = STATUS_DISCONNECTED;
	}
}


void client_config_cleanup(struct mosq_config *cfg)
{
	free(cfg->id);
	free(cfg->host);
	free(cfg->message);
	free(cfg->topic);
	
	mosquitto_property_free_all(&cfg->connect_props);
	mosquitto_property_free_all(&cfg->publish_props);
	mosquitto_property_free_all(&cfg->subscribe_props);
	mosquitto_property_free_all(&cfg->unsubscribe_props);
	mosquitto_property_free_all(&cfg->disconnect_props);
}



int main(int argc, char *argv[])
{

	flexbuffers::Builder fbb;
	struct mosquitto *mosq = NULL;
	int rc;

	//mosq_config
	cfg.id = NULL;
	cfg.qos = 0; ///qos - integer value 0, 1 or 2 indicating the Quality of Service to be used for the message.
	cfg.retain = true; ///	retain - set to true to make the message retained.
	cfg.clean_session = true;
	cfg.repeat_count = 2;
	
	//repeat Delay
	float f = 1 * 1.0e6f;
	cfg.repeat_delay.tv_sec = (int)f / 1000000;
	cfg.repeat_delay.tv_usec = (int)f % 1000000;

	cfg.host = strdup(DEFAULT_MQTT_HOST);
	cfg.port = DEFAULT_MQTT_PORT;
	cfg.topic = strdup(DEFAULT_MQTT_TOPIC);
	cfg.keepalive = DEFAULT_MQTT_KEEPALIVE;
	cfg.protocol_version = MQTT_PROTOCOL_V5;

	/* Parse options */
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-h"))
		{
			if (i == argc - 1) {
				fprintf(stderr, "Error: -h argument given but no host specified.");
				return 1;
			}
			else {
				free(cfg.host);
				cfg.host = strdup(argv[i + 1]);
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
				cfg.port = atoi(argv[i + 1]);
			}
			i++;
		}
		else
		{
			usage(argv[0]);
		}

	}


	mosquitto_lib_init();

	//Client Config Load
	rc = mosquitto_property_check_all(CMD_CONNECT, cfg.connect_props);
	if (rc) {
		err_printf(&cfg, "Error in CONNECT properties: %s\n", mosquitto_strerror(rc));
		return 1;
	}
	rc = mosquitto_property_check_all(CMD_PUBLISH, cfg.publish_props);
	if (rc) {
		err_printf(&cfg, "Error in PUBLISH properties: %s\n", mosquitto_strerror(rc));
		return 1;
	}
	rc = mosquitto_property_check_all(CMD_SUBSCRIBE, cfg.subscribe_props);
	if (rc) {
		err_printf(&cfg, "Error in SUBSCRIBE properties: %s\n", mosquitto_strerror(rc));
		return 1;
	}
	rc = mosquitto_property_check_all(CMD_UNSUBSCRIBE, cfg.unsubscribe_props);
	if (rc) {
		err_printf(&cfg, "Error in UNSUBSCRIBE properties: %s\n", mosquitto_strerror(rc));
		return 1;
	}
	rc = mosquitto_property_check_all(CMD_DISCONNECT, cfg.disconnect_props);
	if (rc) {
		err_printf(&cfg, "Error in DISCONNECT properties: %s\n", mosquitto_strerror(rc));
		return 1;
	}

	/* Create a new client instance.
	 * id = NULL -> ask the broker to generate a client id for us
	 * clean session = true -> the broker should remove old sessions when we connect
	 * obj = NULL -> we aren't passing any of our private data for callbacks
	 */
	mosq = mosquitto_new(cfg.id, cfg.clean_session, NULL);

	if (!mosq) {
		switch (errno) {
		case ENOMEM:
			fprintf(stderr, "Error: Out of memory.\n");
			break;
		case EINVAL:
			fprintf(stderr, "Error: Invalid id.\n");
			break;
		}
		client_config_cleanup(&cfg);
		mosquitto_destroy(mosq);
		mosquitto_lib_cleanup();
		return 1;
	}
	
	//client_option			
	mosquitto_int_option(mosq, MOSQ_OPT_PROTOCOL_VERSION, cfg.protocol_version);
	
	//callback
	mosquitto_publish_v5_callback_set(mosq, my_publish_callback);
	mosquitto_connect_v5_callback_set(mosq, my_connect_callback);
	mosquitto_disconnect_v5_callback_set(mosq, my_disconnect_callback);
	

	//connect
	rc = mosquitto_connect_bind_v5(mosq, cfg.host, cfg.port, cfg.keepalive, NULL, cfg.connect_props);
	if (rc != MOSQ_ERR_SUCCESS) {
		fprintf(stderr, "Error: %s\n", mosquitto_strerror(rc));
		
		client_config_cleanup(&cfg);
		mosquitto_destroy(mosq);
		mosquitto_lib_cleanup();
		
		return 1;
	}
	
	struct timeval tv;
	char buf[BUF_LENGTH];
	std::vector<uint8_t> flex_buf;

	//Loop
	int loop_delay = 1000;

	if (cfg.repeat_count > 1 && (cfg.repeat_delay.tv_sec == 0 || cfg.repeat_delay.tv_usec != 0)) {
		loop_delay = (int)cfg.repeat_delay.tv_usec / 2000;
	}
		
	do {
		rc = mosquitto_loop(mosq, loop_delay, 1);
		if (ready_for_repeat && check_repeat_time()) {
			rc = MOSQ_ERR_SUCCESS;

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

			rc = mosquitto_publish_v5(mosq, NULL, cfg.topic, flex_buf.size(), flex_buf.data(), cfg.qos, cfg.retain, cfg.publish_props);
			
			if (rc != MOSQ_ERR_SUCCESS) {
				fprintf(stderr, "Error publishing: %s\n", mosquitto_strerror(rc));
			}

		}
	
	} while (rc == MOSQ_ERR_SUCCESS);



	client_config_cleanup(&cfg);
	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();

	return 0;
}