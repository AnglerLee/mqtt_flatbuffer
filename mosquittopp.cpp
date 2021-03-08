#include <iostream>
#include "mosqpp_client.h"

#define CLIENT_ID "Client_ID"
#define DEFAULT_MQTT_HOST "127.0.0.1"
#define DEFAULT_MQTT_PORT 1883
#define DEFAULT_MQTT_TOPIC "EXAMPLE_TOPIC"

void usage(char *argv0)
{
	fprintf(stderr,
		"Usage: %s [-h host] [-p port]\n", argv0);
	exit(1);
}


int main(int argc, char *argv[])
{
	int rc;
	mosqpp_client *client;
	

	char *host = NULL;
	char client_id[] = CLIENT_ID;
	int port = DEFAULT_MQTT_PORT;

	// Parse options 
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-h"))
		{
			if (i == argc - 1) {
				fprintf(stderr, "Error: -h argument given but no host specified.");
				return 1;
			}
			else {
				host = _strdup(argv[i + 1]);
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
				port = atoi(argv[i + 1]);
			}
			i++;
		}
		else
		{
			usage(argv[0]);
		}

	}

	mosqpp::lib_init();

	if (host == NULL) 
	{
		client = new mosqpp_client(client_id, DEFAULT_MQTT_HOST, port);
	}
	else
	{
		client = new mosqpp_client(client_id, host, port);
	}

	while (1)
	{
		rc = client->loop();
		if (rc)
		{
			client->reconnect();
		}
		else
			client->subscribe(NULL, DEFAULT_MQTT_TOPIC);
	}

	mosqpp::lib_cleanup();
	free(host);

	return 0;
}