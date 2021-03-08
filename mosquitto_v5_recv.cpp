#include <stdio.h>
#include <stdlib.h>
#if defined(_WINDOWS)
# include <windows.h>
#define sleep(x) Sleep((x)*1000)
#define strdup _strdup
#else
#include <getopt.h>
#include <unistd.h>
#include <signal.h>	
#include <sys/time.h>	
#endif
#include <mosquitto.h>
#include <mqtt_protocol.h>
#include <flatbuffers/flexbuffers.h>


#define UNUSED(A) (void)(A)

#define DEFAULT_MQTT_HOST "127.0.0.1"
#define DEFAULT_MQTT_PORT 1883
#define DEFAULT_MQTT_KEEPALIVE 60


#define BUF_LENGTH 65536


struct mosq_config {
	char *id;
	int protocol_version;
	int keepalive;
	char *host;
	int port;
	int qos;
	bool retain;
	bool clean_session;
	bool debug;
	bool quiet;
	char **topics; /* sub, rr */
	int topic_count; /* sub, rr */
	char **unsub_topics; /* sub */
	int unsub_topic_count; /* sub */
	int sub_opts; /* sub */
	mosquitto_property *connect_props;
	mosquitto_property *publish_props;
	mosquitto_property *subscribe_props;
	mosquitto_property *unsubscribe_props;
	mosquitto_property *disconnect_props;
};

struct mosq_config cfg;

int last_mid = 0;
static bool timed_out = false;
static int connack_result = 0;
bool connack_received = false;


void usage(char *argv0)
{
	fprintf(stderr,
		"Usage: %s [-h host] [-p port]\n", argv0);
	exit(1);
}


#ifndef _WINDOWS
void my_signal_handler(int signum)
{
	if (signum == SIGALRM || signum == SIGTERM || signum == SIGINT) {
		if (connack_received) {
			process_messages = false;
			mosquitto_disconnect_v5(g_mosq, MQTT_RC_DISCONNECT_WITH_WILL_MSG, cfg.disconnect_props);
		}
		else {
			exit(-1);
		}
	}
	if (signum == SIGALRM) {
		timed_out = true;
	}
}
#endif

void err_printf(const struct mosq_config *cfg, const char *fmt, ...)
{
	va_list va;

	if (cfg->quiet) return;

	va_start(va, fmt);
	vfprintf(stderr, fmt, va);
	va_end(va);
}

int cfg_add_topic(struct mosq_config *cfg, char *topic)
{
	if (mosquitto_validate_utf8(topic, (int)strlen(topic))) {
		fprintf(stderr, "Error: Malformed UTF-8 in argument.\n\n");
		return 1;
	}

	if (mosquitto_sub_topic_check(topic) == MOSQ_ERR_INVAL) {
		fprintf(stderr, "Error: Invalid subscription topic '%s', are all '+' and '#' wildcards correct?\n", topic);
		return 1;
	}
	cfg->topic_count++;
	cfg->topics = (char **)realloc(cfg->topics, (size_t)cfg->topic_count * sizeof(char *));
	if (!cfg->topics) {
		err_printf(cfg, "Error: Out of memory.\n");
		return 1;
	}
	cfg->topics[cfg->topic_count - 1] = strdup(topic);

	return 0;
}

int cfg_unsub_topic(struct mosq_config *cfg, char *topic)
{
	if (mosquitto_validate_utf8(topic, (int)strlen(topic))) {
		fprintf(stderr, "Error: Malformed UTF-8 in argument.\n\n");
		return 1;
	}

	if (mosquitto_sub_topic_check(topic) == MOSQ_ERR_INVAL) {
		fprintf(stderr, "Error: Invalid unsubscribe topic '%s', are all '+' and '#' wildcards correct?\n", topic);
		return 1;
	}
	cfg->unsub_topic_count++;
	cfg->unsub_topics = (char **)realloc(cfg->unsub_topics, (size_t)cfg->unsub_topic_count * sizeof(char *));
	if (!cfg->unsub_topics) {
		err_printf(cfg, "Error: Out of memory.\n");
		return 1;
	}
	cfg->unsub_topics[cfg->unsub_topic_count - 1] = strdup(topic);

	return 0;
}

void my_publish_callback(struct mosquitto *mosq, void *obj, int mid, int reason_code, const mosquitto_property *properties)
{
	UNUSED(obj);
	UNUSED(reason_code);
	UNUSED(properties);

	if ((mid == last_mid || last_mid == 0)) {
		mosquitto_disconnect_v5(mosq, 0, cfg.disconnect_props);
	}
}

void my_message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg, const mosquitto_property *properties)
{
	UNUSED(obj);
	UNUSED(properties);
	
	if (msg->payloadlen == 0) return;

	fprintf(stdout, "topic '%s': message %d bytes\n", msg->topic, msg->payloadlen);

	//fprintf(stderr, "message : '%s'\n", (char *)msg->payload);

	unsigned char in_buf[BUF_LENGTH];
	memcpy_s(in_buf, BUF_LENGTH, msg->payload, msg->payloadlen);
	std::vector<uint8_t> buf(in_buf, in_buf + msg->payloadlen);

	auto map = flexbuffers::GetRoot(buf).AsMap();
	fprintf(stdout, "Map size: %zu\n", map.size());

	auto keys = map.Keys();
	auto values = map.Values();

	for (int i = 0; i < keys.size(); i++) {
		auto key = keys[i];
		auto val = values[i];

		fprintf(stdout, "Key[%d]: %s : %s\n", i, key.AsString().c_str(),
			val.ToString().c_str());
	}



}

void my_connect_callback(struct mosquitto *mosq, void *obj, int result, int flags, const mosquitto_property *properties)
{
	int i;

	UNUSED(obj);
	UNUSED(flags);
	UNUSED(properties);

	connack_received = true;

	connack_result = result;
	if (!result) {
		mosquitto_subscribe_multiple(mosq, NULL, cfg.topic_count, cfg.topics, cfg.qos, cfg.sub_opts, cfg.subscribe_props);

		for (i = 0; i < cfg.unsub_topic_count; i++) {
			mosquitto_unsubscribe_v5(mosq, NULL, cfg.unsub_topics[i], cfg.unsubscribe_props);
		}
	}
	else {
		if (result) {
			if (cfg.protocol_version == MQTT_PROTOCOL_V5) {
				if (result == MQTT_RC_UNSUPPORTED_PROTOCOL_VERSION) {
					err_printf(&cfg, "Connection error: %s. Try connecting to an MQTT v5 broker, or use MQTT v3.x mode.\n", mosquitto_reason_string(result));
				}
				else {
					err_printf(&cfg, "Connection error: %s\n", mosquitto_reason_string(result));
				}
			}
			else {
				err_printf(&cfg, "Connection error: %s\n", mosquitto_connack_string(result));
			}
		}
		mosquitto_disconnect_v5(mosq, 0, cfg.disconnect_props);
	}
}

void my_subscribe_callback(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos)
{
	int i;
	bool some_sub_allowed = (granted_qos[0] < 128);
	bool should_print = cfg.debug && !cfg.quiet;
	UNUSED(obj);

	if (should_print) printf("Subscribed (mid: %d): %d", mid, granted_qos[0]);
	for (i = 1; i < qos_count; i++) {
		if (should_print) printf(", %d", granted_qos[i]);
		some_sub_allowed |= (granted_qos[i] < 128);
	}
	if (should_print) printf("\n");

	if (some_sub_allowed == false) {
		mosquitto_disconnect_v5(mosq, 0, cfg.disconnect_props);
		err_printf(&cfg, "All subscription requests were denied.\n");
	}

}

void my_log_callback(struct mosquitto *mosq, void *obj, int level, const char *str)
{
	UNUSED(mosq);
	UNUSED(obj);
	UNUSED(level);

	printf("%s\n", str);
}


void client_config_cleanup(struct mosq_config *cfg)
{
	int i;

	free(cfg->id);
	free(cfg->host);

	if (cfg->topics) {
		for (i = 0; i < cfg->topic_count; i++) {
			free(cfg->topics[i]);
		}
		free(cfg->topics);
	}
	
	if (cfg->unsub_topics) {
		for (i = 0; i < cfg->unsub_topic_count; i++) {
			free(cfg->unsub_topics[i]);
		}
		free(cfg->unsub_topics);
	}

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

#ifndef _WINDOWS
	struct sigaction sigact;
#endif

	//mosq_config
	cfg.id = NULL;
	cfg.qos = 0; ///qos - integer value 0, 1 or 2 indicating the Quality of Service to be used for the message.
	cfg.retain = true; ///	retain - set to true to make the message retained.
	cfg.clean_session = true;
	
	cfg.debug = true;
	cfg.quiet = false;

	cfg.host = strdup(DEFAULT_MQTT_HOST);
	cfg.port = DEFAULT_MQTT_PORT;
	cfg.keepalive = DEFAULT_MQTT_KEEPALIVE;
	cfg.protocol_version = MQTT_PROTOCOL_V5;

	char topic_1[] = "MY_TOPIC";
	char topic_2[] = "ANGLER_TOPIC";
	char topic_3[] = "EXAMPLE_TOPIC";
	cfg_add_topic(&cfg, topic_1);
	cfg_add_topic(&cfg, topic_2);
	cfg_add_topic(&cfg, topic_3);

	char un_topic_1[] = "MY_TOPIC";
	cfg_unsub_topic(&cfg, un_topic_1);

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
	if (cfg.debug) {
		mosquitto_log_callback_set(mosq, my_log_callback);
	}
	mosquitto_subscribe_callback_set(mosq, my_subscribe_callback);
	mosquitto_connect_v5_callback_set(mosq, my_connect_callback);
	mosquitto_message_v5_callback_set(mosq, my_message_callback);


	//connect
	rc = mosquitto_connect_bind_v5(mosq, cfg.host, cfg.port, cfg.keepalive, NULL, cfg.connect_props);
	if (rc != MOSQ_ERR_SUCCESS) {
		fprintf(stderr, "Error: %s\n", mosquitto_strerror(rc));

		client_config_cleanup(&cfg);
		mosquitto_destroy(mosq);
		mosquitto_lib_cleanup();

		return 1;
	}

#ifndef _WINDOWS
	sigact.sa_handler = my_signal_handler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;

	if (sigaction(SIGALRM, &sigact, NULL) == -1) {
		perror("sigaction");
		goto cleanup;
	}

	if (sigaction(SIGTERM, &sigact, NULL) == -1) {
		perror("sigaction");
		goto cleanup;
	}

	if (sigaction(SIGINT, &sigact, NULL) == -1) {
		perror("sigaction");
		goto cleanup;
	}
#endif

	rc = mosquitto_loop_forever(mosq, -1, 1);

	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();

	
	client_config_cleanup(&cfg);
	if (timed_out) {
		err_printf(&cfg, "Timed out\n");
		return MOSQ_ERR_TIMEOUT;
	}
	else if (rc) {
		err_printf(&cfg, "Error: %s\n", mosquitto_strerror(rc));
	}
	if (connack_result) {
		return connack_result;
	}
	else {
		return rc;
	}


	return 0;
}