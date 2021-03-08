#pragma once
#include <mosquittopp.h>
#include <mqtt_protocol.h>

#define MAX_PAYLOAD 50
#define DEFAULT_MQTT_KEEPALIVE 60

class mosqpp_client : public mosqpp::mosquittopp
{
public :
	mosqpp_client(const char *id, const char *host, int port);
	~mosqpp_client();

	void on_connect(int result);
	void on_message(const struct mosquitto_message *message);
	void on_publish(int mid);
	void on_subscribe(int mid, int qos_count, const int *granted_qos);

private:
	int keepalive;
};

