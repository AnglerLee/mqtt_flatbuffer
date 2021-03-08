#include <iostream>
#include "mosqpp_client.h"

#define PUBLISH_TOPIC "EXAMPLE_TOPIC"


mosqpp_client::mosqpp_client(const char *id, const char *host, int port) : mosquittopp(id)
{
	keepalive = DEFAULT_MQTT_KEEPALIVE;
	connect(host, port, keepalive);
}

mosqpp_client::~mosqpp_client()
{
}

void mosqpp_client::on_connect(int result)
{
	if (result != MQTT_RC_SUCCESS)
	{
		if (result == MQTT_RC_UNSUPPORTED_PROTOCOL_VERSION) 
		{
			std::cout << "Connection error: " << mosquitto_reason_string(result) 
				<< ". Try connecting to an MQTT v5 broker, or use MQTT v3.x mode." << std::endl;
		}
		else
		{
			std::cout << "Connection error: " << mosquitto_reason_string(result) << std::endl;
		}
	}
}

void mosqpp_client::on_message(const mosquitto_message * message)
{
	int payload_size = MAX_PAYLOAD + 1;
	char buf[MAX_PAYLOAD + 1];

	memset(buf, 0, payload_size * sizeof(char));
	memcpy(buf, message->payload, MAX_PAYLOAD * sizeof(char));
	   	

	if (!strcmp(message->topic, PUBLISH_TOPIC))
	{
		// Examples of messages for M2M communications...
		if (!strcmp(buf, "stat"))
		{
			snprintf(buf, payload_size, "This is a Status Message...");
			publish(NULL, PUBLISH_TOPIC, strlen(buf), buf);

			std::cout << "Status Request Recieved." << std::endl;

		}
	}
	else 
	{
		std::cout << "on_message : " << message->topic << " : " << buf << std::endl;
	}
}

void mosqpp_client::on_publish(int mid)
{
	std::cout << "on_publish : " << mid << std::endl;
}

void mosqpp_client::on_subscribe(int mid, int qos_count, const int * granted_qos)
{
	//std::cout << "on_subscribe : " << mid << " : " << qos_count << std::endl;
}
