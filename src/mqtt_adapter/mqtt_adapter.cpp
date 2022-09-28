#include <iostream>
#include <unistd.h>
#include <string>
#include <thread>
#include <time.h>
#include <random>
#include <iomanip>
#include <vector>
#include <mutex>
#include <cstdlib>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <chrono>
#include <semaphore.h>
#include <mosquittopp.h>
#include <cstring>
#include <cstdio>

#define CLIENT_ID "Client_ID"
//#define BROKER_ADDRESS "localhost"
#define BROKER_ADDRESS "10.5.0.50"
#define MQTT_PORT 1883
#define MQTT_TOPIC "sensorData"
#define PUBLISH_TOPIC "sensorData"
#define MAX_PAYLOAD 75
#define DEFAULT_KEEP_ALIVE 60

class mqtt_client : public mosqpp::mosquittopp {
private:
	const char* host;
	const char* id;
	const char* topic;
	int port;
	int keepalive;

	void on_disconnect(int rc);
    void on_connect(int rc);
    void on_message(const struct mosquitto_message *message);
    void on_subscribe(int mid, int qos_count, const int *granted_qos);
public:
    mqtt_client(const char *id, const char * _topic, const char *host, int port);
    ~mqtt_client();
};

using std::cout;
using std::cerr;
using boost::asio::ip::udp;
using std::string;
using std::cout;
using std::cerr;
using std::runtime_error;
using std::exception;
using std::thread;
using std::setprecision;
using std::vector;
using std::mutex;
using std::atoi;
using namespace std::chrono;
using std::stoi;
using std::stol;
using std::to_string;
using std::system_error;
using std::unitbuf;

struct GatewayUDPRequest {
	long int id = {};
	string host = {};
	int port = {};
	long int timestamp = {};
};

vector<string>* receivedSensorCSVs = new vector<string>{};
mutex* mtxReceivedSensorCSVs = new mutex();

void simulateUDPServer();
GatewayUDPRequest parseRequest(char* data_);
string generateUDPReply(GatewayUDPRequest curReq);

class server {
public:
	server(boost::asio::io_service& io_service, short port)
    : // io_service_(io_service),
    socket_(io_service, udp::endpoint(udp::v4(), port)) {
		socket_.async_receive_from(
			boost::asio::buffer(data_, max_length), sender_endpoint_,
			boost::bind(&server::handle_receive_from, this,
			  boost::asio::placeholders::error,
			  boost::asio::placeholders::bytes_transferred));
	}

	void handle_receive_from(const boost::system::error_code& error, size_t bytes_recvd) {
		if (!error && bytes_recvd > 0) {
			//cout << "Request is: " << data_ << "\n" << unitbuf;
			GatewayUDPRequest curReq = parseRequest(data_);

			string reply = generateUDPReply(curReq);

			char replyChar[max_length];
			strcpy(replyChar, reply.c_str());
			size_t reply_length = strlen(replyChar);
			socket_.async_send_to(
				boost::asio::buffer(replyChar, reply_length), sender_endpoint_, // muss replyChar wirklich ein char array sein?
				boost::bind(&server::handle_send_to, this,
				boost::asio::placeholders::error,
				boost::asio::placeholders::bytes_transferred));
		} else {
			socket_.async_receive_from(
			    boost::asio::buffer(data_, max_length), sender_endpoint_,
			    boost::bind(&server::handle_receive_from, this,
				boost::asio::placeholders::error,
				boost::asio::placeholders::bytes_transferred));
		}
	}

	void handle_send_to(const boost::system::error_code& /*error*/, size_t /*bytes_sent*/) {
		socket_.async_receive_from(
			boost::asio::buffer(data_, max_length), sender_endpoint_,
			boost::bind(&server::handle_receive_from, this,
			boost::asio::placeholders::error,
			boost::asio::placeholders::bytes_transferred));
	}

private:
	// boost::asio::io_service& io_service_;
	udp::socket socket_;
	udp::endpoint sender_endpoint_;
	enum { max_length = 4096 };
	char data_[max_length];
};

mqtt_client::mqtt_client(const char * _id,const char * _topic, const char * _host, int _port) : mosquittopp(_id)
{
	mosqpp::lib_init();
	this->keepalive = DEFAULT_KEEP_ALIVE;    // Basic configuration setup for myMosq class
	this->id = _id;
	this->port = _port;
	this->host = _host;
	this->topic = _topic;
	connect_async(host, port, keepalive);
}

mqtt_client::~mqtt_client() {
	mosqpp::lib_cleanup();    // Mosquitto library cleanup
}

void mqtt_client::on_disconnect(int rc) {
	std::cout << ">> myMosq - disconnection(" << rc << ")" << std::endl << unitbuf;
}

void mqtt_client::on_connect(int rc) {
	if ( rc == 0 ) {
		std::cout << ">> myMosq - connected with server " << std::endl << unitbuf;
		subscribe(NULL, MQTT_TOPIC);
		cout << "subscribed to " << MQTT_TOPIC << "\n" << unitbuf;
	} else {
		std::cout << ">> myMosq - Impossible to connect with server(" << rc << ")" << std::endl << unitbuf;
	}
}

void mqtt_client::on_subscribe(int mid, int qos_count, const int *granted_qos)
{
	std::cout << "Subscription succeeded." << std::endl << unitbuf;
}

void mqtt_client::on_message(const struct mosquitto_message *message)
{
    int payload_size = MAX_PAYLOAD + 1;
    char buf[payload_size];

    if(!strcmp(message->topic, PUBLISH_TOPIC))
    {
        memset(buf, 0, payload_size * sizeof(char));

        /* Copy N-1 bytes to ensure always 0 terminated. */
        memcpy(buf, message->payload, MAX_PAYLOAD * sizeof(char));

        //std::cout << buf << std::endl << unitbuf;

        mtxReceivedSensorCSVs->lock();
        	// max 50 values should be hold inside receivedSensorCSVs
			if (receivedSensorCSVs->size() >= 50) {
				receivedSensorCSVs->erase(receivedSensorCSVs->begin());
			}
			receivedSensorCSVs->push_back(buf);
        mtxReceivedSensorCSVs->unlock();
    }
}

int main(int argc, char *argv[]) try
{
	thread udpServer(simulateUDPServer);
	udpServer.detach();

    class mqtt_client *iot_client;
    int errorCode = {};

    iot_client = new mqtt_client(CLIENT_ID, MQTT_TOPIC, BROKER_ADDRESS, MQTT_PORT);

    /* From the documentation for loop_forever():
     * This function call loop() for you in an infinite blocking loop.  It is useful for the case where you only want to run the MQTT client loop in your program.
	 * It handles reconnecting in case server connection is lost.  If you call mosquitto_disconnect() in a callback it will return.
     */
    errorCode = iot_client->loop_forever();
    if (errorCode != MOSQ_ERR_SUCCESS) {
    	if (errorCode == MOSQ_ERR_INVAL) {
    		cerr << "[ERROR] the input parameters were invalid\n" << unitbuf;
    	} else if (errorCode == MOSQ_ERR_NOMEM) {
    		cerr << "[ERROR] an out of memory condition occurred\n" << unitbuf;
    	} else if (errorCode == MOSQ_ERR_NO_CONN) {
    		cerr << "[ERROR] the client isn’t connected to a broker\n" << unitbuf;
    	} else if (errorCode == MOSQ_ERR_CONN_LOST) {
    		cerr << "[ERROR] the connection to the broker was lost\n" << unitbuf;
    	} else if (errorCode == MOSQ_ERR_PROTOCOL) {
    		cerr << "[ERROR] there is a protocol error communicating with the broker\n" << unitbuf;
    	} else if (errorCode == MOSQ_ERR_ERRNO) {
    		cerr << "[ERROR] a system call returned an error\n" << unitbuf;
    	}
    	cerr << "[ERROR] Something went wrong with mqtt. Error Code: " << errorCode << ". Exiting...\n" << unitbuf;
    	exit(0);
    }

    return 0;
}
catch ( system_error& s ) {
    cerr << "\nerror -4: " << "\n";
    return -4;
}
catch ( runtime_error& r ) {
    cerr << "\nerror -3: " << r.what() << "\n";
    return -3;
}
catch ( exception& e ) {
    cerr << "\nerror -2: " << e.what() << "\n";
    return -2;
}
catch ( ... ) {
    cerr << "\nan error -1"  << "\n";
    return -1;
}

void simulateUDPServer() {
	boost::asio::io_service io_service;

	server s(io_service, 11000);
	cout << "udp server creation successful\n" << unitbuf;

	io_service.run();
	cerr << "[ERROR] udp server broke down\n" << unitbuf;
}

GatewayUDPRequest parseRequest(char* data_) {
	GatewayUDPRequest parsedRequest = {};
	string request = std::string(data_);

	size_t last = 0;
	size_t next = 0;
	string delimiter = ";";
	int index = {};
	while ((next = request.find(delimiter, last)) != string::npos) {
		switch(index) {
			case 0: parsedRequest.id = stol(request.substr(last, next-last));
					break;
			case 1: parsedRequest.host = request.substr(last, next-last);
					break;
			case 2: parsedRequest.port = stoi(request.substr(last, next-last));
					break;
			case 3: parsedRequest.timestamp = stol(request.substr(last, next-last));
					break;
		}
		++index;
		last = next + 1;
	}
	return parsedRequest;
}

string generateUDPReply(GatewayUDPRequest curReq) {
	milliseconds timestamp = duration_cast< milliseconds >(system_clock::now().time_since_epoch());
	string reply = {};
	int numberOfAvailableSensorCSVs = {};

	mtxReceivedSensorCSVs->lock();
		numberOfAvailableSensorCSVs = receivedSensorCSVs->size();
		if (numberOfAvailableSensorCSVs > 0) {
			reply = receivedSensorCSVs->at(0);
			receivedSensorCSVs->erase(receivedSensorCSVs->begin());
		}
	mtxReceivedSensorCSVs->unlock();
	if (numberOfAvailableSensorCSVs > 0) {
		reply = reply.substr(reply.find(";"), reply.length());
		reply = to_string(curReq.id) + reply;
	} else {
		reply = to_string(curReq.id) + ";" +
				to_string(timestamp.count()) +";" +
				"0.0" + ";"
				+ "0.0" + ";" +
				"0.0" +
				";-1;" +
				curReq.host + ";" + // has to be the ip of the sensor !!!
				to_string(curReq.port) + ";" + // has to be the port of the sensor !!!
				"NULL";
	}
	return reply;
}
