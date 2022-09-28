/*
 * Database.cpp
 *
 *  Created on: May 11, 2022
 *      Author: fbellmann
 */

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
#include <map>

// Apache Thrift dependencies:
#include "./../gen-cpp/Database.h"
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/concurrency/ThreadManager.h>

// get ip address dependencies:
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/if_link.h>

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
using boost::asio::ip::tcp;
using std::unitbuf;
using std::map;

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;
using apache::thrift::concurrency::ThreadFactory;
using apache::thrift::concurrency::ThreadManager;

map<string, SensorMetadata>* sensorMetadataCollection = new map<string, SensorMetadata>{};
const int numberOfThreads = 10;
string ip = {};
int numberOfCreateRequest = {-1};

string getIPAdress();

class DatabaseHandler : virtual public DatabaseIf {
 public:
	DatabaseHandler() {
		// Your initialization goes here
	}

	void ping() {
		// Your implementation goes here
		printf("ping\n");
	}

	int32_t updateSensorMetadata(const std::string& id, const SensorMetadata& sensorMetadata) {
		map<string, SensorMetadata>::iterator datasetToBeUpdated = sensorMetadataCollection->find(id);
		if (datasetToBeUpdated == sensorMetadataCollection->end()) {
			//cout << "could not update | a data set with id: " << id << " was not found in the database" << "\n" << unitbuf;
			return -2;
		} else {
			datasetToBeUpdated->second = sensorMetadata;
			//cout << "success | update of data set with id: " << id << "\n" << unitbuf;
			return 0;
		}
	}

	int32_t deleteSensorMetadata(const std::string& id) {
		map<string, SensorMetadata>::iterator datasetToBeDeleted = sensorMetadataCollection->find(id);
		if (datasetToBeDeleted == sensorMetadataCollection->end()) {
			//cout << "could not delete | a data set with id: " << id << " was not found in the database" << "\n" << unitbuf;
			return -2;
		} else {
			sensorMetadataCollection->erase(id);
			//cout << "success | deleting data set with id: " << id << "\n" << unitbuf;
			return 0;
		}
	}

	void readSensorMetadata(SensorMetadata& _return, const std::string& id) {
		map<string, SensorMetadata>::iterator datasetToBeRead = sensorMetadataCollection->find(id);
		if (datasetToBeRead == sensorMetadataCollection->end()) {
			//cout << "could not read | a data set with id: " << id << " was not found in the database" << "\n" << unitbuf;
			_return.port = -2;
		} else {
			_return = datasetToBeRead->second;
			//cout << "success | reading data set with id: " << id << "\n" << unitbuf;
		}
	}

	int32_t createSensorMetadata(const std::map<std::string, SensorMetadata>& sensorMetadataCollectionReceived) {
		if (sensorMetadataCollectionReceived.empty()) {
			cout << "could not create | the received collection of sensorMetadata is empty" << "\n" << unitbuf;
			return -1;
		} else {
			vector<string> listOfDuplicates;
			int foundDuplicate = 0;		// value 0 = no duplicates found | value -3 = found at least one duplicate
			for (auto itr = sensorMetadataCollectionReceived.begin() ; itr != sensorMetadataCollectionReceived.end() ; ++itr) {
				if(sensorMetadataCollection->find(itr->first) != sensorMetadataCollection->end()){
					listOfDuplicates.push_back(itr->first);
					foundDuplicate = -3;
				} else {
					sensorMetadataCollection->insert(std::pair<string, SensorMetadata>(itr->first, itr->second));
					////cout << "success | creating data set with id '" << itr->first << "'" << "\n" << unitbuf;
				}
			}
			if (!listOfDuplicates.empty()) {
				anounceDuplicates(&listOfDuplicates);
			}
			anounceAllDatasets();
			return foundDuplicate;
		}
	}

	void readAll(std::map<std::string, SensorMetadata>& _return, const int32_t numberOfRows) {
		int countRows = 0;
		for (auto itr = --sensorMetadataCollection->end() ; itr != --sensorMetadataCollection->begin() ; --itr) {
			_return.insert(std::pair<string, SensorMetadata>(itr->first, itr->second));
			countRows++;
			if (countRows == numberOfRows) {
				break;
			}
		}
	}

	int32_t isUpdatePossible(const std::string& id) {
		map<string, SensorMetadata>::iterator datasetToBeUpdated = sensorMetadataCollection->find(id);
		if (datasetToBeUpdated == sensorMetadataCollection->end()) {
			return -1;
		} else {
			return 0;
		}
	}

	int32_t isDeletePossible(const std::string& id) {
		map<string, SensorMetadata>::iterator datasetToBeDeleted = sensorMetadataCollection->find(id);
		if (datasetToBeDeleted == sensorMetadataCollection->end()) {
			return -1;
		} else {
			return 0;
		}
	}

	int32_t isCreatePossible(const std::map<std::string, SensorMetadata> & sensorMetadataCollectionReceived) {
		++numberOfCreateRequest;
		if (((numberOfCreateRequest % 2) == 1) && ip == "10.5.0.200") { // on every thirad create there should be a delayed answer just for simulation and only at database 10.5.0.200
			//cout << "--------->>>>>>> ICH BRAUCHE SEHR LANGE\n" << unitbuf;
			usleep(10000000);
		}
		if ((numberOfCreateRequest >= 4) && ip == "10.5.0.200") {
			//cout << "--------->>>>>>> ICH BEENDE MICH\n" << unitbuf;
			exit(-1);
		}
		if (sensorMetadataCollectionReceived.empty()) {
			return -1;
		} else {
			for (auto itr = sensorMetadataCollectionReceived.begin() ; itr != sensorMetadataCollectionReceived.end() ; ++itr) {
				if(sensorMetadataCollection->find(itr->first) != sensorMetadataCollection->end()){
					return -1;
				}
			}
		}
		return 0;
	}


 private:
	void anounceDuplicates(vector<string>* listOfDuplicates) {
		cout << "Duplicates found while creating new data sets. The IDs of the duplicates are: ";
		for (auto lod : *listOfDuplicates) {
			cout << lod << " ";
		}
		cout << "\n" << unitbuf;
	}

	void anounceAllDatasets() {
		cout << "------------announcing all data sets------------" << "\n" << unitbuf;
		for (auto itr = sensorMetadataCollection->begin() ; itr != sensorMetadataCollection->end() ; ++itr) {
			cout << "id: " << itr->first  << " | temperature: " << itr->second.sensorData.temperature
											<< " brightness: " << itr->second.sensorData.brightness
											<< " humidity: " << itr->second.sensorData.humidity
											<< "\n";
		}
		cout << "-------------done with announcement--------------" << "\n" << unitbuf;
	}

};

int main(int argc, char **argv) try {
	ip = getIPAdress();
	int port = 9090;
	::std::shared_ptr<DatabaseHandler> handler(new DatabaseHandler());
	::std::shared_ptr<TProcessor> processor(new DatabaseProcessor(handler));
	::std::shared_ptr<TServerTransport> serverTransport(new TServerSocket(port));
	::std::shared_ptr<TTransportFactory> transportFactory(new TFramedTransportFactory());
	::std::shared_ptr<TProtocolFactory> protocolFactory(new TBinaryProtocolFactory());
	::std::shared_ptr<ThreadManager> threadManager = ThreadManager::newSimpleThreadManager(numberOfThreads);
	::std::shared_ptr<ThreadFactory> threadFactory(new ThreadFactory());
	threadManager->threadFactory(threadFactory);
	threadManager->start();

	TServer* server = NULL;
	server = new TThreadedServer(processor, serverTransport, transportFactory, protocolFactory);
	server->serve();
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
