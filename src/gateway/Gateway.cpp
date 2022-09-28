/*
 * Gateway.cpp
 *
 *  Created on: May 2, 2022
 *      Author: fbellmann
 */
#include <iostream>
#include <unistd.h>
#include <string>
#include <thread>
#include <time.h>
#include <random>
#include <iomanip>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <boost/asio.hpp>
#include <vector>
#include <chrono>
#include <map>
#include <semaphore.h>
#include <mutex>

using boost::asio::ip::udp;
using boost::asio::ip::tcp;
using std::string;
using std::cout;
using std::cerr;
using std::runtime_error;
using std::exception;
using std::thread;
using std::setprecision;
using std::vector;
using std::begin;
using std::end;
using namespace std::chrono;
using std::map;
using std::to_string;
using std::mutex;
using std::system_error;
using std::unitbuf;


struct SensorData {
	double temperature = {};
	double brightness = {};
	double humidity = {};
};

struct SensorUDPReply {
	long int id = {};
	int port = {};
	string ip = {};
	long int timestamp = {};
	SensorData sensorData = {};
	int errorCode = {};
};

struct HTTPReply {
	int statusCode = {};
	int contentLength = {};
	string content = {};
};

// global variables
//vector<string>* sensorSocketIPs = new vector<string>{"10.5.0.5","10.5.0.7"}; // only normal sensors
vector<string>* sensorSocketIPs = new vector<string>{"10.5.0.51","10.5.0.53"};//,"10.5.0.54","10.5.0.55"}; // only mqtt sensors
//vector<string>* sensorSocketIPs = new vector<string>{"10.5.0.5","10.5.0.7","10.5.0.51","10.5.0.53","10.5.0.54","10.5.0.55"}; // all sensors toghether
string webserverIP = "10.5.0.100";
//string webserverIP = "localhost";
// the two maps will have the key like IP:Port to differentiate between the sensors
// the sensors will reply their ip address and port within the udp packet
map<string, sem_t*> semSensors = {};
map<string, long int[2]> sentUDPRequests = {}; // first value of the long int array holds the requestID, second the request timestamp
mutex* mtxSentUDPRequests = new mutex();
const int REQUEST_TIME = 4500000;
const int MAX_PACKET_SIZE = 4096;
const int MAX_NUMBER_OF_RETRIES = 1;
const int TIMEOUT_IN_SECONDS = 1;
const int MAX_NUMBER_OF_RETRIES_TO_CONNECT_TO_WEBSERVER = 3;
const int TIMEOUT_UNTIL_NEXT_CONNECTION_ATTEMPT_TO_WEBSERVER = 3000000;
const int TIMEOUT_UNTIL_NEXT_POST_REQUEST_TO_WEBSERVER = 0;
const int MAX_NUMBER_OF_RETRIES_TO_READ_TCP_REPLY = 10;
boost::asio::io_service io_service;
udp::socket s(io_service, udp::endpoint(udp::v4(), 0));
udp::resolver resolver(io_service);
// store replies and send them to the webserver
vector<SensorUDPReply>* sensorUDPReplies = new vector<SensorUDPReply>{};
mutex* mtxSensorUDPReplies = new mutex();
sem_t* semNumberSensorUDPReplies = new sem_t();
// nicht-funktionaler Test:
mutex* mtxNumberOfLostPackets = new mutex();
long int numberOfLostPackets = {};
long int numberOfTotalPackets = {};

// methods sensor interaction
void simulateUDPSocket(string ip, string port);
string generateRequest(long int requestID, string ip, string port, milliseconds timestamp);
SensorUDPReply parseReply(char* data_);
string convertToTwoDecimalPlaces(string number);
void handleReply();

// methods webserver interaction
void sendRequestsToWebserver();
int getContentLength(string replyBuffer);
int calculateLengthOfStringAfterPOSTHeader(string replyBuffer);
HTTPReply parseHttpReply(string replyBuffer, int contentLength);
void handleReplyFromWebserver(tcp::socket tcpS, size_t request_length);
// RTT messen:
long int rttSum = {};
long int numberOfTotalHTTPPostMessages = {};


int main(int argc, char* argv[]) try {
	string port = "11000";

	sem_init(semNumberSensorUDPReplies, 0, 0);

	int threadID = {};
	cout << "Spawning threads for:\n" << unitbuf;
	for(std::vector<string>::iterator socketIP = begin(*sensorSocketIPs); socketIP != end(*sensorSocketIPs); ++socketIP) {
		// set up all the semaphores
		sem_t* semSensor = new sem_t();
		sem_init(semSensor, 0, 0);
		string keyOfSensorMap = *socketIP + ":" + port;
		semSensors[keyOfSensorMap] = semSensor;
		sentUDPRequests[keyOfSensorMap][0] = 0;
		sentUDPRequests[keyOfSensorMap][1] = 0;
		++threadID;

		// simulate all udp sockets to sensor
		cout << *socketIP << ":" << port << "\n" << unitbuf;
		thread sensorSocket(simulateUDPSocket, *socketIP, port);
		sensorSocket.detach();
	}

	thread webserverInteraction(sendRequestsToWebserver);
	webserverInteraction.detach();

	thread waitForAnswer(handleReply);
	waitForAnswer.join();

	s.close();
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

void simulateUDPSocket(string ip, string port) {
	// this is needed because when the gateway communicates with the mqtt adapter,
	// then the ip of the adapter (to which it should the udp request)
	// is different than the ip of the sensor where the data came from
	string ipToSendRequestTo = {};
	if (ip == "10.5.0.51" || ip == "10.5.0.55" || ip == "10.5.0.53" || ip == "10.5.0.54") {
		ipToSendRequestTo = "10.5.0.52";
	} else {
		ipToSendRequestTo = ip;
	}

	char ip_char[ipToSendRequestTo.length() + 1];
	strcpy(ip_char, ipToSendRequestTo.c_str());
	char port_char[port.length() + 1];
	strcpy(port_char, port.c_str());


	udp::resolver::query query(udp::v4(), ip_char, port_char);
	udp::resolver::iterator iterator = resolver.resolve(query);

	long int requestID = {0};
	long int timestampOfLastRequest = {};
	string keyOfSensorMap = ip + ":" + port;
	struct timespec ts;
	int returnCode = {};

	auto startTimeOfProgram = steady_clock::now();
	while (true) {
		for (int retryCount = {}; retryCount < MAX_NUMBER_OF_RETRIES; ++retryCount) {
			milliseconds timestamp = duration_cast< milliseconds >(system_clock::now().time_since_epoch());
			char request[MAX_PACKET_SIZE];
			string req = generateRequest(requestID, ip, port, timestamp);
			strcpy(request, req.c_str());
			size_t request_length = strlen(request);
			timestampOfLastRequest = stol(to_string(timestamp.count()));

			mtxSentUDPRequests->lock();
				sentUDPRequests[keyOfSensorMap][0] = requestID;
				sentUDPRequests[keyOfSensorMap][1] = timestampOfLastRequest;
			mtxSentUDPRequests->unlock();

			// Nicht-Funktionaler Test:
			/*mtxNumberOfLostPackets->lock();
				++numberOfTotalPackets;
	    		if (numberOfTotalPackets >= 10001) {
	    			cout << "Number of Total Packets: " << numberOfTotalPackets - 1 << "\n";
	    			cout << "Number of Lost Packets: " << numberOfLostPackets << "\n";
	    			cout << "Number of Successful Packets: " << numberOfTotalPackets - 1 - numberOfLostPackets << "\n" << unitbuf;
					auto endTimeOfProgram = steady_clock::now();
					cout << "Elapsed time in milliseconds: "
						<< duration_cast<milliseconds>(endTimeOfProgram - startTimeOfProgram).count()
						<< " ms" << "\n" << unitbuf;
	    			exit(0);
	    		}
			mtxNumberOfLostPackets->unlock();*/

			s.send_to(boost::asio::buffer(request, request_length), *iterator);

			// wait for the response
		    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
		        perror("clock_gettime");
		        exit(EXIT_FAILURE);
		    }
		    ts.tv_sec += TIMEOUT_IN_SECONDS;
		    while ((returnCode = sem_timedwait((semSensors[keyOfSensorMap]), &ts)) == -1 && errno == EINTR)
		        continue;
		    if (returnCode == -1) { // if we weren't able to get the semaphore --> that means there came no answer from the sensor in time
		        // we need to retry
		    	if (errno == ETIMEDOUT) {
		    		mtxSentUDPRequests->lock();
		    			long int timeoutTime = (duration_cast< milliseconds >(system_clock::now().time_since_epoch())).count() - sentUDPRequests[keyOfSensorMap][1];
		    		mtxSentUDPRequests->unlock();

		    		mtxNumberOfLostPackets->lock();
		    			++numberOfLostPackets;
			    	mtxNumberOfLostPackets->unlock();
			    	//cerr << "[ERROR] Reply " + to_string(requestID) + " from " + keyOfSensorMap + " timed out after " + to_string(timeoutTime) + " ms. Retry Count: " + to_string(retryCount) + "\n" << unitbuf;
		        } else {
		            perror("sem_timedwait");
		        }
		    } else {
		    	// sem_timedwait() succeeded, we've got the semaphore, so it came a reply in time
		    	break; // break from loop to avoid retrying
		    }
		}
		usleep(REQUEST_TIME); // sollte vielleicht nur gemacht werden, wenn es keinen Fehler gab
		++requestID;
	}
}

string generateRequest(long int requestID, string ip, string port, milliseconds timestamp) {
	string req = std::to_string(requestID) + ";" + ip + ";" + port + ";" + std::to_string(timestamp.count()) + ";NULL";
	return req;
}

SensorUDPReply parseReply(char* data_) {
	SensorUDPReply parsedReply = {};
	string reply = std::string(data_);

	size_t last = 0;
	size_t next = 0;
	string delimiter = ";";
	int index = {};
	while ((next = reply.find(delimiter, last)) != string::npos) {
		setprecision(2);
		switch(index) {
			case 0: parsedReply.id = stol(reply.substr(last, next-last));
					break;
			case 1: parsedReply.timestamp = stol(reply.substr(last, next-last));
					break;
			case 2: parsedReply.sensorData.brightness = stod(reply.substr(last, next-last));
					break;
			case 3: parsedReply.sensorData.humidity = stod(reply.substr(last, next-last));
					break;
			case 4: parsedReply.sensorData.temperature = stod(reply.substr(last, next-last));
					break;
			case 5: parsedReply.errorCode = stoi(reply.substr(last, next-last));
					break;
			case 6: parsedReply.ip = reply.substr(last, next-last); // needed to differentiate between the sensors
					break;
			case 7: parsedReply.port = stoi(reply.substr(last, next-last)); // needed to differentiate between the sensors
					break;
		}
		++index;
		last = next + 1;
	}
	return parsedReply;
}

void handleReply() {
	while (true) {
	    char reply[MAX_PACKET_SIZE];
	    udp::endpoint sender_endpoint;
	    size_t reply_length = s.receive_from(boost::asio::buffer(reply, MAX_PACKET_SIZE), sender_endpoint);
	    /*std::cout << "Reply is: ";
	    std::cout.write(reply, reply_length);
	    std::cout << "\n";*/
	    SensorUDPReply sensorUDPReply = parseReply(reply);
	    string keyOfSensorMap = sensorUDPReply.ip + ":" + to_string(sensorUDPReply.port);

	    // check if the id the expected id
	    mtxSentUDPRequests->lock();
	    	long int expectedID = sentUDPRequests[keyOfSensorMap][0];
	    	long int timestampOfRequest = sentUDPRequests[keyOfSensorMap][1];
	    mtxSentUDPRequests->unlock();
	    if (expectedID == sensorUDPReply.id) {
    		mtxSentUDPRequests->lock();
    			long int amountOfMsItTookToGetTheReply = (duration_cast< milliseconds >(system_clock::now().time_since_epoch())).count() - timestampOfRequest;
    		mtxSentUDPRequests->unlock();
	    	// check if there is no error
	    	if (sensorUDPReply.errorCode == 0) {
	    		//cout << "[SUCCESS] Request " + to_string(sensorUDPReply.id) + " returned from sensor " + keyOfSensorMap + " after " + to_string(amountOfMsItTookToGetTheReply) + " ms and gave the data:\n BRIGHT -> " + convertToTwoDecimalPlaces(to_string(sensorUDPReply.sensorData.brightness)) + " |  HUM -> " + convertToTwoDecimalPlaces(to_string(sensorUDPReply.sensorData.humidity)) + " | TEMP -> " + convertToTwoDecimalPlaces(to_string(sensorUDPReply.sensorData.temperature)) + "\n" << unitbuf;
	    		sem_post(semSensors[keyOfSensorMap]); // signal that there was data received from the sensore
	    		mtxSensorUDPReplies->lock();
	    			sensorUDPReplies->push_back(sensorUDPReply);
	    			sem_post(semNumberSensorUDPReplies);
	    		mtxSensorUDPReplies->unlock();
	    	} else {
	    		// at the point of writing there is only one case where an error code
	    		// occurs: when the sensor has no new data generated. this case is not
	    		// that important. we can just create a new request and try again.
	    		//cout << "[WARNING] Request " + to_string(sensorUDPReply.id) + " returned from sensor " + keyOfSensorMap + " with error " + to_string(sensorUDPReply.errorCode) + " after " + to_string(amountOfMsItTookToGetTheReply) + " ms\n" << unitbuf;
	    		sem_post(semSensors[keyOfSensorMap]); // signal that there was data received from the sensore
	    	}
	    }
	}
}

string convertToTwoDecimalPlaces(string number) {
	string delimiter = ".";
	number = number.substr(0, number.find(delimiter) + 1) + number.substr(number.find(delimiter) + 1, 2);
	return number;
}

void sendRequestsToWebserver() {
	char ip_char[webserverIP.length() + 1];
	strcpy(ip_char, webserverIP.c_str());
	char port_char[] = "12080";


	auto startTimeOfProgram = steady_clock::now();
	auto startRequestTime = steady_clock::now(); // will get overridden before a request
	auto endRequestTime = steady_clock::now(); // will get overridden after a request

    tcp::resolver tcpResolver(io_service);
    tcp::resolver::query tcpQuery(tcp::v4(), ip_char, port_char);
    tcp::resolver::iterator tcpIterator = tcpResolver.resolve(tcpQuery);

    tcp::socket tcpS(io_service);
    for (int i = 0; i < MAX_NUMBER_OF_RETRIES_TO_CONNECT_TO_WEBSERVER; ++i) {
        try {
        	boost::asio::connect(tcpS, tcpIterator);
        	cout << "Connection successfully established to webserver at " << ip_char << ":" << port_char << "\n" << unitbuf;
        	break; // connection could be established successfully
        } catch (exception& e) {
        	// connection could not be established successfully --> let's retry it
        	cerr << "[ERROR] Failed to connect to the webserver. Retry Count: " << i << "\n" << unitbuf;
        	if (i == MAX_NUMBER_OF_RETRIES_TO_CONNECT_TO_WEBSERVER - 1) {
        		cerr << "[ERROR] Could not establish a connection to the webserver after " << i << " retries. Stopping thread...\n" << unitbuf;
        		return; // stop the thread
        	}
        	usleep(TIMEOUT_UNTIL_NEXT_CONNECTION_ATTEMPT_TO_WEBSERVER);
        }
    }

    while (true) { // handle session termination exception by retrying x times
    	usleep(TIMEOUT_UNTIL_NEXT_POST_REQUEST_TO_WEBSERVER);
    	string req = {};
    	string body = {};
    	HTTPReply httpReply = {};

    	// [ATTENTION]: possible bug found: two threads could take the same data and send therefore the same data to the webserver!
    	// solution: temporarily remove the first sensorUDPReply from the vector
    	// and add it back at the first position if the transmission to the webserver failed
    	//cout << "Checking if data is available to send it to the webserver...\n" << unitbuf;
    	sem_wait(semNumberSensorUDPReplies);
    	mtxSensorUDPReplies->lock();
    		//cout << "[SUCCESS] Available data found!\n" << unitbuf;
    		SensorUDPReply sensorUDPReply = sensorUDPReplies->at(0);
	    mtxSensorUDPReplies->unlock();

        body =	"temperature=" + to_string(sensorUDPReply.sensorData.temperature) + "&";
        body +=	"humidity=" + to_string(sensorUDPReply.sensorData.humidity) + "&";
        body +=	"brightness=" + to_string(sensorUDPReply.sensorData.brightness) + "&";
        body +=	"ip=" + sensorUDPReply.ip + "&";
        body +=	"port=" + to_string(sensorUDPReply.port) + "&";
        body +=	"timestampSensor=" + to_string(sensorUDPReply.timestamp);

        req = 	"POST /saveSensorData HTTP/1.1\r\n";
        req +=	"Host: localhost\r\n";
        req +=	"Content-Type: text/html; charset=UTF-8\r\n";
        req +=	"Content-Length: " + to_string(body.length()) + "\r\n\r\n";
		req += 	body;

        char request[4096] = {};
		strcpy(request, req.c_str());

        size_t request_length = strlen(request);
        char reply[4096];
        string replyBuffer = {};
        for (int connectRetryCount = 0; connectRetryCount < MAX_NUMBER_OF_RETRIES_TO_CONNECT_TO_WEBSERVER; ++connectRetryCount) {
			try {
				startRequestTime = steady_clock::now();
				boost::asio::write(tcpS, boost::asio::buffer(request, request_length));
				++numberOfTotalHTTPPostMessages;

				// call as many receives as we need to get all the data of one reply
				for (int readRetryCount = 0; readRetryCount < MAX_NUMBER_OF_RETRIES_TO_READ_TCP_REPLY; ++readRetryCount) {
					boost::asio::read(tcpS, boost::asio::buffer(reply, request_length)); // we don't need a timeout because if read fails it will throw an exception and then we will just try to reconnect to the webserver
					replyBuffer += reply; // needed to check whether or not there was enough data received
					if (replyBuffer.find("\r\n\r\n") != std::string::npos) {
						int contentLength = getContentLength(replyBuffer);
						int lengthOfStringAfterPOSTHeader = calculateLengthOfStringAfterPOSTHeader(replyBuffer);
			    		// check if a whole message was received
			    		if (lengthOfStringAfterPOSTHeader >= contentLength) {
			    			//cout << "length of string " << lengthOfStringAfterPOSTHeader << " >= " << "content length " << contentLength << "\n";

			    			httpReply = parseHttpReply(replyBuffer, contentLength);
			    			httpReply.contentLength = contentLength;
			    			// delete the reply from the replyBuffer
			    			replyBuffer = replyBuffer.substr(replyBuffer.find("\r\n\r\n") + 4 + contentLength, replyBuffer.length());

			    			endRequestTime = steady_clock::now();
			    			break; // we got a complete message so go ahead --> read was successfull
			    		} else {
			    			cout << "length of string " << lengthOfStringAfterPOSTHeader << " < " << "content length " << contentLength << "\n" << unitbuf;
			    			cout << "[WARNING] No complete reply message was received. Retry Count: " << readRetryCount << "\n" << unitbuf;
			    		}
					} else {
						cout << "[WARNING] No complete reply message was received. Retry Count: " << readRetryCount << "\n" << unitbuf;
					}
		        	if (readRetryCount == MAX_NUMBER_OF_RETRIES_TO_READ_TCP_REPLY - 1) {
		        		cerr << "[ERROR] Could not read a whole reply after " << readRetryCount << " retries. Stopping thread...\n" << unitbuf;
		        		return; // stop the thread
		        	}
				}

				break; // tcp request was successfull
			} catch (exception& e) {
	        	try {
	        		boost::asio::connect(tcpS, tcpIterator);
	        		cout << "Connection successfully reestablished to webserver at " << ip_char << ":" << port_char << "\n" << unitbuf;
	        	} catch (exception& e) {
	        		cerr << "[ERROR] Connection to webserver got lost. Retry Count: " << connectRetryCount << "\n" << unitbuf;
		        	if (connectRetryCount == MAX_NUMBER_OF_RETRIES_TO_CONNECT_TO_WEBSERVER - 1) {
		        		cerr << "[ERROR] Could not send nor receive data to/from the webserver after " << connectRetryCount << " retries. Stopping thread...\n" << unitbuf;
		        		return; // stop the thread
		        	}
	        	}
	        	usleep(TIMEOUT_UNTIL_NEXT_CONNECTION_ATTEMPT_TO_WEBSERVER);
			}
        }


        auto rtt = duration_cast<microseconds>(endRequestTime - startRequestTime).count();
        //cout << "[SUCCESS] Reply from webserver after " << rtt << " microseconds with status: " << httpReply.statusCode << " | Content: " << httpReply.content << "\n" << unitbuf;


        rttSum = rttSum + rtt;
        // nicht-funktionaler Test:
        /*if (numberOfTotalHTTPPostMessages >= 10000) {
			cout << "Number of Total HTTP Post Messages: " << numberOfTotalHTTPPostMessages << "\n" << unitbuf;
			cout << "Average RTT: " << (rttSum / numberOfTotalHTTPPostMessages) << "\n" << unitbuf;
			auto endTimeOfProgram = steady_clock::now();
		    cout << "Elapsed time in milliseconds: "
		        << duration_cast<milliseconds>(endTimeOfProgram - startTimeOfProgram).count()
		        << " ms" << "\n" << unitbuf;
			exit(0);
        }*/

        if (httpReply.statusCode == 200) { // http post was successfull
        	mtxSensorUDPReplies->lock();
        		sensorUDPReplies->erase(sensorUDPReplies->begin()); // delete the entry that we've successfully transfered to the webserver
        	mtxSensorUDPReplies->unlock();
        } else {
        	cerr << "[ERROR] There was an error processing the request at the webserver. Error-Code: " << httpReply.statusCode << "\n" << unitbuf;
        	// we could do something like counting the retries or so but i guess it's not that important
        	// we will just try to send the same data again though
        }
    }
}

int getContentLength(string replyBuffer) {
	string hlp = replyBuffer.substr(replyBuffer.find("Content-Length: ") + 15, replyBuffer.length());
	string hlp2 = hlp.substr(0, hlp.find("\r\n\r\n"));
	return stoi(hlp2);
}

int calculateLengthOfStringAfterPOSTHeader(string replyBuffer) {
	string hlp = replyBuffer.substr(replyBuffer.find("\r\n\r\n") + 4, replyBuffer.length());
	return hlp.length();
}

HTTPReply parseHttpReply(string replyBuffer, int contentLength) {
	HTTPReply httpReply = {};

	httpReply.statusCode = stoi(replyBuffer.substr(replyBuffer.find(" "), replyBuffer.find(" ", replyBuffer.find(" ") + 1)));
	httpReply.content = replyBuffer.substr(replyBuffer.find("\r\n\r\n") + 4, contentLength);
	return httpReply;
}
