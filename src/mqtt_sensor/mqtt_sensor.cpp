#define _GNU_SOURCE     /* To get defns of NI_MAXSERV and NI_MAXHOST */
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

// get ip address dependencies:
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/if_link.h>

//#define PUBLISHER_ID "Publisher_ID"
//#define BROKER_ADDRESS "localhost"
#define BROKER_ADDRESS "10.5.0.50"
#define MQTT_PORT 1883
#define PUBLISH_TOPIC "sensorData"
#define MAX_PAYLOAD 75
#define DEFAULT_KEEP_ALIVE 60

class mqtt_publisher : public mosqpp::mosquittopp
{
private:
	const char* host;
	const char* id;
	const char* topic;
	int port;
	int keepalive;

	void on_connect(int rc);
	void on_disconnect(int rc);
	void on_publish(int mid);
public:
	mqtt_publisher(const char *id, const char * _topic, const char *host, int port);
	~mqtt_publisher();
	bool send_message(const char * _message);
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

struct SensorDescription {
	string ip = {};
	int port = {};
};

struct SensorData {
	double temperature = {};
	double brightness = {};
	double humidity = {};
};

struct GatewayUDPRequest {
	long int id = {};
	string host = {};
	int port = {};
	long int timestamp = {};
};

SensorDescription* sensorDescription = new SensorDescription{};
/* holds all of the sensor data which was generated in the past.
 * from here the gateway gets it's values. if a udp request comes in,
 * then the first entry of the vector is returned. if there is none, then
 * the value was already returned to the gateway or there is no data generated.
 */
vector<SensorData*>* historicSensorData = new vector<SensorData*>{};
mutex* mtxHistoricSensorData = new mutex();
sem_t* newDataToSendAvailable =  new sem_t();

constexpr double MIN_TEMP_VALUE = 0.0;
constexpr double MAX_TEMP_VALUE = 25.0;
constexpr double MIN_BRIGHT_VALUE = 0.0;
constexpr double MAX_BRIGHT_VALUE = 100.0;
constexpr double MIN_HUM_VALUE = 30.0;
constexpr double MAX_HUM_VALUE = 80.0;
const int GENERATION_TIME = 5000000;

void generateSensorData();
string generateMqttMessage();
string convertToTwoDecimalPlaces(string number);
string getIPAdress();

mqtt_publisher::mqtt_publisher(const char * _id,const char * _topic, const char * _host, int _port) : mosquittopp(_id)
{
	mosqpp::lib_init();        // Mandatory initialization for mosquitto library
	this->keepalive = DEFAULT_KEEP_ALIVE;    // Basic configuration setup for myMosq class
	this->id = _id;
	this->port = _port;
	this->host = _host;
	this->topic = _topic;
	connect_async(host,     // non blocking connection to broker request
	port,
	keepalive);
	loop_start();            // Start thread managing connection / publish / subscribe
};

mqtt_publisher::~mqtt_publisher() {
	loop_stop();            // Kill the thread
	mosqpp::lib_cleanup();    // Mosquitto library cleanup
}

bool mqtt_publisher::send_message(const  char * _message) {
	// Send message - depending on QoS, mosquitto lib managed re-submission this the thread
	//
	// * NULL : Message Id (int *) this allow to latter get status of each message
	// * topic : topic to be used
	// * lenght of the message
	// * message
	// * qos (0,1,2)
	// * retain (boolean) - indicates if message is retained on broker or not
	// Should return MOSQ_ERR_SUCCESS
	int ret = publish(NULL, this->topic, strlen(_message), _message, 1, false);
	return ( ret == MOSQ_ERR_SUCCESS );
}

void mqtt_publisher::on_disconnect(int rc) {
	std::cout << ">> myMosq - disconnection(" << rc << ")" << std::endl << unitbuf;
}

void mqtt_publisher::on_connect(int rc) {
	if ( rc == 0 ) {
		std::cout << ">> myMosq - connected with server" << std::endl << unitbuf;
	} else {
		std::cout << ">> myMosq - Impossible to connect with server(" << rc << ")" << std::endl << unitbuf;
	}
}

void mqtt_publisher::on_publish(int mid) {
	//std::cout << ">> myMosq - Message (" << mid << ") succeed to be published " << std::endl << unitbuf;
}

int main(int argc, char *argv[]) try
{
	sem_init(newDataToSendAvailable, 0, 0);
	string ip = getIPAdress();
	//string ip = "localhost";
	cout << "I've got ip address " + ip + "\n" << unitbuf;

	// generating sensor data
	thread sensorData(generateSensorData);
	sensorData.detach();

	// save configuration to SensorDescription
	sensorDescription->ip = ip;
	sensorDescription->port = 11000;

	// create mqtt publisher
    class mqtt_publisher *iot_publisher;
    bool sendMessageWasSuccessful = {};
    string PUBLISHER_ID_STRING = sensorDescription->ip + ":" + to_string(sensorDescription->port); // setting the publisher_id to the different
    char PUBLISHER_ID[PUBLISHER_ID_STRING.length()]; // setting the publisher_id to the different
    strcpy(PUBLISHER_ID, PUBLISHER_ID_STRING.c_str());
    iot_publisher = new mqtt_publisher(PUBLISHER_ID, PUBLISH_TOPIC, BROKER_ADDRESS, MQTT_PORT);

    while (true) {
    	// publish messages periodically to the broker
    	sem_wait(newDataToSendAvailable);
    	mtxHistoricSensorData->lock();
    		string messageString = generateMqttMessage();
    		//cout << messageString << "\n" << unitbuf;
    	mtxHistoricSensorData->unlock();
        char messageChar[messageString.length()];
        strcpy(messageChar, messageString.c_str());
        sendMessageWasSuccessful = iot_publisher->send_message(messageChar);
        if (!sendMessageWasSuccessful) {
        	cerr << "[ERROR] Something went wrong with mqtt. Stopping publisher... \n" << unitbuf;
        	exit(0);
        }
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

string getIPAdress() {
    struct ifaddrs *ifaddr;
    int family, s;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    /* Walk through linked list, maintaining head pointer so we
       can free list later. */

    for (struct ifaddrs *ifa = ifaddr; ifa != NULL;
             ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        family = ifa->ifa_addr->sa_family;

        /* Display interface name and family (including symbolic
           form of the latter for the common families). */

        /* For an AF_INET* interface address, display the address. */

        if (family == AF_INET || family == AF_INET6) {
            s = getnameinfo(ifa->ifa_addr,
                    (family == AF_INET) ? sizeof(struct sockaddr_in) :
                                          sizeof(struct sockaddr_in6),
                    host, NI_MAXHOST,
                    NULL, 0, NI_NUMERICHOST);
            if (s != 0) {
                printf("getnameinfo() failed: %s\n", gai_strerror(s));
                exit(EXIT_FAILURE);
            }

            string stringHost(host);
            if (stringHost.find("10.5.0.") != std::string::npos) { // check if the current ip address contains a substring of the ip address we are looking for
            	// the sensor has one of the ip addresses that are included in the possibleIPAdresses string
            	if (stringHost != "10.5.0.1") {
            		freeifaddrs(ifaddr);
            		return stringHost;
            	}
            }
        }
    }
    freeifaddrs(ifaddr);
    cerr << "[ERROR] Could not find the right ip address\n"  << unitbuf;
    exit(EXIT_FAILURE);
}

void generateSensorData() {
	// initialize random values generator for each of the sensor data attributes
    std::random_device rd;
    std::default_random_engine eng(rd());
    std::uniform_real_distribution<double> tempGenerator(MIN_TEMP_VALUE, MAX_TEMP_VALUE);
    std::uniform_real_distribution<double> brightGenerator(MIN_BRIGHT_VALUE, MAX_BRIGHT_VALUE);
    std::uniform_real_distribution<double> humGenerator(MIN_HUM_VALUE, MAX_HUM_VALUE);
    while (true) {
		usleep(GENERATION_TIME); // how long it will take till the values change
		/* holds the current sensor data which will be then stored into the
		 * historicSensorData array
		 */
		SensorData* sensorData = new SensorData{};
		//sem_wait(freeSpacesLeftInHistoricSensorData);
		setprecision(2);
		sensorData->brightness = brightGenerator(eng);
		sensorData->temperature = tempGenerator(eng);
		sensorData->humidity = humGenerator(eng);
		mtxHistoricSensorData->lock();
			//cout << "current vector size: " << historicSensorData->size() << "\n" << unitbuf;
			if (historicSensorData->size() >= 50) {
				historicSensorData->erase(historicSensorData->begin());
			}
			historicSensorData->push_back(sensorData);
			sem_post(newDataToSendAvailable);
		mtxHistoricSensorData->unlock();
		/*cout 	<< "new generated values: || "
				<< "bright: " << sensorData->brightness << " "
				<< "hum: " << sensorData->humidity << " "
				<< "temp: " << sensorData->temperature << " "
				<< "\n" << unitbuf;*/
    }
}

string generateMqttMessage() {
	milliseconds timestamp = duration_cast< milliseconds >(system_clock::now().time_since_epoch());
	string reply = {};

	SensorData* sensorData = historicSensorData->at(0);
	reply = to_string(0) + ";" +
			to_string(timestamp.count()) + ";" +
			convertToTwoDecimalPlaces(to_string(sensorData->brightness)) + ";" +
			convertToTwoDecimalPlaces(to_string(sensorData->humidity)) + ";" +
			convertToTwoDecimalPlaces(to_string(sensorData->temperature)) +
			";0;" +
			sensorDescription->ip + ";" +
			to_string(sensorDescription->port) + ";" +
			"NULL";
	historicSensorData->erase(historicSensorData->begin());

	return reply;
}

string convertToTwoDecimalPlaces(string number) {
	string delimiter = ".";
	number = number.substr(0, number.find(delimiter) + 1) + number.substr(number.find(delimiter) + 1, 2);
	return number;
}
