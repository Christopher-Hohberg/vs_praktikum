/*
 * Webserver.cpp
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

// get ip address dependencies:
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/if_link.h>

#include "./../gen-cpp/Database.h"
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>

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

using namespace std;
using namespace apache::thrift;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;

struct HTTPRequest {
	string method = {};
	string url = {};
	int contentLength = {};
	string content = {};
};

// gateway interaction
string requestBuffer = {};
mutex* mtxRequestBuffer = new mutex();

HTTPRequest parseHttpRequest(string data_, int contentLength);
int calculateLengthOfStringAfterPOSTHeader(string requestBuffer);
int getContentLength(string requestBuffer);
SensorMetadata parseHTTPContent(string httpContent);
vector<SensorMetadata>* sensorMetadatas = new vector<SensorMetadata>{};
mutex* mtxSensorMetadatas = new mutex();

// database interaction
sem_t* numberOfAvailableDataToSendToDatabase = new sem_t();
long int numberOfDatabaseInteractions = {};
long int numberOfSuccessfulDatabaseInteractions = {};
long int numberOfSuccessfulDatabaseCreations = {};

// 2 phase commit interaction
map<string, int> numberOfTimeoutsBehindEachOtherByDatabase = {
//		{"10.5.0.200", 0}
};
map<string, sem_t*> semaphoreByDatabase = {
//		{"10.5.0.200", new sem_t()}
};
map<string, sem_t*>::iterator semaphoreByDatabaseIterator;
map<string, int> votingResultByDatabase = { // 1 = noch nicht abgestimmt; 0 = Zugestimmt; -1 = Abort
//		{"10.5.0.200", 1}
};
map<string, int> resultAfterCommitOperationByDatabase = { // zum Abfangen für die ganz normalen Return-Werte
//		{"10.5.0.200", 0}
};
map<string, timespec> timestampWhenTimoutIsOccuredByDatabase = { // um Merken, zu welchem Zeitpunkt man für jede Datenbank einen Timeout definiert
};
vector<string> databases{"10.5.0.200", "10.5.0.201", "10.5.0.202"}; // all available databases
long int MAXIMUM_NUMBER_OF_ALLOWED_TIMEOUTS_BEHIND_EACH_OTHER = 3;
long int TIMEOUT_IN_SECONDS = 4.5;
sem_t* threadIsReadyToSendDataToTheDatabases = new sem_t();
mutex* mtxVotingResultByDatabase = new mutex();
mutex* mtxresultAfterCommitOperationByDatabase = new mutex();
SensorMetadata sensorMetadataFromDatabase;
mutex* mtxsensorMetadataFromDatabase = new mutex();
mutex* mtxTimestampWhenTimoutIsOccuredByDatabase = new mutex();

void controlDatabaseInteraction();
void newInteractWithDatabase(string op, string ip, map<string, SensorMetadata> dataToSend, string requestID);
void waitForResponseOfEachThread(string requestID);
void resetFeedbackMaps();
void executeCommandOnEachReachableDatabase(string commandToBeExecuted, map<string, SensorMetadata> dataToSend, string requestID);
void evaluateVotingResult();

class session
{
public:
  session(boost::asio::io_service& io_service)
    : socket_(io_service)
  {
  }

  tcp::socket& socket()
  {
    return socket_;
  }

  void start()
  {
    socket_.async_read_some(boost::asio::buffer(data_, max_length),
        boost::bind(&session::handle_read, this,
          boost::asio::placeholders::error,
          boost::asio::placeholders::bytes_transferred));
  }

private:
  void handle_read(const boost::system::error_code& error,
      size_t bytes_transferred)
  {
    if (!error)
    {
    	HTTPRequest httpRequest = {};
    	bool completeMessageReceived = false;

    	mtxRequestBuffer->lock();
    	requestBuffer += data_;

    	if (requestBuffer.find("\r\n\r\n") != std::string::npos) { // check if the buffer already contains information about the Content-Length
    		int contentLength = getContentLength(requestBuffer);
    		int lengthOfStringAfterPOSTHeader = calculateLengthOfStringAfterPOSTHeader(requestBuffer);

    		// check if a whole message was received
    		if (lengthOfStringAfterPOSTHeader >= contentLength) {
    			////cout << "length of string " << lengthOfStringAfterPOSTHeader << " >= " << "content length " << contentLength << "\n" << unitbuf;

    			httpRequest = parseHttpRequest(requestBuffer, contentLength);
    			httpRequest.contentLength = contentLength;

    			/*//cout << "Request was:\n";
            	//cout << "Method: " << httpRequest.method << " at " << httpRequest.url << " | Content-Length: " << httpRequest.contentLength << " | Content:\n";
            	//cout << httpRequest.content << "\n" << unitbuf;*/

            	// delete the httpRequestString that we parsed from the requestBuffer string
            	requestBuffer = requestBuffer.substr(requestBuffer.find("\r\n\r\n") + 4 + contentLength, requestBuffer.length());
            	completeMessageReceived = true;
    		} else {
    			//cout << "length of string " << lengthOfStringAfterPOSTHeader << " < " << "content length " << contentLength << "\n" << unitbuf;
    			//cout << "[WARNING] No complete message was received ;((((\n" << unitbuf;
    		}
    	}
    	mtxRequestBuffer->unlock();

    	if (completeMessageReceived) {
        	SensorMetadata sensorMetadata = parseHTTPContent(httpRequest.content);
        	mtxSensorMetadatas->lock();
        		sensorMetadatas->push_back(sensorMetadata);
        		// signal the database interaction thread that he has something to do
        		sem_post(numberOfAvailableDataToSendToDatabase);
        	mtxSensorMetadatas->unlock();
        	////cout 	<< "Parsed sensor data from: " << sensorMetadata.ip << ":" << sensorMetadata.port << ":\n";
        	//cout	<< "temp: " << sensorMetadata.sensorData.temperature << " | bright: " << sensorMetadata.sensorData.brightness << " | hum: " << sensorMetadata.sensorData.humidity << "\n" << unitbuf;
        	////cout << "timestamp of sensor: " << sensorMetadata.timestamp << "\n";

        	//int lengthOfTimestamp = to_string(sensorMetadata.timestamp).length();
        	//int contentLength = 17 + lengthOfTimestamp;
        	int contentLength = {};
    		string reply = "HTTP/1.1 200 OK\r\n";
    		reply +=	"Host: localhost\r\n";
    		reply += 	"Content-Type: text/html; charset=UTF-8\r\n";
    		reply +=	"Content-Length: " + to_string(contentLength) + "\r\n\r\n";

    		////cout << "Generating response...\n";

    		////cout << reply << "\n" << unitbuf;

    		char replyChar[max_length];
    		strcpy(replyChar, reply.c_str());
    		////cout << "Char: " << replyChar << "\n";
    		//size_t reply_length = strlen(replyChar);
    		boost::asio::async_write(socket_,
    			  boost::asio::buffer(replyChar, bytes_transferred),
    			  boost::bind(&session::handle_write, this,
    			  boost::asio::placeholders::error));
    	}
    }
    else
    {
      delete this;
    }
  }

  void handle_write(const boost::system::error_code& error)
  {
    if (!error)
    {
      socket_.async_read_some(boost::asio::buffer(data_, max_length),
          boost::bind(&session::handle_read, this,
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred));
    }
    else
    {
      delete this;
    }
  }

  tcp::socket socket_;
  enum { max_length = 4096 };
  char data_[max_length];
};

bool operator >(const timespec& lhs, const timespec& rhs)
{
    if (lhs.tv_sec == rhs.tv_sec)
        return lhs.tv_nsec > rhs.tv_nsec;
    else
        return lhs.tv_sec > rhs.tv_sec;
}

class server
{
public:
  server(boost::asio::io_service& io_service, short port)
    : io_service_(io_service),
      acceptor_(io_service, tcp::endpoint(tcp::v4(), port))
  {
    start_accept();
  }

private:
  void start_accept()
  {
    session* new_session = new session(io_service_);
    acceptor_.async_accept(new_session->socket(),
        boost::bind(&server::handle_accept, this, new_session,
          boost::asio::placeholders::error));
  }

  void handle_accept(session* new_session,
      const boost::system::error_code& error)
  {
    if (!error)
    {
      new_session->start();
    }
    else
    {
      delete new_session;
    }
    start_accept();
  }

  boost::asio::io_service& io_service_;
  tcp::acceptor acceptor_;
};

int main(int argc, char* argv[]) try {

	sem_init(numberOfAvailableDataToSendToDatabase, 0, 0);

	// initialize the semaphores inside the maps for each database
	for(auto it = std::begin(databases); it != std::end(databases); ++it) {
		numberOfTimeoutsBehindEachOtherByDatabase.insert({*it, 0});
		semaphoreByDatabase.insert({*it, new sem_t()});
		sem_init(semaphoreByDatabase.find(*it)->second, 0, 1);
		votingResultByDatabase.insert({*it, 1});
		resultAfterCommitOperationByDatabase.insert({*it, 0});
	}

	sem_init(threadIsReadyToSendDataToTheDatabases, 0, 0);

	thread databaseInteraction(controlDatabaseInteraction);
	databaseInteraction.detach();

	boost::asio::io_service io_service;

	server s(io_service, 12080);

	io_service.run();

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

HTTPRequest parseHttpRequest(string request, int contentLength) {
	HTTPRequest httpRequest = {};

	httpRequest.method = request.substr(0, request.find(" "));
	httpRequest.url = request.substr(request.find(" ") + 1, request.find("HTTP/1.1") - request.find(" ") - 2);
	httpRequest.content = request.substr(request.find("\r\n\r\n") + 4, contentLength);

	return httpRequest;
}

int calculateLengthOfStringAfterPOSTHeader(string requestBuffer) {
	string hlp = requestBuffer.substr(requestBuffer.find("\r\n\r\n") + 4, requestBuffer.length());
	return hlp.length();
}

int getContentLength(string requestBuffer) {
	string hlp = requestBuffer.substr(requestBuffer.find("Content-Length: ") + 15, requestBuffer.length());
	string hlp2 = hlp.substr(0, hlp.find("\r\n\r\n"));
	return stoi(hlp2);
}

SensorMetadata parseHTTPContent(string httpContent) {
	SensorMetadata sensorMetadata = {};

	sensorMetadata.sensorData.temperature = stod(httpContent.substr(httpContent.find("temperature=") + 12, httpContent.find("&humidity") - httpContent.find("temperature=") - 12));
	sensorMetadata.sensorData.humidity = stod(httpContent.substr(httpContent.find("humidity=") + 9, httpContent.find("&brightness") - httpContent.find("humidity=") - 9));
	sensorMetadata.sensorData.brightness = stod(httpContent.substr(httpContent.find("brightness=") + 11, httpContent.find("&ip") - httpContent.find("brightness=") - 11));

	sensorMetadata.ip = httpContent.substr(httpContent.find("ip=") + 3, httpContent.find("&port") - httpContent.find("ip=") - 3);
	sensorMetadata.port = stoi(httpContent.substr(httpContent.find("port=") + 5, httpContent.find("&timestampSensor") - httpContent.find("port=") - 5));
	sensorMetadata.timestamp = stol(httpContent.substr(httpContent.find("timestampSensor=") + 16, httpContent.length() - httpContent.find("timestampSensor=") - 16));

	return sensorMetadata;
}

void controlDatabaseInteraction() {
	// Beispiel-Daten
	/*SensorMetadata sensorMetadata = {};
	sensorMetadata.sensorData.temperature = 101.;
	sensorMetadata.sensorData.humidity = 101.;
	sensorMetadata.sensorData.brightness = 101.;
	sensorMetadata.ip = "test";
	sensorMetadata.port = 1337;
	sensorMetadata.timestamp = 1038547;
	sensorMetadatas->push_back(sensorMetadata);
	sem_post(numberOfAvailableDataToSendToDatabase);*/
	
	
	// Variables for the CRUD-Execution-Sequence
    int timeToExecuteOtherOperations = 0;
    int executionOrder = 1;
    string commandToExecute = {};
	
	// buffer for the data, that could not be created in the database
    map<string, SensorMetadata> buffer = {};


    int requestID = {};
    string requestIDString = {};

    auto startTimeOfProgram = steady_clock::now();

	
	while(true){
        // nicht-funktionaler Test:
        /*if (numberOfDatabaseInteractions >= 1000) {
        	cout << "Number of Database Interactions: " << numberOfDatabaseInteractions << "\n";
        	cout << "Number of Successful Database Interactions: " << numberOfSuccessfulDatabaseInteractions << "\n";
        	cout << "Number of Successful Database Creations: " << numberOfSuccessfulDatabaseCreations << "\n";
        	cout << "Number of Failed Database Interactions: " << (numberOfDatabaseInteractions - numberOfSuccessfulDatabaseInteractions) << "\n";
			auto endTimeOfProgram = steady_clock::now();
			cout << "Elapsed time in milliseconds: "
				<< duration_cast<milliseconds>(endTimeOfProgram - startTimeOfProgram).count()
				<< " ms" << "\n" << unitbuf;
        	exit(0);
        } else {
        	cout << numberOfDatabaseInteractions << "\n" << unitbuf;
        }*/

		// check if all databases are down
		bool allDatabasesDown = true;
		for (auto iter = numberOfTimeoutsBehindEachOtherByDatabase.begin(); iter != numberOfTimeoutsBehindEachOtherByDatabase.end(); ++iter){
			if((*iter).second <= MAXIMUM_NUMBER_OF_ALLOWED_TIMEOUTS_BEHIND_EACH_OTHER){
				allDatabasesDown = false;
				break;
			}
		}	
		if(allDatabasesDown){
			cout << "Commit-Error: Can't commit! All databases are down." << endl << unitbuf;
			break;
		}
		
		++requestID;
		requestIDString = to_string(requestID);

		// wait till there is data available and prepare the data
		sem_wait(numberOfAvailableDataToSendToDatabase);

        map<string, SensorMetadata> dataToSend = {};
        mtxSensorMetadatas->lock();
            for (vector<SensorMetadata>::iterator it = sensorMetadatas->begin() ; it != sensorMetadatas->end(); ++it) {
                string id = (*it).ip + ":" + std::to_string((*it).timestamp);
                dataToSend[id] = (*it);
                // decrement the semaphore
                sem_trywait(numberOfAvailableDataToSendToDatabase);
            }
            // delete the sent data from the vector
            sensorMetadatas->clear();
        mtxSensorMetadatas->unlock();

        timeToExecuteOtherOperations++;
		
		
		// Create-Block: First try to vote for Create-Op. 
		// If vote successfull, then create in all databases
		try{
			cout << "/-----$$$$$\t\tVOTING FOR CREATE\t\t$$$$$-----\\\n" << unitbuf;
			executeCommandOnEachReachableDatabase("isCreatePossible", dataToSend, requestIDString);
			
			waitForResponseOfEachThread(requestIDString); // voting messages for create operation

			evaluateVotingResult();
			
			// Once the code is here, there IS an unanimous vote! Otherwise it would have thrown an exception!
			executeCommandOnEachReachableDatabase("create", dataToSend, requestIDString);

			waitForResponseOfEachThread(requestIDString); // commit messages for create operation

			for(auto itr = resultAfterCommitOperationByDatabase.begin(); itr != resultAfterCommitOperationByDatabase.end(); ++itr) {
				if (numberOfTimeoutsBehindEachOtherByDatabase[itr->first] <= MAXIMUM_NUMBER_OF_ALLOWED_TIMEOUTS_BEHIND_EACH_OTHER) {
		            if(itr->second == 0){
		            	cout << "Create-Op: Database create was successfully executed!" << endl << unitbuf;
		            	++numberOfSuccessfulDatabaseInteractions;
		            	++numberOfSuccessfulDatabaseCreations;
		            } else if (itr->second == -3) {
		            	cout << "Create-Op: Database create was partially successfully executed!" << endl << unitbuf;
		            	++numberOfSuccessfulDatabaseInteractions;
		            	++numberOfSuccessfulDatabaseCreations;
		            } else if (itr->second == -1){
		        		cout << "General Error while trying to create in database! Trying again next iteration with previous data in buffer. Error: " << itr->second << endl << unitbuf;
		            } else {
		            	cout << "An error occurred while trying to create in database! Error: " << itr->second << endl << unitbuf;
		            }
		        }
			}

			cout << "\\-----$$$$$\tCREATE PROCESS FINISHED SUCCESSFUL $$$$$-----/\n" << unitbuf;

			buffer.clear();
			resetFeedbackMaps(); // reset votingResultByDatabase and resultAfterCommitOperationByDatabase
		}
		catch(int code){
			if(code < 0){
				cout << "Commit-Error: Aborting commit! Couldn't get a unanimous vote. Trying with buffer next iteration." << endl << unitbuf;
				cout << "\\-----$$$$$\tCREATE PROCESS FINISHED WITH FAILURES $$$$$-----/\n" << unitbuf;
				buffer.insert(dataToSend.begin(), dataToSend.end());
				resetFeedbackMaps();
				continue;
			}
		}

		// This part executes every three Create-Operation the other CRUD-Operations respectively
		if(timeToExecuteOtherOperations == 3){
			try{
				++requestID;
				requestIDString = to_string(requestID);
				if(executionOrder == 1){ // update
					cout << "/-----$$$$$\t\tVOTING FOR UPDATE\t\t$$$$$-----\\\n" << unitbuf;
					executeCommandOnEachReachableDatabase("isUpdatePossible", dataToSend, requestIDString);

					waitForResponseOfEachThread(requestIDString);

					evaluateVotingResult();

					// Once the code is here, there IS an unanimous vote! Otherwise it would have thrown an exception!
					executeCommandOnEachReachableDatabase("update", dataToSend, requestIDString);

					waitForResponseOfEachThread(requestIDString); // commit messages for create operation

					for(auto itr = resultAfterCommitOperationByDatabase.begin(); itr != resultAfterCommitOperationByDatabase.end(); ++itr) {
						if (numberOfTimeoutsBehindEachOtherByDatabase[itr->first] <= MAXIMUM_NUMBER_OF_ALLOWED_TIMEOUTS_BEHIND_EACH_OTHER) {
				            if(itr->second == 0){
		                    	SensorMetadata data = dataToSend.begin()->second;
		                    	cout << "Update-Op: Sensor-Data with ID " << data.ip << ":" << data.timestamp << " with following Data: " <<
		                    			"Brightness: " << data.sensorData.brightness << "; Humidity: " << data.sensorData.humidity <<
										"; Temperature: " << data.sensorData.temperature << endl << unitbuf;
				            	++numberOfSuccessfulDatabaseInteractions;
				            } else {
				            	cout << "Update-Op: An error occurred while trying to update the database " << itr->first << "! Error: " << itr->second << endl << unitbuf;
				            }
				        }
					}
					cout << "\\-----$$$$$\tUPDATE PROCESS FINISHED SUCCESSFUL $$$$$-----/\n" << unitbuf;
				} else if (executionOrder == 2) { // delete
					cout << "/-----$$$$$\t\tVOTING FOR DELETE\t\t$$$$$-----\\\n" << unitbuf;
					executeCommandOnEachReachableDatabase("isDeletePossible", dataToSend, requestIDString);

					waitForResponseOfEachThread(requestIDString);

					evaluateVotingResult();

					// Once the code is here, there IS an unanimous vote! Otherwise it would have thrown an exception!
					executeCommandOnEachReachableDatabase("delete", dataToSend, requestIDString);

					waitForResponseOfEachThread(requestIDString); // commit messages for create operation

					for(auto itr = resultAfterCommitOperationByDatabase.begin(); itr != resultAfterCommitOperationByDatabase.end(); ++itr) {
						if (numberOfTimeoutsBehindEachOtherByDatabase[itr->first] <= MAXIMUM_NUMBER_OF_ALLOWED_TIMEOUTS_BEHIND_EACH_OTHER) {
				            if(itr->second == 0){
		                    	SensorMetadata data = dataToSend.begin()->second;
		                    	cout << "Delete-Op: Sensor-Data with ID " << data.ip << ":" << data.timestamp << " with following Data: " <<
		                    			"Brightness: " << data.sensorData.brightness << "; Humidity: " << data.sensorData.humidity <<
										"; Temperature: " << data.sensorData.temperature << endl << unitbuf;
		                    	++numberOfSuccessfulDatabaseInteractions;
				            } else {
				            	cout << "Delete-Op: An error occurred while trying to update the database " << itr->first << "! Error: " << itr->second << endl << unitbuf;
				            }
				        }
					}
					cout << "\\-----$$$$$\tDELETE PROCESS FINISHED SUCCESSFUL $$$$$-----/\n" << unitbuf;
				} else if (executionOrder == 3) { // read
					for(auto itr = numberOfTimeoutsBehindEachOtherByDatabase.begin(); itr != numberOfTimeoutsBehindEachOtherByDatabase.end(); ++itr) {
						if (itr->second <= MAXIMUM_NUMBER_OF_ALLOWED_TIMEOUTS_BEHIND_EACH_OTHER) { // ask databases one by one and continue as soon as one database can answer
							thread voteForCreateDB(newInteractWithDatabase, "read", itr->first, dataToSend, requestIDString);
							voteForCreateDB.detach();
							sem_wait(threadIsReadyToSendDataToTheDatabases); // wait till new thread signales, that it is ready to send the data (that means, it has the semaphore from the global semaphoreByDatabase map)

							// wait till this on database has given an answer
						    // needed to check for timeout
							struct timespec ts;
							int returnCode = {};

							if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
								perror("clock_gettime");
								exit(EXIT_FAILURE);
							}
							ts.tv_sec += TIMEOUT_IN_SECONDS;
							while ((returnCode = sem_timedwait((semaphoreByDatabase[itr->first]), &ts)) == -1 && errno == EINTR)
								continue;
							if (returnCode == -1) {
								if (errno == ETIMEDOUT) {
									numberOfTimeoutsBehindEachOtherByDatabase[itr->first] += 1;
									cout << "[ERROR] Database " << itr->first << " timed out.\n" << unitbuf;
								} else {
						            perror("sem_timedwait");
						        }
							} else {
						    	// sem_timedwait() succeeded, we've got the semaphore, so it came a reply in time
								if(sensorMetadataFromDatabase.port > 0){
			                    	//success
			                    	cout << "Read-Op: Sensor-Data with ID " << sensorMetadataFromDatabase.ip << ":" << sensorMetadataFromDatabase.timestamp << " with following Data: " <<
			                    			"Brightness: " << sensorMetadataFromDatabase.sensorData.brightness << "; Humidity: " << sensorMetadataFromDatabase.sensorData.humidity <<
											"; Temperature: " << sensorMetadataFromDatabase.sensorData.temperature << endl << unitbuf;
			                    	++numberOfSuccessfulDatabaseInteractions;
								} else {
									cout << "Read-Op: An error occurred while trying to read from database. Element not found with the ID " << dataToSend.begin()->first <<". Error: " << sensorMetadataFromDatabase.port << endl << unitbuf;
								}
								sem_post(semaphoreByDatabase[itr->first]);
						    }
						} else {
							cout << "[WARNING] Database " << itr->first << " is down.\n" << unitbuf;
						}
					}
				}
				resetFeedbackMaps();
			} catch(int code){
				if(code < 0){
					cout << "Commit-Error: Aborting commit! Couldn't get a unanimous vote." << endl << unitbuf;
					resetFeedbackMaps();
					continue;
				}
			}

			executionOrder++;
			timeToExecuteOtherOperations = 0;
			if(executionOrder == 4){
			executionOrder = 1;
			}
		}
        if (timeToExecuteOtherOperations >= 4) {
        	timeToExecuteOtherOperations = 0;
        }
	}	
}


// New Version
void newInteractWithDatabase(string op, string ip, map<string, SensorMetadata> dataToSend, string requestID){
	try {
		//std::shared_ptr<TTransport> socket(new TSocket("localhost", 9090));
		std::shared_ptr<TTransport> socket(new TSocket(ip, 9090)); // hier die ip
		std::shared_ptr<TTransport> transport(new TFramedTransport(socket));
		std::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
		DatabaseClient database(protocol);

		// insert a new timestamp value for the case the waitForResponse-Method has not created a dataset yet
		string keyOfTimestampWhenTimoutIsOccuredByDatabase = ip + "-" + requestID;
		mtxTimestampWhenTimoutIsOccuredByDatabase->lock();
	    if (timestampWhenTimoutIsOccuredByDatabase.find(keyOfTimestampWhenTimoutIsOccuredByDatabase) != timestampWhenTimoutIsOccuredByDatabase.end()) {
	        // key exists already
	    } else {
			struct timespec ts;
			ts.tv_sec = 0;
			ts.tv_nsec = 0;
			timestampWhenTimoutIsOccuredByDatabase.insert({keyOfTimestampWhenTimoutIsOccuredByDatabase, ts});
	    }
	    mtxTimestampWhenTimoutIsOccuredByDatabase->unlock();

		int semaphoreCount = {};
		int votingResult = 1;
		sem_getvalue(semaphoreByDatabase.find(ip)->second, &semaphoreCount);
		for (int i = 0; i < semaphoreCount; ++i) {
			sem_trywait(semaphoreByDatabase.find(ip)->second); // take the semaphore for the specific database
			/*
			 * It could be the case, that the semaphore has a value > 1, so we have to decrement it till 0.
			 * Because if a newInteractWithDatabase has an tcp socket exception, then the semaphore isn't incremented again to avoid
			 * the situation, where the controller thinks that the thread answered (because it gave the semahpore back) but it only was an exception.
			 */
		}
		sem_post(threadIsReadyToSendDataToTheDatabases); // tell the controller, that this thread is ready

		// signalisiert dem controller, dass create auf einer datenbank möglich/nicht möglich ist
		if(op == "isCreatePossible"){
			cout << "Voting for creation at " << ip << "...\n" << unitbuf;
			transport->open();
				votingResult = database.isCreatePossible(dataToSend); // try to reach the database and write the result back into the map
			transport->close();
			struct timespec ts;
			if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
				perror("clock_gettime");
				exit(EXIT_FAILURE);
			}
			mtxTimestampWhenTimoutIsOccuredByDatabase->lock();
			if (timestampWhenTimoutIsOccuredByDatabase[keyOfTimestampWhenTimoutIsOccuredByDatabase] > ts || timestampWhenTimoutIsOccuredByDatabase[keyOfTimestampWhenTimoutIsOccuredByDatabase].tv_sec == 0) { // nur Rückgabewert in die Map eintragen, falls Result innerhalb der Timeout-Zeit kam
				votingResultByDatabase[ip] = votingResult;
				sem_post(semaphoreByDatabase.find(ip)->second); // give the controller the signal, that a result came from the datbase
			}
			mtxTimestampWhenTimoutIsOccuredByDatabase->unlock();
			return;
		}

		// signalisiert dem controller, dass update auf einer Datenbank möglich/nicht möglich ist
		if(op == "isUpdatePossible"){
			cout << "Voting for update of " << dataToSend.begin()->first << " at " << ip << "...\n" << unitbuf;
			transport->open();
				votingResult = database.isUpdatePossible(dataToSend.begin()->first); // try to reach the database and write the result back into the map
			transport->close();
			struct timespec ts;
			if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
				perror("clock_gettime");
				exit(EXIT_FAILURE);
			}
			mtxTimestampWhenTimoutIsOccuredByDatabase->lock();
			if (timestampWhenTimoutIsOccuredByDatabase[keyOfTimestampWhenTimoutIsOccuredByDatabase] > ts || timestampWhenTimoutIsOccuredByDatabase[keyOfTimestampWhenTimoutIsOccuredByDatabase].tv_sec == 0) { // nur Rückgabewert in die Map eintragen, falls Result innerhalb der Timeout-Zeit kam
				votingResultByDatabase[ip] = votingResult;
				sem_post(semaphoreByDatabase.find(ip)->second); // give the controller the signal, that a result came from the datbase
			}
			mtxTimestampWhenTimoutIsOccuredByDatabase->unlock();
			return;
		}

		// signalisiert dem controller, dass delete auf einer datenbank möglich/nicht möglich ist
		if(op == "isDeletePossible"){
			cout << "Voting for deletion of " << dataToSend.begin()->first << " at " << ip << "...\n" << unitbuf;
			transport->open();
				votingResult = database.isDeletePossible(dataToSend.begin()->first); // try to reach the database and write the result back into the map
			transport->close();
			struct timespec ts;
			if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
				perror("clock_gettime");
				exit(EXIT_FAILURE);
			}
			mtxTimestampWhenTimoutIsOccuredByDatabase->lock();
			if (timestampWhenTimoutIsOccuredByDatabase[keyOfTimestampWhenTimoutIsOccuredByDatabase] > ts || timestampWhenTimoutIsOccuredByDatabase[keyOfTimestampWhenTimoutIsOccuredByDatabase].tv_sec == 0) { // nur Rückgabewert in die Map eintragen, falls Result innerhalb der Timeout-Zeit kam
				votingResultByDatabase[ip] = votingResult;
				sem_post(semaphoreByDatabase.find(ip)->second); // give the controller the signal, that a result came from the datbase
			}
			mtxTimestampWhenTimoutIsOccuredByDatabase->unlock();
			return;
		}

		if(op == "create"){
			cout << "Committing create operation at " << ip << "...\n" << unitbuf;
			++numberOfDatabaseInteractions;
			transport->open();
				resultAfterCommitOperationByDatabase[ip] = database.createSensorMetadata(dataToSend); // try to reach the database and write the result back into the map
			transport->close();
			sem_post(semaphoreByDatabase.find(ip)->second); // give the controller the signal, that a result came from the datbase
			return;
		}
	
		if(op == "update"){
			cout << "Committing update operation at " << ip << "...\n" << unitbuf;
			++numberOfDatabaseInteractions;
			transport->open();
				resultAfterCommitOperationByDatabase[ip] = database.updateSensorMetadata(dataToSend.begin()->first, dataToSend.begin()->second); // try to reach the database and write the result back into the map
			transport->close();
			sem_post(semaphoreByDatabase.find(ip)->second); // give the controller the signal, that a result came from the datbase
			return;
		}
	
		if(op == "delete"){
			cout << "Committing delete operation at " << ip << "...\n" << unitbuf;
			++numberOfDatabaseInteractions;
			transport->open();
				resultAfterCommitOperationByDatabase[ip] = database.deleteSensorMetadata(dataToSend.begin()->first); // try to reach the database and write the result back into the map
			transport->close();
			sem_post(semaphoreByDatabase.find(ip)->second); // give the controller the signal, that a result came from the datbase
			return;
		}
	
		if(op == "read"){
			++numberOfDatabaseInteractions;
			transport->open();
				database.readSensorMetadata(sensorMetadataFromDatabase, dataToSend.begin()->first); // try to reach the database and write the result back into the map
			transport->close();
			sem_post(semaphoreByDatabase.find(ip)->second); // give the controller the signal, that a result came from the datbase
			return;
		}
	} catch (TTransportException& e) {
		cout << "[ERROR] TCP Socket Failure: " << e.what() << "\n" << unitbuf;
		resultAfterCommitOperationByDatabase[ip] = -1;
		sensorMetadataFromDatabase.port = -2;
	}
}

void executeCommandOnEachReachableDatabase(string commandToBeExecuted, map<string, SensorMetadata> dataToSend, string requestID) {
	for(auto itr = numberOfTimeoutsBehindEachOtherByDatabase.begin(); itr != numberOfTimeoutsBehindEachOtherByDatabase.end(); ++itr) {
		if (itr->second <= MAXIMUM_NUMBER_OF_ALLOWED_TIMEOUTS_BEHIND_EACH_OTHER) {
			thread voteForCreateDB(newInteractWithDatabase, commandToBeExecuted, itr->first, dataToSend, requestID);
			voteForCreateDB.detach();
			sem_wait(threadIsReadyToSendDataToTheDatabases); // wait till new thread signales, that it is ready to send the data (that means, it has the semaphore from the global semaphoreByDatabase map)
		} else {
			cout << "[WARNING] Database " << itr->first << " is down.\n" << unitbuf;
		}
	}
}

void waitForResponseOfEachThread(string requestID) {
    // needed to check for timeout
	struct timespec ts;
	int returnCode = {};

	// wait for the response of each thread
	for(auto itr = numberOfTimeoutsBehindEachOtherByDatabase.begin(); itr != numberOfTimeoutsBehindEachOtherByDatabase.end(); ++itr) {
		if (itr->second <= MAXIMUM_NUMBER_OF_ALLOWED_TIMEOUTS_BEHIND_EACH_OTHER) {
			if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
				perror("clock_gettime");
				exit(EXIT_FAILURE);
			}
			ts.tv_sec += TIMEOUT_IN_SECONDS;

			// insert or update new timestamp value
			string keyOfTimestampWhenTimoutIsOccuredByDatabase = itr->first + "-" + requestID;
			mtxTimestampWhenTimoutIsOccuredByDatabase->lock();
		    if (timestampWhenTimoutIsOccuredByDatabase.find(keyOfTimestampWhenTimoutIsOccuredByDatabase) != timestampWhenTimoutIsOccuredByDatabase.end()) {
		        // key exists already
		    	timestampWhenTimoutIsOccuredByDatabase[keyOfTimestampWhenTimoutIsOccuredByDatabase] = ts;
		    } else {
				timestampWhenTimoutIsOccuredByDatabase.insert({keyOfTimestampWhenTimoutIsOccuredByDatabase, ts});
		    }
		    mtxTimestampWhenTimoutIsOccuredByDatabase->unlock();

			while ((returnCode = sem_timedwait((semaphoreByDatabase[itr->first]), &ts)) == -1 && errno == EINTR)
				continue;
			if (returnCode == -1) {
				if (errno == ETIMEDOUT) {
					numberOfTimeoutsBehindEachOtherByDatabase[itr->first] += 1;
					votingResultByDatabase[itr->first] = -1;
					resultAfterCommitOperationByDatabase[itr->first] = -1;
					cout << "[ERROR] Database " << itr->first << " timed out.\n" << unitbuf;
				} else {
		            perror("sem_timedwait");
		        }
			} else {
		    	// sem_timedwait() succeeded, we've got the semaphore, so it came a reply in time
				numberOfTimeoutsBehindEachOtherByDatabase[itr->first] = 0;
		    }
			sem_post(semaphoreByDatabase[itr->first]);
		}
	}
}

void evaluateVotingResult() {
	// evaluate voting result
	for (auto iter = votingResultByDatabase.begin(); iter != votingResultByDatabase.end(); ++iter){
		if (numberOfTimeoutsBehindEachOtherByDatabase[iter->first] <= MAXIMUM_NUMBER_OF_ALLOWED_TIMEOUTS_BEHIND_EACH_OTHER) {
			cout << "Database " << iter->first << " voted " << (*iter).second << "\n" << unitbuf;
			if((*iter).second < 0){ // wenn auch nur eine einzige Datenbank anders als mit 0 abgestimmt hat, dann abort
				throw(-1);
			} else if ((*iter).second > 0) {
				cout << "[ERROR] A database hasn't voted yet, but should have at this point of code!\n" << unitbuf;
				exit(-1);
			}
		}
	}
}

void resetFeedbackMaps() {
    // Reset Commit Votes
	for (auto iter = votingResultByDatabase.begin(); iter != votingResultByDatabase.end(); ++iter){
		(*iter).second = 1;
	}

    // Reset Commit Timeouts numberOfTimeoutsBehindEachOtherByDatabase
	for (auto iter = resultAfterCommitOperationByDatabase.begin(); iter != resultAfterCommitOperationByDatabase.end(); ++iter){
		(*iter).second = 0;
	}

    // Reset Timeout Timestamps
	mtxTimestampWhenTimoutIsOccuredByDatabase->lock();
	timestampWhenTimoutIsOccuredByDatabase.clear();
	mtxTimestampWhenTimoutIsOccuredByDatabase->unlock();

	SensorMetadata s;
	sensorMetadataFromDatabase = s;
}
