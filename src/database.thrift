struct SensorData {
 1: double temperature,
 2: double brightness,
 3: double humidity
}

struct SensorMetadata {
 1: i32 port,
 2: string ip,
 3: i64 timestamp,
 4: SensorData sensorData
}

service Database {
 void ping(),
 i32 updateSensorMetadata(1: string id, 2: SensorMetadata sensorMetadata),
 i32 deleteSensorMetadata(1: string id),
 SensorMetadata readSensorMetadata(1: string id),
 i32 createSensorMetadata(1: map<string,SensorMetadata> sensorMetadataCollection),
 map<string,SensorMetadata> readAll(1: i32 numberOfRows),
 i32 isUpdatePossible(1: string id),
 i32 isDeletePossible(1: string id),
 i32 isCreatePossible(1: map<string,SensorMetadata> sensorMetadataCollection)
}
