/**
 * Autogenerated by Thrift Compiler (0.16.0)
 *
 * DO NOT EDIT UNLESS YOU ARE SURE THAT YOU KNOW WHAT YOU ARE DOING
 *  @generated
 */
#include "database_types.h"

#include <algorithm>
#include <ostream>

#include <thrift/TToString.h>




SensorData::~SensorData() noexcept {
}


void SensorData::__set_temperature(const double val) {
  this->temperature = val;
}

void SensorData::__set_brightness(const double val) {
  this->brightness = val;
}

void SensorData::__set_humidity(const double val) {
  this->humidity = val;
}
std::ostream& operator<<(std::ostream& out, const SensorData& obj)
{
  obj.printTo(out);
  return out;
}


uint32_t SensorData::read(::apache::thrift::protocol::TProtocol* iprot) {

  ::apache::thrift::protocol::TInputRecursionTracker tracker(*iprot);
  uint32_t xfer = 0;
  std::string fname;
  ::apache::thrift::protocol::TType ftype;
  int16_t fid;

  xfer += iprot->readStructBegin(fname);

  using ::apache::thrift::protocol::TProtocolException;


  while (true)
  {
    xfer += iprot->readFieldBegin(fname, ftype, fid);
    if (ftype == ::apache::thrift::protocol::T_STOP) {
      break;
    }
    switch (fid)
    {
      case 1:
        if (ftype == ::apache::thrift::protocol::T_DOUBLE) {
          xfer += iprot->readDouble(this->temperature);
          this->__isset.temperature = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      case 2:
        if (ftype == ::apache::thrift::protocol::T_DOUBLE) {
          xfer += iprot->readDouble(this->brightness);
          this->__isset.brightness = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      case 3:
        if (ftype == ::apache::thrift::protocol::T_DOUBLE) {
          xfer += iprot->readDouble(this->humidity);
          this->__isset.humidity = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      default:
        xfer += iprot->skip(ftype);
        break;
    }
    xfer += iprot->readFieldEnd();
  }

  xfer += iprot->readStructEnd();

  return xfer;
}

uint32_t SensorData::write(::apache::thrift::protocol::TProtocol* oprot) const {
  uint32_t xfer = 0;
  ::apache::thrift::protocol::TOutputRecursionTracker tracker(*oprot);
  xfer += oprot->writeStructBegin("SensorData");

  xfer += oprot->writeFieldBegin("temperature", ::apache::thrift::protocol::T_DOUBLE, 1);
  xfer += oprot->writeDouble(this->temperature);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldBegin("brightness", ::apache::thrift::protocol::T_DOUBLE, 2);
  xfer += oprot->writeDouble(this->brightness);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldBegin("humidity", ::apache::thrift::protocol::T_DOUBLE, 3);
  xfer += oprot->writeDouble(this->humidity);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldStop();
  xfer += oprot->writeStructEnd();
  return xfer;
}

void swap(SensorData &a, SensorData &b) {
  using ::std::swap;
  swap(a.temperature, b.temperature);
  swap(a.brightness, b.brightness);
  swap(a.humidity, b.humidity);
  swap(a.__isset, b.__isset);
}

SensorData::SensorData(const SensorData& other0) noexcept {
  temperature = other0.temperature;
  brightness = other0.brightness;
  humidity = other0.humidity;
  __isset = other0.__isset;
}
SensorData& SensorData::operator=(const SensorData& other1) noexcept {
  temperature = other1.temperature;
  brightness = other1.brightness;
  humidity = other1.humidity;
  __isset = other1.__isset;
  return *this;
}
void SensorData::printTo(std::ostream& out) const {
  using ::apache::thrift::to_string;
  out << "SensorData(";
  out << "temperature=" << to_string(temperature);
  out << ", " << "brightness=" << to_string(brightness);
  out << ", " << "humidity=" << to_string(humidity);
  out << ")";
}


SensorMetadata::~SensorMetadata() noexcept {
}


void SensorMetadata::__set_port(const int32_t val) {
  this->port = val;
}

void SensorMetadata::__set_ip(const std::string& val) {
  this->ip = val;
}

void SensorMetadata::__set_timestamp(const int64_t val) {
  this->timestamp = val;
}

void SensorMetadata::__set_sensorData(const SensorData& val) {
  this->sensorData = val;
}
std::ostream& operator<<(std::ostream& out, const SensorMetadata& obj)
{
  obj.printTo(out);
  return out;
}


uint32_t SensorMetadata::read(::apache::thrift::protocol::TProtocol* iprot) {

  ::apache::thrift::protocol::TInputRecursionTracker tracker(*iprot);
  uint32_t xfer = 0;
  std::string fname;
  ::apache::thrift::protocol::TType ftype;
  int16_t fid;

  xfer += iprot->readStructBegin(fname);

  using ::apache::thrift::protocol::TProtocolException;


  while (true)
  {
    xfer += iprot->readFieldBegin(fname, ftype, fid);
    if (ftype == ::apache::thrift::protocol::T_STOP) {
      break;
    }
    switch (fid)
    {
      case 1:
        if (ftype == ::apache::thrift::protocol::T_I32) {
          xfer += iprot->readI32(this->port);
          this->__isset.port = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      case 2:
        if (ftype == ::apache::thrift::protocol::T_STRING) {
          xfer += iprot->readString(this->ip);
          this->__isset.ip = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      case 3:
        if (ftype == ::apache::thrift::protocol::T_I64) {
          xfer += iprot->readI64(this->timestamp);
          this->__isset.timestamp = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      case 4:
        if (ftype == ::apache::thrift::protocol::T_STRUCT) {
          xfer += this->sensorData.read(iprot);
          this->__isset.sensorData = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      default:
        xfer += iprot->skip(ftype);
        break;
    }
    xfer += iprot->readFieldEnd();
  }

  xfer += iprot->readStructEnd();

  return xfer;
}

uint32_t SensorMetadata::write(::apache::thrift::protocol::TProtocol* oprot) const {
  uint32_t xfer = 0;
  ::apache::thrift::protocol::TOutputRecursionTracker tracker(*oprot);
  xfer += oprot->writeStructBegin("SensorMetadata");

  xfer += oprot->writeFieldBegin("port", ::apache::thrift::protocol::T_I32, 1);
  xfer += oprot->writeI32(this->port);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldBegin("ip", ::apache::thrift::protocol::T_STRING, 2);
  xfer += oprot->writeString(this->ip);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldBegin("timestamp", ::apache::thrift::protocol::T_I64, 3);
  xfer += oprot->writeI64(this->timestamp);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldBegin("sensorData", ::apache::thrift::protocol::T_STRUCT, 4);
  xfer += this->sensorData.write(oprot);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldStop();
  xfer += oprot->writeStructEnd();
  return xfer;
}

void swap(SensorMetadata &a, SensorMetadata &b) {
  using ::std::swap;
  swap(a.port, b.port);
  swap(a.ip, b.ip);
  swap(a.timestamp, b.timestamp);
  swap(a.sensorData, b.sensorData);
  swap(a.__isset, b.__isset);
}

SensorMetadata::SensorMetadata(const SensorMetadata& other2) {
  port = other2.port;
  ip = other2.ip;
  timestamp = other2.timestamp;
  sensorData = other2.sensorData;
  __isset = other2.__isset;
}
SensorMetadata& SensorMetadata::operator=(const SensorMetadata& other3) {
  port = other3.port;
  ip = other3.ip;
  timestamp = other3.timestamp;
  sensorData = other3.sensorData;
  __isset = other3.__isset;
  return *this;
}
void SensorMetadata::printTo(std::ostream& out) const {
  using ::apache::thrift::to_string;
  out << "SensorMetadata(";
  out << "port=" << to_string(port);
  out << ", " << "ip=" << to_string(ip);
  out << ", " << "timestamp=" << to_string(timestamp);
  out << ", " << "sensorData=" << to_string(sensorData);
  out << ")";
}


