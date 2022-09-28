// This autogenerated skeleton file illustrates how to build a server.
// You should copy it to another filename to avoid overwriting it.

#include "Database.h"
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;

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
    // Your implementation goes here
    printf("updateSensorMetadata\n");
  }

  int32_t deleteSensorMetadata(const std::string& id) {
    // Your implementation goes here
    printf("deleteSensorMetadata\n");
  }

  void readSensorMetadata(SensorMetadata& _return, const std::string& id) {
    // Your implementation goes here
    printf("readSensorMetadata\n");
  }

  int32_t createSensorMetadata(const std::map<std::string, SensorMetadata> & sensorMetadataCollection) {
    // Your implementation goes here
    printf("createSensorMetadata\n");
  }

  void readAll(std::map<std::string, SensorMetadata> & _return, const int32_t numberOfRows) {
    // Your implementation goes here
    printf("readAll\n");
  }

  int32_t isUpdatePossible(const std::string& id) {
    // Your implementation goes here
    printf("isUpdatePossible\n");
  }

  int32_t isDeletePossible(const std::string& id) {
    // Your implementation goes here
    printf("isDeletePossible\n");
  }

  int32_t isCreatePossible(const std::map<std::string, SensorMetadata> & sensorMetadataCollection) {
    // Your implementation goes here
    printf("isCreatePossible\n");
  }

};

int main(int argc, char **argv) {
  int port = 9090;
  ::std::shared_ptr<DatabaseHandler> handler(new DatabaseHandler());
  ::std::shared_ptr<TProcessor> processor(new DatabaseProcessor(handler));
  ::std::shared_ptr<TServerTransport> serverTransport(new TServerSocket(port));
  ::std::shared_ptr<TTransportFactory> transportFactory(new TBufferedTransportFactory());
  ::std::shared_ptr<TProtocolFactory> protocolFactory(new TBinaryProtocolFactory());

  TSimpleServer server(processor, serverTransport, transportFactory, protocolFactory);
  server.serve();
  return 0;
}
